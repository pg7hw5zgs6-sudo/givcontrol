// GivControl: local controller for GivEnergy inverters.
// Builds: HAS_DISPLAY=1 for the ESP32-2432S028 "Cheap Yellow Display" (screen, touch, SD);
// HAS_DISPLAY=0 for a bare board such as an ESP32-C6 dev board (web page only, RGB status LED).
// Reads the inverter every minute, serves a web page, and changes a small whitelist of
// verified settings on request.
//
// Writes go through a two-step plan -> confirm, run one register at a time, are read back,
// and are logged (flash, plus SD if fitted). Controls are locked until unlocked in Settings.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SD.h>
#include <SPIFFS.h>
#include "giv.h"
#include "web_index.h"
#include "history.h"
#include "ota.h"
#ifndef MDNS_NAME
#define MDNS_NAME "givcontrol"
#endif
#ifndef HAS_DISPLAY
#define HAS_DISPLAY 1
#endif
#if HAS_DISPLAY
#include <TFT_eSPI.h>
#include "fonts.h"
#endif

// ---------------------------------------------------------------- hardware
#if HAS_DISPLAY   // CYD
static const int LED_R = 4, LED_G = 16, LED_B = 17;          // active LOW
static const int T_CLK = 25, T_MOSI = 32, T_MISO = 39, T_CS = 33, T_IRQ = 36;  // XPT2046 touch
static const int SD_CS = 5;
TFT_eSPI tft;
#else             // bare board: one addressable RGB LED (GPIO8 on Espressif's C6 DevKits)
#ifndef RGB_LED_PIN
#define RGB_LED_PIN 8
#endif
#endif

WebServer server(80);
Preferences prefs;

// ---------------------------------------------------------------- settings
static char host[32] = "";                // dongle address: "" until found or entered
static char tz[48] = "GMT0BST,M3.5.0/1,M10.5.0";   // POSIX time zone, UK by default (Settings)
static bool controlsOn = false;          // writes allowed at all
static bool invertTft = false;           // correct for the ST7789 driver (photo-checked); Settings flips it
static const uint32_t POLL_MS = 60000;
static const char* LOG_FILE = "/changes.csv";

// ---------------------------------------------------------------- state
static char savedSerial[11] = "", savedDongle[11] = "";   // last good serial for the dongle in use (NVS "s_<dongle>")
enum { D_IDLE, D_RUNNING, D_DONE };
static volatile int discState = D_IDLE, discProgress = 0;
static Found discFound[6];
static volatile int discCount = 0;
static uint32_t discLast = 0;
static bool everAnswered = false;
static Reading last;
static bool haveReading = false, sdOk = false, fsOk = false;
static int fails = 0;
static uint32_t lastPoll = 0;
static int screen = 0;                   // S_HOME, S_SCHED, S_CONTROL, S_EVENTS, S_FIND, S_TEST, S_CAL, S_CONFIRM
static int zeroStatus = 0;
static bool tripped = false, prevSerialOk = true, commsDown = false;

struct Ev { time_t ts; uint8_t sev; char txt[96]; };          // sev: 0 info 1 ok 2 warn 3 fault
static Ev events[40];
static int evCount = 0, evHead = 0;

struct Hist { time_t ts; int16_t soc, w, grid, load; };
static Hist hist[288];                                         // 24 h at 5-minute resolution
static int hCount = 0, hHead = 0;
static time_t lastHist = 0;

// ---------------------------------------------------------------- plans and jobs
// A plan is a list of register writes waiting for confirmation. Confirming turns it into
// the job, which loop() runs one register at a time.
enum Kind { K_SET, K_HOLD, K_RESUME, K_CHARGENOW, K_RESTORE, K_CLOCK, K_SERIAL };
struct Step { uint16_t reg, value; int old; };
struct Plan {
  uint32_t id = 0; uint32_t made = 0; Kind kind = K_SET; char title[64] = "";
  Step steps[16]; int n = 0;
};
static Plan pending;                     // waiting for confirm (id 0 = none)
static Plan job;                         // running (id 0 = none)
static int jobStep = 0, jobOk = 0;
static char jobResult[160] = "";
static uint32_t nextId = 1;
static const uint32_t PLAN_TTL = 120000;

// ---------------------------------------------------------------- helpers
#if HAS_DISPLAY
static void led(bool r, bool g, bool b) { digitalWrite(LED_R, !r); digitalWrite(LED_G, !g); digitalWrite(LED_B, !b); }
#else
static void led(bool r, bool g, bool b) { rgbLedWrite(RGB_LED_PIN, r ? 40 : 0, g ? 40 : 0, b ? 40 : 0); }   // dim
#endif

static String hhmm(time_t t) {
  if (t < 100000) return "--:--";
  struct tm lt; localtime_r(&t, &lt); char s[8]; strftime(s, sizeof s, "%H:%M", &lt); return s;
}

static String hm(int v) {               // HHMM register value -> "HH:MM"
  if (v < 0) return "--:--";
  char s[8]; snprintf(s, sizeof s, "%02d:%02d", v / 100, v % 100); return s;
}

static int nowHHMM() { time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt); return lt.tm_hour * 100 + lt.tm_min; }

// charge/discharge limit as a label: ~166 W per step up to ~5.7 kW was measured on an AIO; other
// models show the raw step (0-50)
static String kw(int limit) {
  if (limit < 0) return "-";
  if (last.inv.model != M_AIO) return String(limit) + "/50";
  return String(min(limit * 166, 5700) / 1000.0, 1) + " kW";
}

static bool fresh() { return haveReading && last.inv.hr60 && time(nullptr) - last.ts < 300; }

static void appendLog(const char* line) {
  char d[24]; time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt); strftime(d, sizeof d, "%Y-%m-%d %H:%M:%S", &lt);
  if (fsOk) {
    if (SPIFFS.exists(LOG_FILE)) {
      File f = SPIFFS.open(LOG_FILE, FILE_READ);
      size_t sz = f ? f.size() : 0; if (f) f.close();
      if (sz > 12 * 1024) { SPIFFS.remove("/changes.old.csv"); SPIFFS.rename(LOG_FILE, "/changes.old.csv"); }   // flash is shared with history
    }
    File f = SPIFFS.open(LOG_FILE, FILE_APPEND);
    if (f) { f.printf("%s,%s\n", d, line); f.close(); }
  }
  if (sdOk) {
    File f = SD.open("/givcontrol/changes.csv", FILE_APPEND);
    if (f) { f.printf("%s,%s\n", d, line); f.close(); }
  }
}

static void addEvent(uint8_t sev, const char* fmt, ...) {
  Ev& e = events[evHead];
  e.ts = time(nullptr); e.sev = sev;
  va_list ap; va_start(ap, fmt); vsnprintf(e.txt, sizeof e.txt, fmt, ap); va_end(ap);
  evHead = (evHead + 1) % 40; if (evCount < 40) evCount++;
  Serial.printf("[event] %s\n", e.txt);
  if (fsOk) {
    File f = SPIFFS.open("/ev.log", FILE_READ);
    size_t sz = f ? f.size() : 0; if (f) f.close();
    if (sz > 6 * 1024) { SPIFFS.remove("/ev.old"); SPIFFS.rename("/ev.log", "/ev.old"); }   // ~200 events across both
    f = SPIFFS.open("/ev.log", FILE_APPEND);
    if (f) { f.printf("%ld,%d,%s\n", (long)e.ts, sev, e.txt); f.close(); }
  }
  if (sdOk) {
    File f = SD.open("/givcontrol/events.csv", FILE_APPEND);
    if (f) { struct tm lt; localtime_r(&e.ts, &lt); char d[24]; strftime(d, sizeof d, "%Y-%m-%d %H:%M:%S", &lt);
             f.printf("%s,%d,\"%s\"\n", d, sev, e.txt); f.close(); }
  }
}

// Put the newest saved events back in the RAM list after a restart.
static void loadEvents() {
  for (const char* name : {"/ev.old", "/ev.log"}) {
    File f = SPIFFS.open(name, FILE_READ);
    if (!f) continue;
    while (f.available()) {
      String l = f.readStringUntil('\n');
      int a = l.indexOf(','), b = l.indexOf(',', a + 1);
      if (a < 1 || b < 0) continue;
      Ev& e = events[evHead];
      e.ts = l.substring(0, a).toInt(); e.sev = l.substring(a + 1, b).toInt();
      strlcpy(e.txt, l.substring(b + 1).c_str(), sizeof e.txt);
      evHead = (evHead + 1) % 40; if (evCount < 40) evCount++;
    }
    f.close();
  }
}

// inverter clock minus real time, in seconds (INT32_MIN if unknown)
static long clockDrift(const Reading& r) {
  if (!r.inv.valid || r.inv.clock[0] < 0 || r.ts < 1700000000) return INT32_MIN;
  struct tm t = {};
  t.tm_year = r.inv.clock[0] + 100; t.tm_mon = r.inv.clock[1] - 1; t.tm_mday = r.inv.clock[2];
  t.tm_hour = r.inv.clock[3]; t.tm_min = r.inv.clock[4]; t.tm_sec = r.inv.clock[5]; t.tm_isdst = -1;
  return (long)(mktime(&t) - r.ts);
}

// ---------------------------------------------------------------- detection
static void detect(const Reading& r) {
  bool stopped = r.inv.status == 0 && abs(r.inv.batteryW) < 50 && r.inv.soc >= 0 && r.inv.soc < 99;
  zeroStatus = stopped ? zeroStatus + 1 : 0;
  if (!tripped && zeroStatus >= 2) { tripped = true; addEvent(3, "Battery stopped at %d%%", r.inv.soc); }
  else if (tripped && zeroStatus == 0) { tripped = false; addEvent(1, "Battery running again"); }
  if (r.inv.valid && r.inv.serialOk != prevSerialOk) {
    addEvent(r.inv.serialOk ? 1 : 2, "Serial now %s", r.inv.serial); prevSerialOk = r.inv.serialOk;
  }
}

// ---------------------------------------------------------------- SD reading log
static void logReading(const Reading& r) {
  if (!sdOk) return;
  struct tm lt; localtime_r(&r.ts, &lt);
  char path[48]; strftime(path, sizeof path, "/givcontrol/%Y-%m-%d.csv", &lt);
  bool isNew = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) return;
  if (isNew) f.println("time,soc,battery_w,grid_w,load_w,charge_limit,discharge_limit,reserve,ac_charge,slot_start,slot_end,status");
  char t[12]; strftime(t, sizeof t, "%H:%M:%S", &lt);
  f.printf("%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", t, r.inv.soc, r.inv.batteryW, r.inv.gridW, r.inv.loadW, r.inv.chargeLimit,
           r.inv.dischargeLimit, r.inv.reserve, r.inv.acCharge, r.inv.slotStart, r.inv.slotEnd, r.inv.status);
  f.close();
}

// ---------------------------------------------------------------- plans
static int curValue(uint16_t reg) {
  const Inverter& i = last.inv;
  switch (reg) {
    case 56: case 57: return last.at(reg);
    case 94: return i.slotStart; case 95: return i.slotEnd; case 96: return i.acCharge;
    case 27: return last.at(27);
    case 111: return i.chargeLimit; case 112: return i.dischargeLimit; case 114: return i.reserve;
  }
  if (reg >= 35 && reg <= 40) return i.clock[reg - 35];
  if (reg >= 13 && reg <= 17) { const char* s = i.serial; int k = reg - 13; return ((uint8_t)s[2 * k] << 8) | (uint8_t)s[2 * k + 1]; }
  return givTestGroupOf(reg) >= 0 ? last.at(reg) : -1;   // test-mode candidates
}

