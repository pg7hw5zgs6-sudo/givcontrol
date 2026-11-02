// Firmware updates over the network: a web upload (Settings > Firmware) and, for development,
// ArduinoOTA/espota. Both need the update PIN the user sets on the web page (kept in NVS; there is
// no built-in PIN, and without one updates are off).
//
// Every image is checked before it is accepted (ESP image, built for this chip, fits the slot,
// checksum/SHA via Update.end). A newly installed image boots "pending": it is confirmed only once
// Wi-Fi is up and the inverter has answered (or no dongle is set yet), within 2 minutes; otherwise
// the bootloader rolls back to the previous firmware. Both outcomes are logged in Events.
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>

void otaBegin(WebServer& server, Preferences& prefs, void (*log)(uint8_t sev, const char* msg), bool (*busy)());
// Call from loop(). `healthy`: Wi-Fi connected and the first inverter read done (or no dongle set).
void otaLoop(bool healthy);
// ,"fw":{...} for /api/status
void otaStatus(String& j);
