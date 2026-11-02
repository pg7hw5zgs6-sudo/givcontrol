#include "ota.h"
#include "version.h"
#include <Update.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_ota_ops.h>

static WebServer* srv = nullptr;
static Preferences* nv = nullptr;
static void (*logFn)(uint8_t, const char*) = nullptr;
static bool (*busyFn)() = nullptr;

static char pin[33] = "";              // "" = updates off until the user sets one
static String upErr;                   // why the current web upload was refused
static bool upOk = false, headerChecked = false;
static uint32_t rebootAt = 0, badPinUntil = 0;
static bool pendingVerify = false;     // running a new image that isn't confirmed yet
static bool arduinoOtaOn = false;

#if CONFIG_IDF_TARGET_ESP32C6
static const char* BOARD = "ESP32-C6";
#else
static const char* BOARD = "CYD (ESP32)";
#endif

// Arduino core hook: keep a freshly installed image "pending" so we can confirm it ourselves.
extern "C" bool verifyRollbackLater() { return true; }

static void logf(uint8_t sev, const char* fmt, ...) {
  char b[96]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  if (logFn) logFn(sev, b);
}

static bool pinOk(const String& p) { return pin[0] && p == pin; }

static void startArduinoOta() {
  if (arduinoOtaOn || !pin[0]) return;
  ArduinoOTA.setMdnsEnabled(false);   // mDNS is already running for the web page
  ArduinoOTA.setPassword(pin);
  ArduinoOTA.onStart([]() { logf(0, "Firmware update started (espota)"); });
  ArduinoOTA.onEnd([]() { if (nv) { nv->putString("otaFrom", GC_VERSION); } });
  ArduinoOTA.onError([](ota_error_t e) { logf(3, "Firmware update (espota) failed: error %d", (int)e); });
  ArduinoOTA.begin();
  MDNS.enableArduino(3232, true);
  arduinoOtaOn = true;
}

// web upload: POST /update?size=N with header X-Update-Pin, body = the .bin
static void onUpload() {
  HTTPUpload& up = srv->upload();
  if (up.status == UPLOAD_FILE_START) {
    upErr = ""; upOk = false; headerChecked = false;
    size_t size = srv->arg("size").toInt();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    if (millis() < badPinUntil) upErr = "Wait a few seconds and try again";
    else if (!pin[0]) upErr = "Set an update PIN first";
    else if (!pinOk(srv->header("X-Update-Pin"))) { upErr = "Wrong update PIN"; badPinUntil = millis() + 5000; }
    else if (busyFn && busyFn()) upErr = "A settings change is being written to the inverter: try again when it has finished";
    else if (!next) upErr = "This flash layout has no second firmware slot";
    else if (!size || size > next->size) upErr = "The file is " + String(size / 1024) + " KB, bigger than the " + String(next->size / 1024) + " KB firmware slot";
    else if (!Update.begin(size, U_FLASH)) upErr = String("Can't start the update: ") + Update.errorString();
    if (!upErr.length()) logf(0, "Firmware update started (web, %u KB)", (unsigned)(size / 1024));
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (upErr.length()) return;
    if (!headerChecked) {   // ESP image header: magic 0xE9, chip id at byte 12
      headerChecked = true;
      uint16_t chip = up.currentSize >= 14 ? up.buf[12] | (up.buf[13] << 8) : 0xFFFF;
      if (up.currentSize < 24 || up.buf[0] != 0xE9) upErr = "Not an ESP32 firmware image (.bin)";
      else if (chip != CONFIG_IDF_FIRMWARE_CHIP_ID)
        upErr = String("This firmware is for a different chip (id ") + chip + "), not this " + BOARD + " board";
      if (upErr.length()) { Update.abort(); return; }
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) { upErr = String("Write failed: ") + Update.errorString(); Update.abort(); }
  } else if (up.status == UPLOAD_FILE_END) {
    if (upErr.length()) return;
    if (Update.end(true)) upOk = true;   // checks the image's checksum and SHA-256
    else upErr = String("The image didn't verify: ") + Update.errorString();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort(); upErr = "Upload interrupted";
  }
}