static String fmtValue(uint16_t reg, int v) {
  if (v < 0) return "?";
  if (givIsTime(reg)) return hm(v);
  if (reg == 96 || reg == 27 || reg == 59 || reg == 20) return v ? "on" : "off";
  if (reg == 116 || reg == 242 || (reg >= 244 && reg <= 299 && (reg - 240) % 3 == 2)) return String(v) + "%";
  if (reg == 26) return String(v) + " W";
  if (reg == 318) { static const char* m[4] = {"off", "pause charge", "pause discharge", "pause both"}; return v <= 3 ? m[v] : String(v); }
  if (reg == 111 || reg == 112) return String(v) + " (~" + kw(v) + ")";
  if (reg == 114) return String(v) + "%";
  if (reg >= 13 && reg <= 17) { char s[3] = {(char)(v >> 8), (char)(v & 0xFF), 0}; return s; }
  return String(v);
}

static bool addStep(Plan& p, uint16_t reg, uint16_t value, String& err) {
  const char* why = givAllowed(reg, value);
  if (why) { err = "HR" + String(reg) + ": " + why; return false; }
  if (p.n >= 16) { err = "too many changes at once"; return false; }
  p.steps[p.n++] = {reg, value, curValue(reg)};
  return true;
}

// load the saved serial for this dongle into savedSerial/savedDongle
static void loadSaved(const char* dongle) {
  if (!strcmp(dongle, savedDongle)) return;
  strlcpy(savedDongle, dongle, sizeof savedDongle); savedSerial[0] = 0;
  if (dongle[0]) prefs.getString((String("s_") + dongle).c_str(), savedSerial, sizeof savedSerial);
}

static const char* restoreSerial() {
  const Inverter& i = last.inv;
  if (!i.valid || i.serialOk || strlen(savedSerial) != 10) return nullptr;
  if (!i.dongle[0] || strcmp(i.dongle, savedDongle) != 0) return nullptr;
  return savedSerial;
}

// Build a plan from a request. Returns "" on success or the reason it was refused.
static String makePlan(Plan& p, Kind kind, int a, int b) {
  p = Plan(); p.kind = kind;
  if (!controlsOn) return "Controls are locked (Settings)";
  if (job.id) return "Another change is still running";
  if (!fresh()) return "No recent reading from the inverter";
  const Inverter& i = last.inv;
  if (!givModelVerified(i.model)) return "Controls aren't verified for the " + givModelName(i.model, i.arm) + " yet";
  String err;
  switch (kind) {
    case K_HOLD:
      if (prefs.getBool("hold", false)) return "Battery is already held";
      if (i.soc < 0 || i.reserve < 0 || i.acCharge < 0) return "No battery reading";
      strcpy(p.title, "Pause the battery at its current level");
      if (!addStep(p, 114, constrain(i.soc, 4, 100), err) || !addStep(p, 96, 0, err)) return err;
      break;
    case K_RESUME:
      if (!prefs.getBool("hold", false)) return "Battery is not held";
      strcpy(p.title, "Resume normal battery use");
      if (!addStep(p, 114, prefs.getUShort("hRes", 4), err) || !addStep(p, 96, prefs.getUShort("hAc", 1), err)) return err;
      break;
    case K_CHARGENOW: {
      if (prefs.getBool("hold", false)) return "Resume the battery first";
      if (prefs.getBool("cnow", false)) return "Charge now is already set: restore the schedule first";
      int start = nowHHMM();
      if (a < 0 || a == start) return "Pick an end time";
      snprintf(p.title, sizeof p.title, "Charge from the grid now until %s", hm(a).c_str());
      if (!addStep(p, 94, start, err) || !addStep(p, 95, a, err) || !addStep(p, 96, 1, err)) return err;
      break;
    }
    case K_RESTORE:
      if (!prefs.getBool("cnow", false)) return "Nothing to restore";
      strcpy(p.title, "Restore the charge schedule");
      if (!addStep(p, 94, prefs.getUShort("cS"), err) || !addStep(p, 95, prefs.getUShort("cE"), err) ||
          !addStep(p, 96, prefs.getUShort("cAc"), err)) return err;
      break;
    case K_CLOCK:
      if (time(nullptr) < 1700000000) return "This device's clock isn't set yet";
      strcpy(p.title, "Set the inverter clock from internet time");
      for (int r = 40; r >= 35; r--) if (!addStep(p, r, r == 35 ? 24 : r == 36 || r == 37 ? 1 : 0, err)) return err;  // values filled in when run
      break;
    case K_SERIAL: {
      const char* s = restoreSerial();
      if (!s) return "The serial reads fine, or there is no saved serial to restore";
      snprintf(p.title, sizeof p.title, "Restore the inverter serial to %s", s);
      for (int k = 0; k < 5; k++) if (!addStep(p, 13 + k, ((uint8_t)s[2 * k] << 8) | (uint8_t)s[2 * k + 1], err)) return err;
      break;
    }
    case K_SET: break;   // filled in by the caller
  }
  return "";
}

// Plan only the settings that differ from what the inverter has now.
static String planSet(Plan& p, const uint16_t* regs, const int* vals, int n) {
  String err = makePlan(p, K_SET, -1, -1);
  if (err.length()) return err;
  strcpy(p.title, "Change battery settings");
  for (int k = 0; k < n; k++) {
    if (vals[k] == curValue(regs[k])) continue;
    if (!addStep(p, regs[k], vals[k], err)) return err;
  }
  return p.n ? "" : "Nothing to change";
}

// ---------------------------------------------------------------- untested settings and undo
// Once controls are unlocked, the GivTCP-map settings not yet verified on the AIO are writable too
// (the owner's choice), still only within their ranges; HR163 and anything unlisted stay blocked.
// "Undo last change" puts back the values the last successful change replaced.
static uint8_t candStat[29];   // all "untested"
static void candApply() { givSetTest(true, candStat); }

static struct { uint16_t reg[16]; int val[16]; int n; } undo = {};

static void startJob() {
  job = pending; pending = Plan();
  jobStep = 0; jobOk = 0; jobResult[0] = 0;
  // remember what to go back to before anything is changed
  if (job.kind == K_HOLD) { prefs.putBool("hold", true); prefs.putUShort("hRes", last.inv.reserve); prefs.putUShort("hAc", last.inv.acCharge); }
  if (job.kind == K_CHARGENOW) { prefs.putBool("cnow", true); prefs.putUShort("cS", last.inv.slotStart);
                                 prefs.putUShort("cE", last.inv.slotEnd); prefs.putUShort("cAc", last.inv.acCharge); }
  addEvent(0, "Started: %s", job.title);
  char l[96]; snprintf(l, sizeof l, "start,\"%s\"", job.title); appendLog(l);
}

static void redraw();
static void requestPoll();
static void setHost(const char* h, const char* why);

// Run one step of the job (called from loop; blocks a few seconds).
static void runJobStep() {
  if (!job.id) return;
  if (job.kind == K_CLOCK && jobStep == 0) {
    // seconds are written first: start early in the minute so nothing rolls over mid-way
    struct tm lt; time_t t = time(nullptr); localtime_r(&t, &lt);
    if (lt.tm_sec > 35) return;
  }
  Step& s = job.steps[jobStep];
  if (job.kind == K_CLOCK) {
    struct tm lt; time_t t = time(nullptr); localtime_r(&t, &lt);
    int v[6] = {lt.tm_year - 100, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec};
    s.value = v[s.reg - 35];
  }
  led(1, 0, 1);
  int got; String msg;
  bool ok = givWrite(host, s.reg, s.value, got, msg);
  jobOk += ok;
  char l[160];
  snprintf(l, sizeof l, "HR%u,%s,%s,%s,%s,%s", s.reg, givRegName(s.reg), fmtValue(s.reg, s.old).c_str(),
           fmtValue(s.reg, s.value).c_str(), ok ? "ok" : "FAILED", msg.c_str());
  appendLog(l);
  addEvent(ok ? 1 : 3, "%s %s -> %s: %s", givRegName(s.reg), fmtValue(s.reg, s.old).c_str(), fmtValue(s.reg, s.value).c_str(), ok ? "ok" : msg.c_str());
  jobStep++;
  // stop at the first failure: later steps may not make sense without it
  if (!ok || jobStep >= job.n) {
    bool all = ok && jobOk == job.n;
    snprintf(jobResult, sizeof jobResult, "%s: %s (%d of %d changes confirmed)", job.title, all ? "done" : "FAILED", jobOk, job.n);
    if (job.kind == K_SET && jobOk) {   // the first jobOk steps were written
      undo.n = 0;
      for (int k = 0; k < jobOk; k++) if (job.steps[k].old >= 0) { undo.reg[undo.n] = job.steps[k].reg; undo.val[undo.n++] = job.steps[k].old; }
    }
    if (strstr(job.title, "Undo") && all) undo.n = 0;
    if (all && job.kind == K_RESUME) prefs.putBool("hold", false);
    if (all && job.kind == K_RESTORE) prefs.putBool("cnow", false);
    addEvent(all ? 1 : 3, "%s", jobResult);
    snprintf(l, sizeof l, "end,\"%s\"", jobResult); appendLog(l);
    job = Plan();
    requestPoll();
  }
}

// ---------------------------------------------------------------- GivTCP settings not verified on the AIO
// Shown with their live values; never written until each one has been verified on this unit.
enum CandKind { CK_ONOFF, CK_HHMM, CK_PCT, CK_W, CK_RAW };
struct Cand { uint16_t reg; CandKind kind; const char* name; };
static const Cand CANDS[] = {
  {59, CK_ONOFF, "Timed discharge / export"},
  {44, CK_HHMM, "Discharge slot 2 start"}, {45, CK_HHMM, "Discharge slot 2 end"},
  {31, CK_HHMM, "Charge slot 2 start"}, {32, CK_HHMM, "Charge slot 2 end"},
  {116, CK_PCT, "Charge target"},
  {242, CK_PCT, "Charge slot 1 target"},
  {20, CK_ONOFF, "Charge target enable"},
  {26, CK_W, "Export limit (grid port max)"},
  {318, CK_RAW, "Pause battery mode"},
  {319, CK_HHMM, "Pause slot start"}, {320, CK_HHMM, "Pause slot end"},
};

// Solar: shown once real generation has been seen on the PV inputs (remembered, so the tile
// doesn't vanish at night), or always / never by choice in Settings.
enum { SOLAR_AUTO, SOLAR_SHOW, SOLAR_HIDE };
static int solarMode = SOLAR_AUTO;
static bool pvSeen = false;
static bool solarShown() { return solarMode == SOLAR_SHOW || (solarMode == SOLAR_AUTO && pvSeen); }

// mode from HR27 (eco) and HR59 (timed discharge)
static String modeName(const Reading& r) {
  int eco = r.at(27), timed = r.at(59);
  if (eco < 0) return "?";
  if (timed == 1) return eco == 1 ? "Timed demand" : "Timed export";
  return eco == 1 ? "Eco" : "Timed";
}

