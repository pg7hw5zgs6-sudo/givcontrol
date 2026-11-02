// GivEnergy dongle protocol (TCP 8899).
// Frames: 0x5959 0x0001 len | uid 0x01 fid 0x02 | adapter serial(10) pad(8) | RTU pdu | crc
// Reads: function 3/4, 60 registers per block. Writes: function 6, ONLY registers that
// givAllowed() accepts, one at a time, each read back and retried (see givWrite).
#pragma once
#include <Arduino.h>

// Device type from HR0's top hex digit (GivTCP / givenergy-modbus "model" decoding).
enum Model : uint8_t { M_UNKNOWN, M_HYBRID, M_AC, M_HYBRID_3PH, M_EMS, M_AC_3PH, M_GATEWAY, M_AIO };
Model givModel(uint16_t hr0);
// "All-in-One", "Hybrid Gen 3", ... (hybrid generation is guessed from the ARM firmware)
String givModelName(Model m, uint16_t arm);
// Single-phase models use the register map these reads come from; others show "-".
inline bool givSinglePhase(Model m) { return m == M_AIO || m == M_HYBRID || m == M_AC; }

struct Inverter {
  bool valid = false;        // HR0-59 block was good
  bool hr60 = false;         // HR60-119 block was good
  bool ir = false;           // IR0-59 block was good
  Model model = M_UNKNOWN;
  char serial[11] = "";      // HR13-17
  bool serialOk = false;     // looks like a real serial (not XXXXXXXXXX / blank)
  char dongle[11] = "";      // the WiFi dongle's own serial, from the reply frames
  uint16_t arm = 0, dsp = 0;
  int clock[6] = {-1, -1, -1, -1, -1, -1};  // HR35-40: Y-2000 M D h m s
  int status = -1;           // IR0: 1 = normal, 0 = idle/stopped
  uint32_t fault = 0;        // IR39/40
  int soc = -1;              // IR59
  int batteryW = 0;          // IR52, signed, negative = charging
  // From the GivTCP map, plausible on the AIO but not verified:
  int gridW = 0;             // IR30, signed, positive = export
  int loadW = 0;             // IR42 home load
  int inverterW = 0;         // IR24, signed
  float batteryV = NAN;      // IR50 / 100 (pack ~318 V)
  float chargedToday = NAN, dischargedToday = NAN;  // IR36/37 kWh / 10
  // Solar on the inverter's own PV inputs (hybrids; zero on an AIO, which has none), read directly:
  int pv1W = 0, pv2W = 0;    // IR18 / IR20
  float pvToday = NAN;       // IR17 + IR19, kWh / 10
  int chargeLimit = -1;      // HR111
  int dischargeLimit = -1;   // HR112
  int reserve = -1;          // HR114
  int acCharge = -1;         // HR96
  int slotStart = -1, slotEnd = -1;  // HR94/95 HHMM
  int target = -1;           // HR116 (probably charge target %, NOT verified, read only)
};

// Holding registers 0-359 as read (blocks of 60). Used to show GivTCP-map settings that are
// not verified on the AIO yet: they are displayed, never written.
static const int HR_COUNT = 360;

struct Reading {
  time_t ts = 0;
  Inverter inv;
  uint16_t hr[HR_COUNT] = {0};
  uint8_t hrGot = 0;         // bit n = block HR(60n)-(60n+59) was read
  bool has(int reg) const { return reg >= 0 && reg < HR_COUNT && (hrGot >> (reg / 60) & 1); }
  int at(int reg) const { return has(reg) ? hr[reg] : -1; }
};

// Read everything once. Returns false if the inverter didn't answer.
bool givPoll(const char* host, Reading& r);

// Tell the write whitelist which model it is talking to and which serial (if any) may be
// written back to HR13-17. Until a model is known nothing can be written.
void givSetContext(Model m, const char* restoreSerial);
// Registers verified writable for this model (nullptr-terminated names are not needed).
bool givModelVerified(Model m);

// ---- Test mode: settings from the GivTCP map not yet verified on the AIO, grouped (a slot's start and
// end are one group). With test mode on, the user may write them within fixed ranges and mark each
// group as working (then it stays writable) or failed (then it's blocked again). HR163 and anything
// not listed stay blocked whatever the mode.
enum { TEST_UNTESTED = 0, TEST_WORKS = 1, TEST_FAILED = 2 };
int givTestGroups();
const char* givTestGroupName(int g);
int givTestGroupRegs(int g, uint16_t* out);   // up to 3
int givTestGroupOf(uint16_t reg);             // -1 if not a test candidate
bool givIsTime(uint16_t reg);                 // HH:MM register (verified or candidate)
void givSetTest(bool on, const uint8_t* status);   // status: one byte per group

// The write whitelist. Returns nullptr if (reg, value) may be written, else the reason.
const char* givAllowed(uint16_t reg, uint16_t value);
// Human name of a writable register ("" if not writable).
const char* givRegName(uint16_t reg);

// Write one holding register on the inverter (slave 0x11), read it back, retry up to
// 3 times. Waits so writes are at least 1 s apart. `got` is the read-back value (-1 if
// none). Returns true only if the read-back matches. `msg` explains failures.
bool givWrite(const char* host, uint16_t reg, uint16_t value, int& got, String& msg);

// ---------------------------------------------------------------- discovery
struct Found { char ip[16]; char serial[11]; char dongle[11]; Model model; uint16_t arm; };
// Ask `host` for HR0 (a harmless read). True only if a valid GivEnergy reply comes back.
bool givProbe(const char* host, Found& f);
// Scan this device's /24 for TCP 8899 and probe each open port. `progress` counts 0-254.
int givDiscover(Found* out, int maxFound, volatile int* progress);