static void onUploadDone() {
  if (!upOk) {
    logf(3, "Firmware update refused: %s", upErr.length() ? upErr.c_str() : "no file");
    srv->send(400, "application/json", "{\"error\":\"" + (upErr.length() ? upErr : String("No file received")) + "\"}");
    return;
  }
  nv->putString("otaFrom", GC_VERSION);   // the next boot tells whether the new image took over
  logf(1, "Firmware received and verified: restarting into it");
  srv->send(200, "application/json", "{\"ok\":true}");
  rebootAt = millis() + 1500;
}

static void onPin() {
  if (srv->method() != HTTP_POST) { srv->send(400, "application/json", "{\"error\":\"POST only\"}"); return; }
  String np = srv->arg("pin"), old = srv->arg("old");
  if (millis() < badPinUntil) { srv->send(400, "application/json", "{\"error\":\"Wait a few seconds and try again\"}"); return; }
  if (pin[0] && !pinOk(old)) { badPinUntil = millis() + 5000; srv->send(400, "application/json", "{\"error\":\"The current PIN is wrong\"}"); return; }
  if (np.length() < 6 || np.length() > 32) { srv->send(400, "application/json", "{\"error\":\"Use 6 to 32 characters\"}"); return; }
  strlcpy(pin, np.c_str(), sizeof pin);
  nv->putString("otaPin", pin);
  if (arduinoOtaOn) ArduinoOTA.setPassword(pin); else startArduinoOta();
  logf(1, "Update PIN set");
  srv->send(200, "application/json", "{\"ok\":true}");
}

void otaBegin(WebServer& server, Preferences& prefs, void (*log)(uint8_t, const char*), bool (*busy)()) {
  srv = &server; nv = &prefs; logFn = log; busyFn = busy;
  prefs.getString("otaPin", pin, sizeof pin);

  // after an update: did the new image take over, or did the bootloader roll back?
  String from = prefs.getString("otaFrom", "");
  const esp_partition_t* run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  pendingVerify = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  if (from.length()) {
    if (from == GC_VERSION && !pendingVerify) {
      logf(3, "Firmware update didn't confirm: rolled back to %s", GC_VERSION);
      prefs.remove("otaFrom");
    } else if (!pendingVerify) {
      prefs.remove("otaFrom");   // installed over USB since; nothing to report
    }
  }
  if (pendingVerify) logf(0, "New firmware %s running: confirming once Wi-Fi and the inverter answer (2 min)", GC_VERSION);

  static const char* hdrs[] = {"X-Update-Pin"};
  server.collectHeaders(hdrs, 1);
  server.on("/update", HTTP_POST, onUploadDone, onUpload);
  server.on("/api/otapin", onPin);
  startArduinoOta();
}

void otaLoop(bool healthy) {
#ifdef OTA_TEST_NO_CONFIRM
  healthy = false;   // test build: never confirms, so the rollback can be seen working
#endif
  if (arduinoOtaOn && !(busyFn && busyFn())) ArduinoOTA.handle();   // never during a register write
  if (rebootAt && millis() > rebootAt) ESP.restart();
  if (!pendingVerify) return;
  if (healthy) {
    esp_ota_mark_app_valid_cancel_rollback();
    pendingVerify = false;
    String from = nv->getString("otaFrom", "");
    logf(1, "Firmware update confirmed: %s -> %s", from.length() ? from.c_str() : "?", GC_VERSION);
    nv->remove("otaFrom");
  } else if (millis() > 120000) {
    logf(3, "New firmware %s not confirmed in 2 minutes: rolling back", GC_VERSION);
    delay(500);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

void otaStatus(String& j) {
  const esp_partition_t* run = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  j += ",\"fw\":{\"version\":\""; j += GC_VERSION; j += "\",\"built\":\""; j += __DATE__ " " __TIME__;
  j += "\",\"board\":\""; j += BOARD; j += "\",\"slot\":\""; j += run ? run->label : "?";
  j += "\",\"slotKB\":"; j += next ? (int)(next->size / 1024) : 0;
  j += ",\"pin\":"; j += pin[0] ? "true" : "false";
  j += ",\"pending\":"; j += pendingVerify ? "true" : "false"; j += "}";
}