static void updateLed() {
  if (job.id) led(1, 0, 1);
  else if (WiFi.status() != WL_CONNECTED) led(0, 0, 1);
  else if (tripped || last.inv.fault) led(1, 0, 0);
  else led(0, 1, 0);
}

#if HAS_DISPLAY
// ---------------------------------------------------------------- screen
// Dark, high-contrast palette with anti-aliased Noto Sans. One accent (blue) for things you
// can press; green = charging / OK, amber = discharging, red = fault / locked.
#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
static const uint16_t C_BG = RGB(14, 18, 23), C_CARD = RGB(36, 38, 42), C_CARD2 = RGB(50, 53, 58), C_LINE = RGB(62, 66, 72);   // neutral charcoal cards
static const uint16_t C_TXT = RGB(232, 236, 241), C_MUT = RGB(170, 180, 192), C_DIM = RGB(130, 140, 154), C_ACC = RGB(59, 158, 255);
static const uint16_t C_TAB = RGB(205, 212, 222);   // inactive tabs: only slightly dimmer than the active one
static const uint16_t C_GOOD = RGB(52, 199, 89), C_WARN = RGB(255, 176, 32), C_BAD = RGB(255, 90, 95);

enum { S_HOME, S_SCHED, S_CONTROL, S_EVENTS, S_FIND = 5, S_TEST = 6, S_CAL = 8, S_CONFIRM = 9 };
static const char* TABS[] = {"Home", "Schedule", "Control", "Events"};
static const int TAB_Y = 206;

static const uint8_t* curFont = nullptr;
static void useFont(const uint8_t* f) {
  if (f == curFont) return;
  if (curFont) tft.unloadFont();
  tft.loadFont(f); curFont = f;
}

// Draw text. Smooth fonts blend into `bg`, so it must be the fill underneath.
// Every string is placed on a baseline worked out from the font's cap height (Noto: 0.714 em),
// not from each font's own ascent/descent, so Regular and Bold, and different strings, line up:
// T* datums put the cap tops at y, M* datums centre the capitals on y.
static void txt(const String& s, int x, int y, const uint8_t* f, uint16_t c, uint16_t bg, uint8_t datum = TL_DATUM) {
  useFont(f); tft.setTextColor(c, bg, false);
  int cap = (tft.gFont.yAdvance * 714 + 500) / 1000;
  int base = y;
  uint8_t d = datum;
  switch (datum) {
    case TL_DATUM: case TC_DATUM: case TR_DATUM: base = y + cap; d = datum == TL_DATUM ? L_BASELINE : datum == TC_DATUM ? C_BASELINE : R_BASELINE; break;
    case ML_DATUM: case MC_DATUM: case MR_DATUM: base = y + (cap + 1) / 2; d = datum == ML_DATUM ? L_BASELINE : datum == MC_DATUM ? C_BASELINE : R_BASELINE; break;
  }
  tft.setTextDatum(d);
  tft.drawString(s, x, base);
}

static int textW(const String& s, const uint8_t* f) { useFont(f); return tft.textWidth(s); }

// cut a string to fit `w` pixels
static String fit(String s, int w, const uint8_t* f) {
  if (textW(s, f) <= w) return s;
  while (s.length() > 1 && textW(s + "..", f) > w) s.remove(s.length() - 1);
  return s + "..";
}

static void card(int x, int y, int w, int h, uint16_t c = C_CARD) { tft.fillRoundRect(x, y, w, h, 10, c); }

static String kwStr(int w) { return String(abs(w) / 1000.0, abs(w) < 10000 ? 2 : 1) + " kW"; }

// battery state colour: green charging, amber discharging, red stopped, blue idle
static uint16_t batteryColour(int w) { return tripped ? C_BAD : w < -50 ? C_GOOD : w > 50 ? C_WARN : C_ACC; }


static void statusBar() {
  tft.fillRect(0, 0, 320, 30, C_BG);
  txt("GivControl", 12, 15, F_BOLD, C_TXT, C_BG, ML_DATUM);
  txt(hhmm(time(nullptr)), 308, 15, F_BOLD, C_TXT, C_BG, MR_DATUM);
  String chip; uint16_t c;
  if (job.id) { chip = "Writing"; c = C_ACC; }
  else if (WiFi.status() != WL_CONNECTED) { chip = "No Wi-Fi"; c = C_BAD; }
  else if (controlsOn) { chip = "Unlocked"; c = C_WARN; }
  else { chip = "Locked"; c = C_BAD; }
  int w = textW(chip, F_SMALL) + 20, x = 308 - 56 - w;
  tft.fillRoundRect(x, 5, w, 21, 10, C_CARD2);
  txt(chip, x + w / 2, 15, F_SMALL, c, C_CARD2, MC_DATUM);
}

// Four equal boxes, full bar height. The active one is filled and fully contains its label;
// the font is the largest that fits every label with padding, so nothing spills out or shifts.
static void tabs() {
  const int gap = 4, cw = (320 - gap * 5) / 4, y = TAB_Y + 3, h = 240 - TAB_Y - 6;
  static const uint8_t* font = nullptr;
  if (!font) {
    const uint8_t* order[3] = {F_BOLD, F_TAB, F_SMALL};   // largest first
    for (const uint8_t* f : order) {
      bool fits = true;
      for (int k = 0; k < 4; k++) if (textW(TABS[k], f) > cw - 16) fits = false;
      font = f;
      if (fits) break;
    }
  }
  tft.fillRect(0, TAB_Y, 320, 240 - TAB_Y, C_BG);
  for (int k = 0; k < 4; k++) {
    bool on = k == screen;
    int x = gap + k * (cw + gap);
    uint16_t fill = on ? C_CARD2 : C_BG;
    tft.fillRoundRect(x, y, cw, h, 8, fill);
    if (on) tft.fillRoundRect(x + cw / 2 - 12, y + h - 3, 24, 3, 1, C_ACC);   // small accent mark inside the box
    txt(TABS[k], x + cw / 2, y + h / 2 - 1, font, on ? C_TXT : C_TAB, fill, MC_DATUM);
  }
}

// power-flow row: one short label on the left, the value on the right (state shown by colour)
static void flowRow(int y, const char* label, const String& value, uint16_t vc, uint16_t lc = C_MUT) {
  const int x = 158, w = 156, h = 39, pad = 14;
  card(x, y, w, h);
  txt(label, x + pad, y + h / 2, F_BODY, lc, C_CARD, ML_DATUM);
  txt(fit(value, w - 2 * pad - textW(label, F_BODY) - 8, F_BOLD), x + w - pad, y + h / 2, F_BOLD, vc, C_CARD, MR_DATUM);
}

static void drawDashboard() {
  if (!haveReading) { txt("Waiting for the inverter...", 160, 115, F_BODY, C_MUT, C_BG, MC_DATUM); return; }
  const Inverter& i = last.inv;
  if (!givSinglePhase(i.model)) {   // these readings come from the single-phase map
    card(6, 36, 308, 166);
    txt(givModelName(i.model, i.arm), 160, 90, F_MID, C_TXT, C_CARD, MC_DATUM);
    txt("Readings for this model aren't supported yet", 160, 122, F_SMALL, C_MUT, C_CARD, MC_DATUM);
    txt("Controls stay read only", 160, 142, F_SMALL, C_MUT, C_CARD, MC_DATUM);
    return;
  }
  // battery: ring, big SOC with % beside it, state and power underneath
  card(6, 36, 146, 166);
  int cx = 79, cy = 100, soc = constrain(i.soc, 0, 100), w = i.batteryW;
  uint16_t bc = batteryColour(w);
  tft.drawSmoothArc(cx, cy, 58, 49, 30, 330, C_LINE, C_CARD, true);
  if (soc > 0) tft.drawSmoothArc(cx, cy, 58, 49, 30, 30 + 300 * soc / 100, bc, C_CARD, true);
  String n = String(i.soc);
  int nw = textW(n, F_BIG), pw = textW("%", F_MID), x0 = cx - (nw + 2 + pw) / 2;
  txt(n, x0, cy - 4, F_BIG, C_TXT, C_CARD, ML_DATUM);
  txt("%", x0 + nw + 2, cy - 10, F_MID, C_MUT, C_CARD, ML_DATUM);
  txt("battery", cx, cy + 26, F_SMALL, C_MUT, C_CARD, MC_DATUM);
  String state = tripped ? "Stopped" : w < -50 ? "Charging" : w > 50 ? "Discharging" : "Idle";
  txt(state, cx, 170, F_BOLD, bc, C_CARD, MC_DATUM);
  txt(abs(w) > 50 ? kwStr(w) : String(w) + " W", cx, 189, F_SMALL, C_TXT, C_CARD, MC_DATUM);
  // power flow (solar is not measured by the AIO and the estimate isn't reliable yet)
  int g = i.gridW;
  flowRow(36, "Solar", "-", C_DIM);
  if (g < -30) flowRow(78, "Import", kwStr(g), C_WARN, C_WARN);        // from the grid
  else if (g > 30) flowRow(78, "Export", kwStr(g), C_GOOD, C_GOOD);    // to the grid
  else flowRow(78, "Grid", "0 kW", C_TXT);
  flowRow(120, "Home", kwStr(i.loadW), C_TXT);
  static const char* SHORT[] = {"Inverter", "Hybrid", "AC", "Hybrid 3ph", "EMS", "AC 3ph", "Gateway", "AIO"};
  const char* nm = SHORT[i.model];
  if (i.fault) flowRow(162, nm, "Fault " + String(i.fault, HEX), C_BAD);
  else flowRow(162, nm, i.status == 1 ? "Normal" : i.status == 0 ? "Stopped" : "?", i.status == 1 ? C_GOOD : C_BAD);
}

// Schedule: read-only summary of what is actually scheduled (slots are edited on the web page).
struct Slot { int start, end; bool charge; bool active; int target; };

static int mins(int hhmm) { return hhmm / 100 * 60 + hhmm % 100; }
// 00:00-00:00, 00:01-00:00 and the like mean "no slot"
static bool slotEmpty(int a, int b) { return a < 0 || b < 0 || abs(mins(a) - mins(b)) <= 1; }

static int collectSlots(const Reading& r, Slot* out, int max) {
  int n = 0;
  auto add = [&](int a, int b, bool charge, bool active, int target) {
    if (n < max && !slotEmpty(a, b)) out[n++] = {a, b, charge, active, target};
  };
  const Inverter& i = r.inv;
  bool ac = i.acCharge == 1, timed = r.at(59) == 1;
  add(i.slotStart, i.slotEnd, true, ac, r.at(116));
  add(r.at(31), r.at(32), true, ac, -1);
  for (int k = 0; k < 10; k++) add(r.at(240 + 3 * k), r.at(241 + 3 * k), true, ac, r.at(242 + 3 * k));
  add(r.at(56), r.at(57), false, timed, -1);
  add(r.at(44), r.at(45), false, timed, -1);
  for (int k = 0; k < 10; k++) add(r.at(270 + 3 * k), r.at(271 + 3 * k), false, timed, -1);
  return n;
}

// 24-hour bar: charge windows green, discharge amber, white marker at the current time
static void dayBar(int y, const Slot* sl, int n) {
  const int x0 = 14, w = 292, h = 10;
  tft.fillRoundRect(x0, y, w, h, 5, C_CARD2);
  for (int k = 0; k < n; k++) {
    if (!sl[k].active) continue;
    uint16_t c = sl[k].charge ? C_GOOD : C_WARN;
    int a = mins(sl[k].start), b = mins(sl[k].end);
    auto seg = [&](int from, int to) { int xa = x0 + w * from / 1440, xb = x0 + w * to / 1440; if (xb > xa) tft.fillRect(xa, y, xb - xa, h, c); };
    if (a < b) seg(a, b); else { seg(a, 1440); seg(0, b); }   // over midnight
  }
  int now = mins(nowHHMM()), xn = x0 + w * now / 1440;
  tft.fillRect(xn - 1, y - 4, 3, h + 8, C_TXT);
  const char* lab[5] = {"00", "06", "12", "18", "24"};
  for (int k = 0; k < 5; k++) txt(lab[k], x0 + w * k / 4, y + h + 9, F_SMALL, C_DIM, C_BG, k == 0 ? ML_DATUM : k == 4 ? MR_DATUM : MC_DATUM);
}

static void drawSchedule() {
  if (!haveReading) { txt("Waiting for the inverter...", 160, 115, F_BODY, C_MUT, C_BG, MC_DATUM); return; }
  Slot sl[24];
  int n = collectSlots(last, sl, 24);
  dayBar(42, sl, n);
  int y = 74;
  if (!n) {
    card(6, y, 308, 70);
    txt("No slots scheduled", 160, y + 24, F_BOLD, C_TXT, C_CARD, MC_DATUM);
    txt("edit on the web page (givcontrol.local)", 160, y + 48, F_SMALL, C_MUT, C_CARD, MC_DATUM);
  }
  int shown = 0;
  for (int k = 0; k < n && shown < 3; k++, shown++, y += 38) {
    const Slot& s = sl[k];
    uint16_t dot = !s.active ? C_DIM : s.charge ? C_GOOD : C_WARN;
    card(6, y, 308, 34);
    tft.fillCircle(20, y + 17, 4, dot);
    String what = s.charge ? "Charge" : "Discharge";
    if (!s.active) what += " (off)";
    txt(what, 32, y + 17, F_BODY, s.active ? C_TXT : C_MUT, C_CARD, ML_DATUM);
    String right = hm(s.start) + " - " + hm(s.end);
    if (s.charge && s.target > 0) right += "  to " + String(s.target) + "%";
    txt(right, 302, y + 17, F_BOLD, s.active ? C_TXT : C_MUT, C_CARD, MR_DATUM);
  }
  if (n > shown) { txt("+" + String(n - shown) + " more on the web page", 160, y + 6, F_SMALL, C_MUT, C_BG, MC_DATUM); y += 16; }
  if (!n) y += 76;
  // settings that shape the schedule, read only (edit on the web page)
  const Inverter& i = last.inv;
  int eco = last.at(27);
  struct { const char* k; String v; uint16_t c; } rows[4] = {
    {"Mode", modeName(last), (uint16_t)(eco == 1 ? C_TXT : C_WARN)},
    {"Reserve", String(i.reserve) + "%", C_TXT},
    {"Charge target", last.at(116) < 0 ? String("-") : String(last.at(116)) + "%", C_TXT},
    {"Rates", kw(i.chargeLimit) + " / " + kw(i.dischargeLimit), C_TXT},
  };
  int rh = 24, nr = min(4, (200 - y) / rh);
  if (nr > 0) {
    card(6, y, 308, nr * rh + 8);
    for (int k = 0; k < nr; k++) {
      int ry = y + 4 + k * rh + rh / 2;
      txt(rows[k].k, 20, ry, F_BODY, C_MUT, C_CARD, ML_DATUM);
      txt(rows[k].v, 300, ry, F_BOLD, rows[k].c, C_CARD, MR_DATUM);
    }
  }
}

struct Btn { int x, y, w, h; };

static void pill(const Btn& b, const String& label, uint16_t fill, uint16_t ink, const uint8_t* f = F_BOLD) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 12, fill);
  txt(label, b.x + b.w / 2, b.y + b.h / 2, f, ink, fill, MC_DATUM);
}

// Controls screen buttons (2 x 2). Each only opens a confirm screen.
static const Btn CBTN[4] = {{6, 92, 151, 54}, {163, 92, 151, 54}, {6, 150, 151, 54}, {163, 150, 151, 54}};

// enabled: raised fill with an accent bar; disabled: outline only, dim text, so it can't look tappable
static void button(const Btn& b, const String& title, const String& sub, bool enabled) {
  uint16_t bg = enabled ? C_CARD2 : C_BG;
  if (enabled) { card(b.x, b.y, b.w, b.h, bg); tft.fillRoundRect(b.x, b.y + 12, 3, b.h - 24, 1, C_ACC); }
  else { tft.fillRoundRect(b.x, b.y, b.w, b.h, 10, C_BG); tft.drawRoundRect(b.x, b.y, b.w, b.h, 10, C_LINE); }
  txt(title, b.x + 16, b.y + 19, F_BOLD, enabled ? C_TXT : C_DIM, bg, ML_DATUM);
  txt(fit(sub, b.w - 30, F_SMALL), b.x + 16, b.y + 39, F_SMALL, enabled ? C_MUT : C_DIM, bg, ML_DATUM);
}

static int chargeNowUntil() { int e = (nowHHMM() / 100 + 2) % 24 * 100 + nowHHMM() % 100; return e; }

static void drawControls() {
  if (!haveReading) { txt("Waiting for the inverter...", 160, 115, F_BODY, C_MUT, C_BG, MC_DATUM); return; }
  const Inverter& i = last.inv;
  card(6, 36, 308, 50);
  txt("Charge " + kw(i.chargeLimit) + "    Discharge " + kw(i.dischargeLimit), 20, 52, F_BODY, C_TXT, C_CARD, ML_DATUM);
  String note; uint16_t nc = C_MUT;
  if (!controlsOn) { note = "Locked: unlock on the web page"; nc = C_BAD; }
  else if (job.id) { note = String("Writing: ") + job.title; nc = C_ACC; }
  else if (jobResult[0]) { note = jobResult; nc = strstr(jobResult, "FAILED") ? C_BAD : C_GOOD; }
  else note = "Reserve " + String(i.reserve) + "%   AC charge " + (i.acCharge == 1 ? "on" : "off");
  txt(fit(note, 280, F_SMALL), 20, 72, F_SMALL, nc, C_CARD, ML_DATUM);
  bool en = controlsOn && !job.id && fresh();
  bool held = prefs.getBool("hold", false), cnow = prefs.getBool("cnow", false);
  long d = clockDrift(last);
  button(CBTN[0], held ? "Resume" : "Pause battery", held ? "back to normal use" : "hold at " + String(i.soc) + "%", en);
  button(CBTN[1], cnow ? "Restore slot" : "Charge now", cnow ? "back to the schedule" : "until " + hm(chargeNowUntil()), en && !held);
  button(CBTN[2], "Sync clock", d == INT32_MIN ? String("from internet time") : String(d) + " s off", en);
  button(CBTN[3], "Refresh", "read the inverter", true);
}

static void drawEvents() {
  // where to find this device, and what it is reading
  card(6, 34, 308, 70);
  String ip = WiFi.localIP().toString();
  txt("givcontrol.local", 18, 47, F_BOLD, C_TXT, C_CARD, ML_DATUM);
  txt(ip, 302, 47, F_BOLD, C_ACC, C_CARD, MR_DATUM);
  String wifi = WiFi.status() == WL_CONNECTED ? "Wi-Fi " + WiFi.SSID() + "  " + String(WiFi.RSSI()) + " dBm" : String("Wi-Fi not connected");
  txt(fit(wifi, 284, F_SMALL), 18, 69, F_SMALL, C_MUT, C_CARD, ML_DATUM);
  String dg = host[0] ? "Dongle " + String(host) + (haveReading ? "  read " + hhmm(last.ts) : String("  no reply yet")) : String("Dongle not found yet");
  txt(fit(dg, 284, F_SMALL), 18, 88, F_SMALL, haveReading && !fails ? C_MUT : C_WARN, C_CARD, ML_DATUM);
  int y = 120;
  for (int i = 0; i < evCount && y < 200; i++, y += 21) {
    const Ev& e = events[(evHead - 1 - i + 40) % 40];
    uint16_t c = e.sev == 3 ? C_BAD : e.sev == 2 ? C_WARN : e.sev == 1 ? C_GOOD : C_DIM;
    tft.fillCircle(14, y, 3, c);
    txt(hhmm(e.ts), 24, y, F_SMALL, C_MUT, C_BG, ML_DATUM);
    txt(fit(e.txt, 244, F_SMALL), 68, y, F_SMALL, C_TXT, C_BG, ML_DATUM);
  }
  if (!evCount) txt("No events yet", 160, 150, F_BODY, C_MUT, C_BG, MC_DATUM);
}

// Confirm screen. Confirm sits bottom-centre and Cancel bottom-left; the mirror images of
// both (top-centre, top-left/right, bottom-right) do nothing, so a touch panel that is
// calibrated back to front can never turn "Cancel" into "Confirm".
static const Btn OK_BTN = {110, 184, 100, 48}, NO_BTN = {6, 184, 90, 48};
static uint32_t confirmShown = 0;

static void drawConfirm() {
  tft.fillScreen(C_BG);
  txt("Confirm change", 10, 8, F_SMALL, C_WARN, C_BG);
  txt(fit(pending.title, 300, F_BOLD), 10, 26, F_BOLD, C_TXT, C_BG);
  card(6, 54, 308, 122);
  int y = 62;
  for (int k = 0; k < pending.n && y < 162; k++, y += 19) {
    const Step& s = pending.steps[k];
    String v = pending.kind == K_CLOCK ? String("now") : fmtValue(s.reg, s.value);
    txt(givRegName(s.reg), 16, y, F_SMALL, C_MUT, C_CARD);
    txt(fmtValue(s.reg, s.old) + "  >  " + v, 304, y, F_SMALL, C_TXT, C_CARD, TR_DATUM);
  }
  pill(OK_BTN, "Confirm", RGB(20, 130, 66), C_TXT);
  pill(NO_BTN, "Cancel", C_CARD2, C_TXT);
  confirmShown = millis();
}

// ---------------------------------------------------------------- touch calibration screen
static const int CAL_PTS[4][2] = {{30, 30}, {290, 30}, {290, 210}, {30, 210}};
static int calStep = 0, calRaw[4][2];
static uint32_t calShown = 0;

static void drawCal() {
  tft.fillScreen(C_BG);
  txt("Touch calibration", 160, 92, F_BOLD, C_TXT, C_BG, MC_DATUM);
  txt("Tap the centre of the cross (" + String(calStep + 1) + " of 4)", 160, 118, F_SMALL, C_MUT, C_BG, MC_DATUM);
  txt("use a fingernail or stylus", 160, 138, F_SMALL, C_DIM, C_BG, MC_DATUM);
  int x = CAL_PTS[calStep][0], y = CAL_PTS[calStep][1];
  tft.drawFastHLine(x - 12, y, 25, C_ACC); tft.drawFastVLine(x, y - 12, 25, C_ACC);
  tft.drawCircle(x, y, 6, C_ACC);
  calShown = millis();
}

// Colour test: pure colours plus the theme's own, to check the panel against the web page.
static void drawTest() {
  struct { uint16_t c; const char* name; } sw[8] = {{TFT_RED, "Red"}, {TFT_GREEN, "Green"}, {TFT_BLUE, "Blue"}, {TFT_WHITE, "White"},
    {TFT_BLACK, "Black"}, {C_BG, "Bg"}, {C_CARD, "Card"}, {C_ACC, "Accent"}};
  tft.fillScreen(TFT_BLACK);
  for (int k = 0; k < 8; k++) {
    int x = (k % 4) * 80, y = (k / 4) * 100;
    tft.fillRect(x, y, 80, 100, sw[k].c);
    uint16_t ink = (k == 1 || k == 3 || k == 7) ? TFT_BLACK : TFT_WHITE;   // dark ink on green, white, accent
    txt(sw[k].name, x + 40, y + 50, F_BOLD, ink, sw[k].c, MC_DATUM);
  }
  txt(String("Colour test - inversion ") + (invertTft ? "ON" : "OFF") + " - tap to close", 160, 220, F_SMALL, TFT_WHITE, TFT_BLACK, MC_DATUM);
}

// Dongle discovery: progress while scanning, then the dongles found (tap one to use it).
static const int FIND_Y = 92, FIND_H = 34;

static void drawFind() {
  tft.fillScreen(C_BG);
  txt("Find your GivEnergy dongle", 10, 8, F_BOLD, C_TXT, C_BG);
  IPAddress me = WiFi.localIP();
  String net = String(me[0]) + "." + me[1] + "." + me[2] + ".x";
  if (discState == D_RUNNING) {
    txt("Scanning " + net + "  " + String(discProgress) + " / 254", 10, 40, F_SMALL, C_MUT, C_BG);
    tft.fillRoundRect(10, 62, 300, 8, 4, C_CARD2);
    tft.fillRoundRect(10, 62, max(8, 300 * discProgress / 254), 8, 4, C_ACC);
  } else {
    txt(discCount ? "Tap the one to use:" : "No dongle found on " + net, 10, 40, F_SMALL, C_MUT, C_BG);
    if (!discCount) txt("Enter its address at http://" + me.toString() + "/", 10, 60, F_SMALL, C_MUT, C_BG);
  }
  for (int k = 0; k < discCount && k < 3; k++) {
    int y = FIND_Y + k * (FIND_H + 4);
    bool cur = !strcmp(discFound[k].ip, host);
    tft.fillRoundRect(6, y, 308, FIND_H, 10, cur ? C_CARD2 : C_CARD);
    txt(discFound[k].ip, 16, y + FIND_H / 2, F_BOLD, C_TXT, cur ? C_CARD2 : C_CARD, ML_DATUM);
    txt(givModelName(discFound[k].model, discFound[k].arm) + "  " + discFound[k].serial, 304, y + FIND_H / 2, F_SMALL, C_MUT, cur ? C_CARD2 : C_CARD, MR_DATUM);
  }
  txt("Close", 50, 222, F_BODY, C_MUT, C_BG, MC_DATUM);
}

static void redraw() {
  if (screen == S_FIND) { drawFind(); return; }
  if (screen == S_TEST) { drawTest(); return; }
  if (screen == S_CONFIRM) { drawConfirm(); return; }
  if (screen == S_CAL) { drawCal(); return; }
  tft.fillScreen(C_BG);
  statusBar();
  if (screen == S_HOME) drawDashboard(); else if (screen == S_SCHED) drawSchedule(); else if (screen == S_CONTROL) drawControls(); else drawEvents();
  tabs();
}


// ---------------------------------------------------------------- touch (bit-banged XPT2046)
// A touch is detected from the panel's pressure reading, not the IRQ line, and is mapped to
// the screen with a calibration measured on the device (Settings > Calibrate, or first boot).
struct TouchCal { float ax, bx, ay, by; uint8_t swap, ok; };
static TouchCal tcal = {320.0f / 3500, -200 * 320.0f / 3500, 240.0f / 3560, -240 * 240.0f / 3560, 0, 0};
static const int Z_MIN = 300;
static int tA = -1, tB = -1, tZ = 0, tX = -1, tY = -1, tTemp = -1;   // last tap (diagnostics)
static uint32_t tCount = 0;

static uint16_t xpt(uint8_t cmd) {   // one 12-bit conversion; CS must be low
  for (int b = 7; b >= 0; b--) { digitalWrite(T_MOSI, (cmd >> b) & 1); delayMicroseconds(1); digitalWrite(T_CLK, HIGH); delayMicroseconds(1); digitalWrite(T_CLK, LOW); }
  digitalWrite(T_MOSI, LOW);
  delayMicroseconds(4);
  uint16_t v = 0;
  for (int b = 0; b < 16; b++) { digitalWrite(T_CLK, HIGH); delayMicroseconds(1); v = (v << 1) | digitalRead(T_MISO); digitalWrite(T_CLK, LOW); delayMicroseconds(1); }
  return (v >> 3) & 0xFFF;
}

// raw position (a = channel 0x90, b = channel 0xD0) and pressure
static bool touchRaw(int& a, int& b, int& z) {
  digitalWrite(T_CS, LOW);
  int z1 = xpt(0xB1), z2 = xpt(0xC1);
  z = z1 + 4095 - z2;
  bool down = z1 > 50 && z >= Z_MIN;
  if (down) {
    xpt(0x91);                                   // first sample after switching is noisy
    long sa = 0, sb = 0;
    for (int k = 0; k < 4; k++) { sa += xpt(0x91); sb += xpt(0xD1); }
    a = sa / 4; b = sb / 4;
  }
  xpt(0xD0);                                     // power down, pen IRQ enabled
  digitalWrite(T_CS, HIGH);
  return down;
}

static int touchTemp() {                         // SPI sanity check: internal temperature diode
  digitalWrite(T_CS, LOW);
  int v = xpt(0x87);
  xpt(0xD0);
  digitalWrite(T_CS, HIGH);
  return v;
}

static void rawToScreen(int a, int b, int& x, int& y) {
  int rx = tcal.swap ? b : a, ry = tcal.swap ? a : b;
  x = constrain((int)lroundf(tcal.ax * rx + tcal.bx), 0, 319);
  y = constrain((int)lroundf(tcal.ay * ry + tcal.by), 0, 239);
}

static void loadCal() {
  TouchCal c;
  if (prefs.getBytes("tcal", &c, sizeof c) == sizeof c && c.ok) tcal = c;
}

// fit from the four taps; false if they don't make sense
static bool solveCal() {
  float aL = (calRaw[0][0] + calRaw[3][0]) / 2.0f, aR = (calRaw[1][0] + calRaw[2][0]) / 2.0f;
  float bL = (calRaw[0][1] + calRaw[3][1]) / 2.0f, bR = (calRaw[1][1] + calRaw[2][1]) / 2.0f;
  float aT = (calRaw[0][0] + calRaw[1][0]) / 2.0f, aB = (calRaw[2][0] + calRaw[3][0]) / 2.0f;
  float bT = (calRaw[0][1] + calRaw[1][1]) / 2.0f, bB = (calRaw[2][1] + calRaw[3][1]) / 2.0f;
  TouchCal c = {};
  c.swap = fabsf(bR - bL) > fabsf(aR - aL);       // which channel moves with screen x
  float xl = c.swap ? bL : aL, xr = c.swap ? bR : aR, yt = c.swap ? aT : bT, yb = c.swap ? aB : bB;
  if (fabsf(xr - xl) < 800 || fabsf(yb - yt) < 600) return false;
  c.ax = (290 - 30) / (xr - xl); c.bx = 30 - c.ax * xl;
  c.ay = (210 - 30) / (yb - yt); c.by = 30 - c.ay * yt;
  c.ok = 1;
  tcal = c;
  prefs.putBytes("tcal", &tcal, sizeof tcal);
  return true;
}

static void startCal() { calStep = 0; screen = 8; redraw(); }

static bool in(const Btn& b, int x, int y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

static void showPlan(const String& err) {
  if (err.length()) { pending = Plan(); strlcpy(jobResult, err.c_str(), sizeof jobResult); screen = S_CONTROL; redraw(); return; }
  pending.id = nextId++; pending.made = millis();
  screen = S_CONFIRM; redraw();
}

static void touchPlan(Kind k, int a) { showPlan(makePlan(pending, k, a, -1)); }

static void onTap(int a, int b) {
  int x, y; rawToScreen(a, b, x, y);
  tA = a; tB = b; tX = x; tY = y; tCount++;
  Serial.printf("[touch] raw %d,%d z %d -> %d,%d (screen %d)\n", a, b, tZ, x, y, screen);
  if (screen == S_CAL) {
    if (millis() - calShown < 300) return;
    calRaw[calStep][0] = a; calRaw[calStep][1] = b;
    if (++calStep < 4) { drawCal(); return; }
    bool ok = solveCal();
    addEvent(ok ? 1 : 2, ok ? "Touchscreen calibrated" : "Touch calibration failed: try again");
    screen = S_HOME; redraw();
    return;
  }
  if (screen == S_TEST) { screen = S_HOME; redraw(); return; }
  if (screen == S_FIND) {
    for (int k = 0; k < discCount && k < 3; k++) {
      int fy = FIND_Y + k * (FIND_H + 4);
      if (discState != D_RUNNING && y >= fy && y < fy + FIND_H) { setHost(discFound[k].ip, "chosen on the screen"); screen = S_HOME; redraw(); return; }
    }
    if (y >= TAB_Y) { screen = S_HOME; redraw(); }
    return;
  }
  if (screen == S_CONFIRM) {
    if (in(OK_BTN, x, y) && pending.id && millis() - confirmShown > 400) { startJob(); screen = S_CONTROL; redraw(); }
    else if (in(NO_BTN, x, y)) { pending = Plan(); screen = S_CONTROL; redraw(); }
    return;
  }
  if (y >= TAB_Y) { screen = constrain((x - 2) / 79, 0, 3); redraw(); return; }
  if (screen == S_CONTROL) {
    bool en = controlsOn && !job.id && fresh();
    if (in(CBTN[3], x, y)) { requestPoll(); return; }
    if (!en) return;
    bool held = prefs.getBool("hold", false), cnow = prefs.getBool("cnow", false);
    if (in(CBTN[0], x, y)) touchPlan(held ? K_RESUME : K_HOLD, -1);
    else if (in(CBTN[1], x, y) && !held) touchPlan(cnow ? K_RESTORE : K_CHARGENOW, chargeNowUntil());
    else if (in(CBTN[2], x, y)) touchPlan(K_CLOCK, -1);
    return;
  }
}

// Sample every 15 ms; a tap fires after 3 pressed samples and re-arms after 3 released ones.
static void touchLoop() {
  static uint32_t last = 0;
  static int pressed = 0, released = 3, n = 0;
  static long sa = 0, sb = 0;
  if (millis() - last < 15) return;
  last = millis();
  int a, b, z;
  if (touchRaw(a, b, z)) {
    released = 0;
    if (pressed < 0) return;                     // already fired: wait for release
    sa += a; sb += b; n++; tZ = z;
    if (++pressed >= 3) { pressed = -1; onTap(sa / n, sb / n); }
  } else if (++released >= 3) { pressed = 0; sa = sb = n = 0; }
}

#else   // no display: the screen hooks used elsewhere do nothing
enum { S_HOME, S_SCHED, S_CONTROL, S_EVENTS, S_FIND = 5, S_TEST = 6, S_CAL = 8, S_CONFIRM = 9 };
static void redraw() {}
static void statusBar() {}
#endif   // HAS_DISPLAY

// ---------------------------------------------------------------- web
static String jsonEsc(const char* s) {
  String o; for (const char* p = s; *p; p++) { if (*p == '"' || *p == '\\') o += '\\'; if ((uint8_t)*p >= 32) o += *p; } return o;
}

static void planJson(String& j, const Plan& p) {
  j += "{\"id\":"; j += p.id; j += ",\"title\":\""; j += jsonEsc(p.title); j += "\",\"steps\":[";
  for (int k = 0; k < p.n; k++) {
    const Step& s = p.steps[k];
    if (k) j += ",";
    j += "{\"reg\":"; j += s.reg; j += ",\"name\":\""; j += givRegName(s.reg); j += "\",\"from\":\"";
    j += jsonEsc(fmtValue(s.reg, s.old).c_str()); j += "\",\"to\":\"";
    j += p.kind == K_CLOCK ? String("internet time") : jsonEsc(fmtValue(s.reg, s.value).c_str()); j += "\"}";
  }
  j += "]}";
}

static void handleStatus() {
  const Reading& r = last; const Inverter& i = r.inv;
  String j; j.reserve(3200);
  j += "{\"ok\":"; j += haveReading ? "true" : "false";
  j += ",\"now\":"; j += (long)time(nullptr);
  j += ",\"ts\":"; j += (long)r.ts; j += ",\"fails\":"; j += fails; j += ",\"sd\":"; j += sdOk ? "true" : "false"; j += ",\"fs\":"; j += fsOk ? "true" : "false";
  j += ",\"tripped\":"; j += tripped ? "true" : "false"; j += ",\"invert\":"; j += invertTft ? "true" : "false"; j += ",\"display\":"; j += HAS_DISPLAY ? "true" : "false";
  otaStatus(j);
  j += ",\"undo\":"; j += undo.n; j += ",\"jobId\":"; j += job.id;
  j += ",\"test\":{\"groups\":[";
  for (int g = 0; g < givTestGroups(); g++) {
    if (g) j += ",";
    j += "[\""; j += givTestGroupName(g); j += "\",0,[";
    uint16_t rr[3]; int m = givTestGroupRegs(g, rr);
    for (int k = 0; k < m; k++) {
      if (k) j += ",";
      j += "["; j += rr[k]; j += ","; j += last.at(rr[k]); j += ",-1,"; j += givIsTime(rr[k]) ? 1 : 0;
      j += ",\""; j += givRegName(rr[k]); j += "\"]";
    }
    j += "]]";
  }
  j += "]}";
  { time_t old; int n = hhCount(old); j += ",\"hist\":{\"n\":"; j += n; j += ",\"oldest\":"; j += (long)old;
    j += ",\"capDays\":"; j += hhCapacityDays(); j += "}"; }
  j += ",\"controls\":"; j += controlsOn ? "true" : "false"; j += ",\"host\":\""; j += jsonEsc(host); j += "\"";
  j += ",\"tz\":\""; j += jsonEsc(tz); j += "\",\"me\":\""; j += WiFi.localIP().toString(); j += "\"";
  j += ",\"disc\":{\"running\":"; j += discState == D_RUNNING ? "true" : "false"; j += ",\"progress\":"; j += discProgress; j += ",\"found\":[";
  for (int k = 0; k < discCount; k++) {
    const Found& f = discFound[k];
    if (k) j += ",";
    j += "[\""; j += f.ip; j += "\",\""; j += jsonEsc(f.serial); j += "\",\""; j += givModelName(f.model, f.arm); j += "\",\""; j += jsonEsc(f.dongle); j += "\"]";
  }
  j += "]}";
  j += ",\"held\":"; j += prefs.getBool("hold", false) ? "true" : "false";
  j += ",\"chargeNow\":"; j += prefs.getBool("cnow", false) ? "true" : "false";
  j += ",\"job\":"; if (job.id) { j += "{\"title\":\""; j += jsonEsc(job.title); j += "\",\"step\":"; j += jobStep; j += ",\"of\":"; j += job.n; j += "}"; } else j += "null";
  j += ",\"jobResult\":\""; j += jsonEsc(jobResult); j += "\"";
  long drift = clockDrift(r);
  bool sp = givSinglePhase(i.model);
  j += ",\"inv\":{\"serial\":\""; j += jsonEsc(i.serial); j += "\",\"serialOk\":"; j += i.serialOk ? "true" : "false";
  j += ",\"model\":\""; j += givModelName(i.model, i.arm); j += "\",\"aio\":"; j += i.model == M_AIO ? "true" : "false";
  j += ",\"single\":"; j += sp ? "true" : "false"; j += ",\"verified\":"; j += givModelVerified(i.model) ? "true" : "false";
  j += ",\"dongle\":\""; j += jsonEsc(i.dongle); j += "\",\"restore\":\""; j += restoreSerial() ? restoreSerial() : ""; j += "\"";
  j += ",\"valid\":"; j += i.valid ? "true" : "false";
  j += ",\"arm\":"; j += i.arm; j += ",\"dsp\":"; j += i.dsp; j += ",\"status\":"; j += i.status; j += ",\"fault\":"; j += i.fault;
  j += ",\"soc\":"; j += sp ? String(i.soc) : String("null"); j += ",\"w\":"; j += i.batteryW;
  j += ",\"grid\":"; j += i.gridW; j += ",\"load\":"; j += i.loadW; j += ",\"invW\":"; j += i.inverterW;
  j += ",\"bv\":"; j += isnan(i.batteryV) ? "null" : String(i.batteryV, 1);
  j += ",\"chgDay\":"; j += isnan(i.chargedToday) ? "null" : String(i.chargedToday, 1);
  j += ",\"disDay\":"; j += isnan(i.dischargedToday) ? "null" : String(i.dischargedToday, 1); j += ",\"limit\":"; j += i.chargeLimit;
  j += ",\"dlimit\":"; j += i.dischargeLimit; j += ",\"target\":"; j += i.target;
  j += ",\"reserve\":"; j += i.reserve; j += ",\"ac\":"; j += i.acCharge;
  j += ",\"slot\":["; j += i.slotStart; j += ","; j += i.slotEnd; j += "]";
  j += ",\"drift\":"; j += drift == INT32_MIN ? String("null") : String(drift);
  j += ",\"eco\":"; j += r.at(27); j += ",\"dslot\":["; j += r.at(56); j += ","; j += r.at(57); j += "]"; j += ",\"pv\":{\"w\":"; j += i.pv1W + i.pv2W; j += ",\"w1\":"; j += i.pv1W; j += ",\"w2\":"; j += i.pv2W;
  j += ",\"today\":"; j += isnan(i.pvToday) ? String("null") : String(i.pvToday, 1); j += ",\"show\":"; j += solarShown() ? "true" : "false";
  j += ",\"mode\":"; j += solarMode; j += "}"; j += ",\"mode\":\""; j += modeName(r); j += "\"}";
  j += ",\"cand\":[";
  for (size_t k = 0; k < sizeof CANDS / sizeof CANDS[0]; k++) {
    if (k) j += ",";
    j += "["; j += CANDS[k].reg; j += ","; j += r.at(CANDS[k].reg); j += ","; j += CANDS[k].kind; j += ",\""; j += CANDS[k].name; j += "\"]";
  }
  j += "],\"ext\":[";   // HR240-299: GivTCP extended charge / discharge slots (start, end, target)
  for (int k = 240; k < 300; k++) { if (k > 240) j += ","; j += r.at(k); }
  j += "]}";
  server.send(200, "application/json", j);
}

static void handleHistory() {
  String j; j.reserve(hCount * 40 + 20);
  j += "[";
  for (int i = 0; i < hCount; i++) {
    const Hist& h = hist[(hHead - hCount + i + 288) % 288];
    if (i) j += ",";
    j += "["; j += (long)h.ts; j += ","; j += h.soc; j += ","; j += h.w; j += ","; j += h.grid; j += ","; j += h.load; j += "]";
  }
  j += "]";
  server.send(200, "application/json", j);
}

static void handleEvents() {
  String j = "[";
  for (int i = 0; i < evCount; i++) {
    const Ev& e = events[(evHead - 1 - i + 40) % 40];
    if (i) j += ",";
    j += "["; j += (long)e.ts; j += ","; j += e.sev; j += ",\""; j += jsonEsc(e.txt); j += "\"]";
  }
  j += "]";
  server.send(200, "application/json", j);
}

// last ~6 KB of the change log, as text
static void handleChanges() {
  if (!fsOk || !SPIFFS.exists(LOG_FILE)) { server.send(200, "text/plain", ""); return; }
  File f = SPIFFS.open(LOG_FILE, FILE_READ);
  size_t sz = f.size(), from = sz > 6144 ? sz - 6144 : 0;
  f.seek(from);
  String s = f.readString(); f.close();
  if (from) { int nl = s.indexOf('\n'); if (nl >= 0) s = s.substring(nl + 1); }
  server.send(200, "text/plain", s);
}

static void handleFiles() {
  String j = "[";
  if (sdOk) {
    File d = SD.open("/givcontrol");
    bool first = true;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      if (!first) j += ","; first = false;
      j += "[\""; j += f.name(); j += "\","; j += (long)f.size(); j += "]";
    }
  }
  j += "]";
  server.send(200, "application/json", j);
}

// ---------------------------------------------------------------- half-hour history
struct Sink { String buf; int n; bool csv; };

static void flushSink(Sink& k, bool force) {
  if (k.buf.length() > 1400 || (force && k.buf.length())) { server.sendContent(k.buf); k.buf = ""; }
}

static void hhRow(const HHRec& r, void* ctx) {
  Sink& k = *(Sink*)ctx;
  char line[160];
  if (k.csv) {
    time_t a = r.start, b = r.start + 1800; struct tm la, lb; localtime_r(&a, &la); localtime_r(&b, &lb);
    char sa[20], sb[20]; strftime(sa, sizeof sa, "%Y-%m-%d %H:%M", &la); strftime(sb, sizeof sb, "%Y-%m-%d %H:%M", &lb);
    snprintf(line, sizeof line, "%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%s\n", sa, sb, r.imp / 1000.0, r.exp / 1000.0,
             r.chg / 1000.0, r.dis / 1000.0, r.load / 1000.0, r.pv / 1000.0, r.soc > 100 ? "" : String(r.soc).c_str(),
             r.flags & HH_OPEN ? "in progress" : r.flags & HH_COMPLETE ? "yes" : "no");
  } else {
    snprintf(line, sizeof line, "%s[%lu,%d,%d,%u,%u,%u,%u,%u,%u]", k.n ? "," : "", (unsigned long)r.start,
             r.soc > 100 ? -1 : r.soc, r.flags, r.imp, r.exp, r.chg, r.dis, r.load, r.pv);
  }
  k.buf += line; k.n++;
  flushSink(k, false);
}

// per local day: [day start, import, export, charge, discharge, load, solar (Wh), periods, complete periods]
struct DayAcc { time_t day; uint32_t v[6]; int n, full; };
struct DaySink { Sink out; DayAcc d; };

static void dayFlush(DaySink& k) {
  if (!k.d.n) return;
  char line[140];
  snprintf(line, sizeof line, "%s[%lu,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d]", k.out.n ? "," : "", (unsigned long)k.d.day,
           (unsigned long)k.d.v[0], (unsigned long)k.d.v[1], (unsigned long)k.d.v[2], (unsigned long)k.d.v[3],
           (unsigned long)k.d.v[4], (unsigned long)k.d.v[5], k.d.n, k.d.full);
  k.out.buf += line; k.out.n++;
  flushSink(k.out, false);
  k.d = {};
}

static void dayRow(const HHRec& r, void* ctx) {
  DaySink& k = *(DaySink*)ctx;
  time_t t = r.start; struct tm lt; localtime_r(&t, &lt); lt.tm_hour = lt.tm_min = lt.tm_sec = 0; lt.tm_isdst = -1;
  time_t day = mktime(&lt);
  if (k.d.n && k.d.day != day) dayFlush(k);
  k.d.day = day;
  uint16_t v[6] = {r.imp, r.exp, r.chg, r.dis, r.load, r.pv};
  for (int i = 0; i < 6; i++) k.d.v[i] += v[i];
  k.d.n++; if (r.flags & HH_COMPLETE) k.d.full++;
}

// GET /api/halfhours?from=&to=[&agg=day]   (epoch seconds)
static void handleHalfHours() {
  time_t to = server.hasArg("to") ? server.arg("to").toInt() : time(nullptr) + 1800;
  time_t from = server.hasArg("from") ? server.arg("from").toInt() : to - 86400;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  if (server.arg("agg") == "day") {
    DaySink k = {{"[", 0, false}, {}};
    hhForEach(from, to, dayRow, &k);
    dayFlush(k);
    k.out.buf += "]"; flushSink(k.out, true);
  } else {
    Sink k = {"[", 0, false};
    hhForEach(from, to, hhRow, &k);
    k.buf += "]"; flushSink(k, true);
  }
  server.sendContent("");
}

static void handleHalfHourCsv() {
  time_t to = server.hasArg("to") ? server.arg("to").toInt() : time(nullptr) + 1800;
  time_t from = server.hasArg("from") ? server.arg("from").toInt() : 0;
  server.sendHeader("Content-Disposition", "attachment; filename=\"givcontrol-halfhours.csv\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  Sink k = {"period_start,period_end,grid_import_kwh,grid_export_kwh,battery_charge_kwh,battery_discharge_kwh,"
            "home_kwh,solar_kwh,battery_pct_at_end,complete\n", 0, true};
  hhForEach(from, to, hhRow, &k);
  flushSink(k, true);
  server.sendContent("");
}

static void handleDownload() {
  String name = server.arg("f");
  if (name == "changes.csv" && fsOk && SPIFFS.exists(LOG_FILE)) {
    File f = SPIFFS.open(LOG_FILE, FILE_READ);
    server.sendHeader("Content-Disposition", "attachment; filename=\"givcontrol-changes.csv\"");
    server.streamFile(f, "text/csv"); f.close(); return;
  }
  if (!sdOk || name.indexOf('/') >= 0 || name.indexOf("..") >= 0) { server.send(404, "text/plain", "not found"); return; }
  File f = SD.open("/givcontrol/" + name);
  if (!f) { server.send(404, "text/plain", "not found"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.streamFile(f, "text/csv");
  f.close();
}

static int parseHHMM(const String& s) {     // "HH:MM" -> HHMM, or -1
  int c = s.indexOf(':');
  if (c < 1 || s.length() - c != 3) return -1;
  int h = s.substring(0, c).toInt(), m = s.substring(c + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 100 + m;
}

static void sendErr(const String& e) { server.send(400, "application/json", "{\"error\":\"" + jsonEsc(e.c_str()) + "\"}"); }

// Step 1 of 2: describe what would change. Nothing is written here.
static void handlePlan() {
  if (server.method() != HTTP_POST) { sendErr("POST only"); return; }
  String t = server.arg("type");
  Plan p; String err;
  if (t == "set") {
    // fields r<register>=value; whether each register may be written is decided by givAllowed()
    uint16_t regs[16]; int vals[16], n = 0;
    for (int k = 0; k < server.args() && !err.length(); k++) {
      String name = server.argName(k), a = server.arg(k);
      if (name.length() < 2 || name[0] != 'r' || !isDigit(name[1]) || !a.length()) continue;
      int reg = name.substring(1).toInt();
      if (reg <= 0 || reg > 359 || String(reg) != name.substring(1)) continue;
      if (n >= 16) { err = "too many changes at once"; break; }
      bool tm = givIsTime(reg);
      int v = tm ? parseHHMM(a) : a.toInt();
      if (v < 0 || (!tm && String(v) != a)) { err = String(givRegName(reg)) + ": not a valid value"; break; }
      regs[n] = reg; vals[n++] = v;
    }
    if (!err.length()) err = planSet(p, regs, vals, n);
  } else if (t == "undo") {
    if (!undo.n) err = "Nothing to undo";
    else { err = planSet(p, undo.reg, undo.val, undo.n); if (!err.length()) strcpy(p.title, "Undo last change"); }
  } else if (t == "hold") err = makePlan(p, K_HOLD, -1, -1);
  else if (t == "resume") err = makePlan(p, K_RESUME, -1, -1);
  else if (t == "chargenow") err = makePlan(p, K_CHARGENOW, parseHHMM(server.arg("until")), -1);
  else if (t == "restore") err = makePlan(p, K_RESTORE, -1, -1);
  else if (t == "clock") err = makePlan(p, K_CLOCK, -1, -1);
  else if (t == "serial") err = makePlan(p, K_SERIAL, -1, -1);
  else err = "unknown action";
  if (err.length()) { sendErr(err); return; }
  p.id = nextId++; p.made = millis();
  pending = p;
  if (server.arg("go") == "1") {   // the web page applies in one step: the unlock tick is the only gate
    startJob();
    String j = "{\"started\":true,\"plan\":"; planJson(j, job); j += "}";
    server.send(200, "application/json", j);
    return;
  }
  if (screen == S_CONFIRM) { screen = S_CONTROL; redraw(); }
  String j; planJson(j, pending);
  server.send(200, "application/json", j);
}

// Step 2 of 2: run a plan made in the last two minutes.
static void handleConfirm() {
  if (server.method() != HTTP_POST) { sendErr("POST only"); return; }
  uint32_t id = server.arg("id").toInt();
  if (!pending.id || id != pending.id) { sendErr("That change is no longer pending: start again"); return; }
  if (millis() - pending.made > PLAN_TTL) { pending = Plan(); sendErr("Took too long to confirm: start again"); return; }
  if (!controlsOn || job.id) { sendErr("Controls locked or busy"); return; }
  startJob();
  server.send(200, "application/json", "{\"started\":true}");
}

static void requestDiscovery();

// Use a new dongle address. Saved "pause"/"charge now" values belong to the old inverter.
static void setHost(const char* h, const char* why) {
  if (!strcmp(h, host)) return;
  strlcpy(host, h, sizeof host); prefs.putString("host", host);
  prefs.putBool("hold", false); prefs.putBool("cnow", false);
  haveReading = false; fails = 0;
  addEvent(1, "Dongle %s (%s)", host, why);
  requestPoll();
}

static void handleSettings() {
  if (server.method() != HTTP_POST) { sendErr("POST only"); return; }
  String h = server.arg("host");
  if (h.length()) {
    if (h.length() < 7 || h.length() >= sizeof host || !IPAddress().fromString(h)) { sendErr("Not a valid IP address"); return; }
    setHost(h.c_str(), "chosen in Settings");
  }
  String z = server.arg("tz");
  if (z.length() >= 3 && z.length() < sizeof tz) { strlcpy(tz, z.c_str(), sizeof tz); prefs.putString("tz", tz); setenv("TZ", tz, 1); tzset(); }
  if (server.hasArg("solar")) { solarMode = constrain((int)server.arg("solar").toInt(), 0, 2); prefs.putUChar("solar", solarMode); }
  if (server.hasArg("invert")) {
    invertTft = server.arg("invert") == "1"; prefs.putBool("inv3", invertTft);
#if HAS_DISPLAY
    tft.invertDisplay(invertTft); redraw();
#endif
  }
  if (server.hasArg("controls")) {
    bool want = server.arg("controls") == "1";
    if (want && server.arg("manual") != "1") { sendErr("Confirm that nothing else (GivTCP, Home Assistant, another app) is changing the battery settings"); return; }
    if (want != controlsOn) {
      controlsOn = want; prefs.putBool("controls", controlsOn);
      addEvent(2, "Controls %s", controlsOn ? "unlocked" : "locked");
      appendLog(controlsOn ? "controls,unlocked" : "controls,locked");
    }
  }
  redraw();
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------- poll
// The inverter read takes several seconds, so it runs in its own task; the result is applied
// in loop(). Polls and writes never overlap: a job waits for a running poll and vice versa.
static volatile bool pollBusy = false, pollDone = false, pollOk = false;
static Reading pollOut;
static TaskHandle_t pollTask = nullptr;

static volatile bool wantDiscovery = false;

static void pollWorker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (wantDiscovery) {
      wantDiscovery = false;
      discCount = 0;
      discCount = givDiscover(discFound, 6, &discProgress);
      discState = D_DONE;
      pollBusy = false;
      continue;
    }
    pollOk = givPoll(host, pollOut);
    pollDone = true;
  }
}

static void requestPoll() {
  if (pollBusy || job.id || !host[0] || time(nullptr) < 1700000000) return;   // no dongle / NTP not set yet
  lastPoll = millis();
  pollBusy = true;
  xTaskNotifyGive(pollTask);
}

// Scan the local network for GivEnergy dongles (runs in the poll task, ~30 s).
static void requestDiscovery() {
  if (pollBusy || job.id || discState == D_RUNNING) return;
  discState = D_RUNNING; discProgress = 0; discCount = 0; discLast = millis();
  addEvent(0, "Looking for GivEnergy dongles on the network");
  wantDiscovery = true; pollBusy = true;
  if (screen < S_TEST && !haveReading) screen = S_FIND;
  redraw();
  xTaskNotifyGive(pollTask);
}

static void discoveryDone() {
  discState = D_IDLE;   // results stay in discFound for the picker
  int n = discCount;
  addEvent(n ? 1 : 2, n ? "Found %d GivEnergy dongle%s" : "No GivEnergy dongle found%s", n, n == 1 ? "" : "s");
  bool current = false;
  for (int k = 0; k < n; k++) if (!strcmp(discFound[k].ip, host)) current = true;
  if (n == 1 && !current) setHost(discFound[0].ip, "found automatically");
  if (screen == S_FIND && (n <= 1 || current)) screen = S_HOME;
  redraw();
}

static void applyPoll() {
  pollDone = false;
  Reading& r = pollOut;
  if (pollOk) {
    if (commsDown) { commsDown = false; addEvent(1, "Inverter answering again"); }
    fails = 0;
    // keep the last good settings if this read lost a block
    if (!r.inv.valid && haveReading) {
      bool hr60 = r.inv.hr60, ir = r.inv.ir; Inverter keep = last.inv;
      if (hr60) { keep.slotStart = r.inv.slotStart; keep.slotEnd = r.inv.slotEnd; keep.acCharge = r.inv.acCharge;
                  keep.chargeLimit = r.inv.chargeLimit; keep.dischargeLimit = r.inv.dischargeLimit; keep.reserve = r.inv.reserve; keep.target = r.inv.target; }
      if (ir) { keep.status = r.inv.status; keep.fault = r.inv.fault; keep.soc = r.inv.soc; keep.batteryW = r.inv.batteryW; }
      keep.hr60 = hr60; keep.ir = ir;
      r.inv = keep;
    }
    last = r; haveReading = true; everAnswered = true;
    if (!pvSeen && givSinglePhase(r.inv.model) && r.inv.pv1W + r.inv.pv2W > 20) {   // real generation, not noise
      pvSeen = true; prefs.putBool("pvSeen", true); addEvent(1, "Solar generation seen: showing the solar tile");
    }
    // remember the serial once it has read the same, valid, twice in a row through this dongle
    static char prevSerial[11] = "";
    loadSaved(r.inv.dongle);
    if (r.inv.valid && r.inv.serialOk && r.inv.dongle[0]) {
      if (!strcmp(prevSerial, r.inv.serial) && strcmp(savedSerial, r.inv.serial)) {
        strlcpy(savedSerial, r.inv.serial, sizeof savedSerial);
        prefs.putString((String("s_") + savedDongle).c_str(), savedSerial);
        addEvent(1, "Saved inverter serial %s (dongle %s)", savedSerial, savedDongle);
      }
      strlcpy(prevSerial, r.inv.serial, sizeof prevSerial);
    }
    givSetContext(r.inv.model, restoreSerial());
    detect(r);
    logReading(r);
    if (r.inv.ir && givSinglePhase(r.inv.model)) {
      const Inverter& v = r.inv;
      hhSample(r.ts, max(0, -v.gridW), max(0, v.gridW), max(0, -v.batteryW), max(0, v.batteryW), max(0, v.loadW),
               v.pv1W + v.pv2W, v.soc, solarShown());
    }
    if (r.ts - lastHist >= 300 || hCount == 0) {
      Hist& h = hist[hHead];
      h.ts = r.ts; h.soc = r.inv.soc; h.w = r.inv.batteryW;
      h.grid = r.inv.gridW; h.load = r.inv.loadW;
      hHead = (hHead + 1) % 288; if (hCount < 288) hCount++;
      lastHist = r.ts;
    }
  } else {
    fails++;
    if (fails == 2 && !commsDown) { commsDown = true; addEvent(2, "Inverter not answering (reboot or flash?)"); }
    // the saved address never answered since boot: look for the dongle (it may have a new address)
    if (fails == 2 && !everAnswered) { pollBusy = false; requestDiscovery(); return; }
  }
  pollBusy = false;
  updateLed();
  if (screen < S_TEST) redraw();
}

// ---------------------------------------------------------------- setup / loop
#if HAS_DISPLAY
static void splash(const char* l1, const char* l2 = "", const char* l3 = "") {
  tft.fillScreen(C_BG);
  txt(l1, 160, 92, F_MID, C_TXT, C_BG, MC_DATUM);
  txt(l2, 160, 128, F_SMALL, C_MUT, C_BG, MC_DATUM);
  txt(l3, 160, 148, F_SMALL, C_MUT, C_BG, MC_DATUM);
}

static void handleTouch() {
  int a = -1, b = -1, z = 0;
  bool down = touchRaw(a, b, z);
  String j = "{\"temp\":" + String(touchTemp()) + ",\"down\":" + (down ? "true" : "false") + ",\"z\":" + z +
             ",\"taps\":" + tCount + ",\"last\":{\"a\":" + tA + ",\"b\":" + tB + ",\"z\":" + tZ + ",\"x\":" + tX + ",\"y\":" + tY +
             "},\"cal\":{\"ok\":" + tcal.ok + ",\"swap\":" + tcal.swap + ",\"ax\":" + String(tcal.ax, 5) + ",\"bx\":" + String(tcal.bx, 1) +
             ",\"ay\":" + String(tcal.ay, 5) + ",\"by\":" + String(tcal.by, 1) + "}}";
  server.send(200, "application/json", j);
}
#else
static void splash(const char*, const char* = "", const char* = "") {}
#endif

void setup() {
  Serial.begin(115200);
#if HAS_DISPLAY
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
#endif
  led(0, 0, 1);
  prefs.begin("givctl", false);
  invertTft = prefs.getBool("inv3", false);
#if HAS_DISPLAY
  pinMode(T_IRQ, INPUT); pinMode(T_MISO, INPUT);
  pinMode(T_CS, OUTPUT); digitalWrite(T_CS, HIGH);
  pinMode(T_CLK, OUTPUT); digitalWrite(T_CLK, LOW); pinMode(T_MOSI, OUTPUT);
  tft.init(); tft.setRotation(1); tft.invertDisplay(invertTft); tft.fillScreen(C_BG);
#endif
  splash("GivControl", "starting...");

  prefs.getString("host", host, sizeof host);
  prefs.getString("tz", tz, sizeof tz);
  if (!tz[0]) strcpy(tz, "GMT0BST,M3.5.0/1,M10.5.0");
  prefs.remove("svSer"); prefs.remove("svDng");   // replaced by one entry per dongle
  controlsOn = prefs.getBool("controls", false);
  solarMode = prefs.getUChar("solar", SOLAR_AUTO); pvSeen = prefs.getBool("pvSeen", false);
  prefs.remove("tstat"); prefs.remove("testOn");   // left over from the test-mode build
  candApply();
#if HAS_DISPLAY
  loadCal();
  Serial.printf("[touch] temp channel %d (0 or 4095 = no SPI link), calibrated %d\n", touchTemp(), tcal.ok);
#endif

  // Small C6 boards often have poor antennas: phones then see the setup network but can't join.
  // Lower transmit power, a fixed channel, 802.11b/g/n only (no 11ax), and log joins and DHCP.
  WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t) { Serial.println("[wifi] a device joined the setup network"); }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t) { Serial.println("[wifi] a device left the setup network"); }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
  WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t info) {
    Serial.printf("[wifi] gave address %s\n", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
  }, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFiManager wm;
  wm.setWiFiAPChannel(1);
  WiFiManagerParameter pHost("host", "GivEnergy dongle IP (leave blank to find it automatically)", host, sizeof host - 1);
  wm.addParameter(&pHost);
  wm.setAPCallback([](WiFiManager*) {
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);   // softAP start can reset it
    Serial.printf("[wifi] setup network up: channel 1, b/g/n, tx power %d (x0.25 dBm)\n", (int)WiFi.getTxPower());
  });
  wm.setConfigPortalTimeout(0);
  wm.setConnectTimeout(20);
  if (!wm.autoConnect("GivControl-Setup")) ESP.restart();
  if (strcmp(pHost.getValue(), host) != 0 && strlen(pHost.getValue()) > 6) {
    strlcpy(host, pHost.getValue(), sizeof host); prefs.putString("host", host);
  }
  if (host[0] && !IPAddress().fromString(host)) host[0] = 0;

  configTzTime(tz, "pool.ntp.org", "time.google.com");
  splash("Connected", "setting the clock...");
  for (int i = 0; i < 40 && time(nullptr) < 1700000000; i++) delay(250);   // up to 10 s for NTP
  MDNS.begin(MDNS_NAME);   // the C6 is givcontrol-c6.local, so it can sit next to a CYD (givcontrol.local)
  fsOk = SPIFFS.begin(true);
  if (fsOk) {   // history gets all but ~200 KB (events, change log, filesystem headroom)
    size_t t = SPIFFS.totalBytes();
    hhBegin(t > 400 * 1024 ? t - 200 * 1024 : t * 45 / 100);
    loadEvents();
  }
#if HAS_DISPLAY
  sdOk = SD.begin(SD_CS);
  if (sdOk) SD.mkdir("/givcontrol");
#endif

  server.on("/", []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status", handleStatus);
  server.on("/api/history", handleHistory);
  server.on("/api/events", handleEvents);
  server.on("/api/changes", handleChanges);
  server.on("/api/files", handleFiles);
  server.on("/api/plan", handlePlan);
  server.on("/api/confirm", handleConfirm);
  server.on("/api/settings", handleSettings);
  server.on("/api/refresh", []() { requestPoll(); server.send(200, "application/json", "{\"ok\":true}"); });
  server.on("/api/discover", []() {
    if (server.method() != HTTP_POST) { sendErr("POST only"); return; }
    if (pollBusy || job.id) { sendErr("Busy reading the inverter: try again in a few seconds"); return; }
    requestDiscovery(); server.send(200, "application/json", "{\"ok\":true}");
  });
#if HAS_DISPLAY
  server.on("/api/touch", handleTouch);
  server.on("/api/testcard", []() {
    if (server.method() != HTTP_POST) { sendErr("POST only"); return; }
    screen = S_TEST; redraw(); server.send(200, "application/json", "{\"ok\":true}");
  });
  // what the firmware has written to the screen's memory at x,y (RGB565), for checking colours remotely
  server.on("/api/pixel", []() {
    int x = server.arg("x").toInt(), y = server.arg("y").toInt();
    uint16_t c = tft.readPixel(constrain(x, 0, 319), constrain(y, 0, 239));
    char j[96]; snprintf(j, sizeof j, "{\"x\":%d,\"y\":%d,\"rgb565\":\"0x%04X\",\"r\":%d,\"g\":%d,\"b\":%d}", x, y, c,
                         (c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3);
    server.send(200, "application/json", j);
  });
  server.on("/api/touchcal", []() {
    if (server.method() != HTTP_POST) { sendErr("POST only"); return; }
    startCal(); server.send(200, "application/json", "{\"ok\":true}");
  });
#endif
  server.on("/api/halfhours", handleHalfHours);
  server.on("/halfhours.csv", handleHalfHourCsv);
  server.on("/download", handleDownload);
  otaBegin(server, prefs, [](uint8_t sev, const char* m) { addEvent(sev, "%s", m); }, []() { return job.id != 0; });
  server.begin();

  char ip[48]; snprintf(ip, sizeof ip, "http://%s  or  givcontrol.local", WiFi.localIP().toString().c_str());
  splash("Connected", ip, controlsOn ? "Controls unlocked" : "Controls locked (read only)");
  addEvent(0, "Started, reading %s%s%s", host, sdOk ? ", SD card" : "", controlsOn ? ", controls unlocked" : "");
  delay(2500);
  xTaskCreate(pollWorker, "poll", 8192, nullptr, 1, &pollTask);   // unpinned: the C6 has one core
#if HAS_DISPLAY
  if (!tcal.ok) startCal(); else redraw();
#endif
  requestPoll();   // read straight away
}

void loop() {
  server.handleClient();
  otaLoop(WiFi.status() == WL_CONNECTED && (everAnswered || !host[0]));
  if (pollDone) applyPoll();
  if (discState == D_DONE) discoveryDone();
  // no dongle known: look for one now, and again every 10 minutes until one is found
  if (!host[0] && discState != D_RUNNING && time(nullptr) > 1700000000 && (!discLast || millis() - discLast > 600000)) requestDiscovery();
  if (job.id) { if (!pollBusy) runJobStep(); }
  else if (millis() - lastPoll >= POLL_MS) requestPoll();
  if (pending.id && millis() - pending.made > PLAN_TTL) { pending = Plan(); if (screen == S_CONFIRM) { screen = S_CONTROL; redraw(); } }
#if HAS_DISPLAY
  if (screen == S_CAL && millis() - calShown > 90000) { screen = S_HOME; redraw(); }   // calibration abandoned
  touchLoop();
#endif
  static uint32_t lastClock = 0;
  if (millis() - lastClock > 30000 && screen < S_TEST) { lastClock = millis(); statusBar(); updateLed(); }
  delay(2);
}
