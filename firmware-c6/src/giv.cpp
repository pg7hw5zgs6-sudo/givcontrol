#include "giv.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include <fcntl.h>

static const uint16_t PORT = 8899;
static const uint8_t INVERTER = 0x11;

static uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
  }
  return c;
}

// pdu = slave func reg(2) count-or-value(2)
static size_t frame(uint8_t* out, const uint8_t inner[6]) {
  uint16_t c = crc16(inner, 6);
  const char* adapter = "AB1234G567";   // placeholder; the dongle ignores it
  size_t i = 0;
  out[i++] = 0x59; out[i++] = 0x59; out[i++] = 0x00; out[i++] = 0x01;
  uint16_t len = 2 + 10 + 8 + 6 + 2;
  out[i++] = len >> 8; out[i++] = len & 0xFF;
  out[i++] = 0x01; out[i++] = 0x02;
  memcpy(out + i, adapter, 10); i += 10;
  for (int k = 0; k < 7; k++) out[i++] = 0; out[i++] = 8;
  memcpy(out + i, inner, 6); i += 6;
  out[i++] = c & 0xFF; out[i++] = c >> 8;
  return i;
}

static size_t buildRead(uint8_t* out, uint8_t slave, uint8_t func, uint16_t base, uint16_t count) {
  if (func != 3 && func != 4) return 0;
  uint8_t inner[6] = {slave, func, (uint8_t)(base >> 8), (uint8_t)base, (uint8_t)(count >> 8), (uint8_t)count};
  return frame(out, inner);
}

// ---------------------------------------------------------------- models
Model givModel(uint16_t hr0) {
  switch (hr0 >> 12) {
    case 2: return M_HYBRID; case 3: return M_AC; case 4: return M_HYBRID_3PH; case 5: return M_EMS;
    case 6: return M_AC_3PH; case 7: return M_GATEWAY; case 8: return M_AIO;
  }
  return M_UNKNOWN;
}

String givModelName(Model m, uint16_t arm) {
  switch (m) {
    case M_AIO: return "All-in-One";
    case M_HYBRID: return arm / 100 == 3 ? "Hybrid Gen 3" : (arm / 100 == 8 || arm / 100 == 9) ? "Hybrid Gen 2" : "Hybrid Gen 1";
    case M_AC: return "AC-coupled";
    case M_HYBRID_3PH: return "3-phase Hybrid";
    case M_AC_3PH: return "3-phase AC";
    case M_EMS: return "EMS";
    case M_GATEWAY: return "Gateway";
    default: return "Unknown";
  }
}

// ---------------------------------------------------------------- write whitelist
// Only these registers, only these values, only on models where they have been verified.
// Everything else (HR163 restart above all) is refused.
struct Allowed { uint16_t reg; uint16_t lo, hi; bool hhmm; const char* name; };
// All-in-One: each verified on a real AIO by writing and reading back (2026-10).
static const Allowed AIO_REGS[] = {
  {27, 0, 1, false, "eco mode"},              // 0 = battery idle, 1 = follows demand
  {35, 24, 99, false, "clock year"},
  {36, 1, 12, false, "clock month"},
  {37, 1, 31, false, "clock day"},
  {38, 0, 23, false, "clock hour"},
  {39, 0, 59, false, "clock minute"},
  {40, 0, 59, false, "clock second"},
  {56, 0, 2359, true, "discharge slot 1 start"},  // verified 2026-10-23 (read back; no effect while HR59 = 0)
  {57, 0, 2359, true, "discharge slot 1 end"},
  {94, 0, 2359, true, "charge slot start"},
  {95, 0, 2359, true, "charge slot end"},
  {96, 0, 1, false, "AC charge enable"},
  {111, 2, 50, false, "charge rate limit"},
  {112, 2, 50, false, "discharge rate limit"},
  {114, 4, 100, false, "battery reserve %"},
};
static const struct { Model model; const Allowed* regs; int n; } VERIFIED[] = {
  {M_AIO, AIO_REGS, sizeof AIO_REGS / sizeof AIO_REGS[0]},
  // other models: nothing verified yet, so everything stays read only
};

// ---------------------------------------------------------------- test-mode candidates
// GivTCP-map settings not yet verified on the AIO. Groups 0-8 below; groups 9-28 are the extra
// charge (HR240-269) and discharge (HR270-299) slots: start, end, target %.
struct Cand { uint16_t reg; uint16_t lo, hi; bool hhmm; const char* name; uint8_t group; };
static const Cand CANDS[] = {
  {59, 0, 1, false, "timed discharge / export", 0},
  {44, 0, 2359, true, "discharge slot 2 start", 1}, {45, 0, 2359, true, "discharge slot 2 end", 1},
  {31, 0, 2359, true, "charge slot 2 start", 2}, {32, 0, 2359, true, "charge slot 2 end", 2},
  {116, 4, 100, false, "charge target %", 3},
  {242, 4, 100, false, "slot 1 charge target %", 4},
  {20, 0, 1, false, "charge target enable", 5},
  {26, 0, 6000, false, "export limit W", 6},
  {318, 0, 3, false, "pause mode", 7},
  {319, 0, 2359, true, "pause slot start", 8}, {320, 0, 2359, true, "pause slot end", 8},
};
static const char* GROUPS[9] = {"Timed discharge / export", "Discharge slot 2", "Charge slot 2", "Charge target",
                                "Slot 1 charge target", "Charge target enable", "Export limit", "Pause mode", "Pause slot"};
static const int NGROUPS = 9 + 20;
static bool gTest = false;
static uint8_t gStat[NGROUPS];

static bool extReg(uint16_t reg) { return reg >= 240 && reg <= 299 && reg != 242; }   // 242 is its own group

static bool candidate(uint16_t reg, Cand& c) {
  for (const Cand& x : CANDS) if (x.reg == reg) { c = x; return true; }
  if (!extReg(reg)) return false;
  int k = (reg - 240) / 3, pos = (reg - 240) % 3;
  static char names[60][28];
  char* n = names[reg - 240];
  snprintf(n, 28, "%s slot %d %s", k < 10 ? "extra charge" : "extra discharge", k % 10 + 1, pos == 0 ? "start" : pos == 1 ? "end" : "target %");
  c = {reg, (uint16_t)(pos == 2 ? 4 : 0), (uint16_t)(pos == 2 ? 100 : 2359), pos < 2, n, (uint8_t)(9 + k)};
  return true;
}

int givTestGroups() { return NGROUPS; }

const char* givTestGroupName(int g) {
  static char b[32];
  if (g < 9) return GROUPS[g];
  snprintf(b, sizeof b, "Extra %s slot %d", g < 19 ? "charge" : "discharge", (g - 9) % 10 + 1);
  return b;
}

int givTestGroupRegs(int g, uint16_t* out) {
  int n = 0;
  if (g < 9) { for (const Cand& x : CANDS) if (x.group == g) out[n++] = x.reg; return n; }
  uint16_t b = 240 + (g - 9) * 3;
  for (int i = 0; i < 3; i++) if (b + i != 242) out[n++] = b + i;   // slot 1's target is the separate HR242 group
  return n;
}

int givTestGroupOf(uint16_t reg) { Cand c; return candidate(reg, c) ? c.group : -1; }

void givSetTest(bool on, const uint8_t* status) { gTest = on; memcpy(gStat, status, NGROUPS); }

static Model gModel = M_UNKNOWN;
static char gRestore[11] = "";   // serial that may be written back to HR13-17 ("" = none)

void givSetContext(Model m, const char* restoreSerial) {
  gModel = m;
  strlcpy(gRestore, restoreSerial ? restoreSerial : "", sizeof gRestore);
}

static const Allowed* table(Model m, int& n) {
  for (auto& v : VERIFIED) if (v.model == m) { n = v.n; return v.regs; }
  n = 0; return nullptr;
}

bool givModelVerified(Model m) { int n; return table(m, n) != nullptr; }

bool givIsTime(uint16_t reg) {
  for (const Allowed& a : AIO_REGS) if (a.reg == reg) return a.hhmm;
  Cand c; return candidate(reg, c) && c.hhmm;
}

const char* givRegName(uint16_t reg) {
  for (const Allowed& a : AIO_REGS) if (a.reg == reg) return a.name;
  Cand c; if (candidate(reg, c)) return c.name;
  if (reg >= 13 && reg <= 17) return "serial";
  return "";
}

const char* givAllowed(uint16_t reg, uint16_t value) {
  if (reg == 163) return "HR163 (restart) is never written";
  int n; const Allowed* t = table(gModel, n);
  if (!t) return "not verified for this model";
  if (reg >= 13 && reg <= 17) {
    if (strlen(gRestore) != 10) return "no saved serial to restore";
    int k = reg - 13;
    uint16_t want = ((uint8_t)gRestore[2 * k] << 8) | (uint8_t)gRestore[2 * k + 1];
    return value == want ? nullptr : "serial may only be restored to the saved value";
  }
  for (int i = 0; i < n; i++) {
    const Allowed& a = t[i];
    if (a.reg != reg) continue;
    if (value < a.lo || value > a.hi) return "value out of range";
    if (a.hhmm && (value % 100 > 59 || value / 100 > 23)) return "not a valid HH:MM time";
    return nullptr;
  }
  Cand c;
  if (gModel == M_AIO && candidate(reg, c)) {   // test candidates: AIO only, within fixed ranges
    uint8_t st = gStat[c.group];
    if (st == TEST_FAILED) return "marked as not working in the test checklist";
    if (st != TEST_WORKS && !gTest) return "not verified yet (turn on Test mode to try it)";
    if (value < c.lo || value > c.hi) return "value out of range";
    if (c.hhmm && (value % 100 > 59 || value / 100 > 23)) return "not a valid HH:MM time";
    return nullptr;
  }
  return "register not on the write whitelist";
}

// ---------------------------------------------------------------- one TCP session with the dongle
struct Block { uint8_t slave, func; uint16_t base; bool got; uint16_t regs[60]; };

class Link {
 public:
  WiFiClient c;
  uint8_t buf[1024];
  size_t have = 0;
  bool echo = false; uint16_t echoReg = 0, echoVal = 0;
  char adapter[11] = "";   // dongle serial from the last reply from the inverter

  bool open(const char* host, int ms = 3000) { c.setTimeout(3); have = 0; adapter[0] = 0; return host[0] && c.connect(host, PORT, ms); }
  void close() { c.stop(); }

  // Read whatever has arrived and file matching block replies. The dongle relays everyone's
  // traffic, so replies are matched on what they say (slave, func, base), not request order.
  void pump(Block* blocks, int n) {
    while (c.available() && have < sizeof(buf)) buf[have++] = c.read();
    while (have >= 6) {
      if (buf[0] != 0x59 || buf[1] != 0x59) { memmove(buf, buf + 1, --have); continue; }
      size_t ln = (buf[4] << 8) | buf[5];
      if (6 + ln > sizeof(buf)) { have = 0; break; }
      if (have < 6 + ln) break;
      handle(buf, 6 + ln, blocks, n);
      memmove(buf, buf + 6 + ln, have - 6 - ln);
      have -= 6 + ln;
    }
  }

  void handle(const uint8_t* f, size_t n, Block* blocks, int nb) {
    if (n < 42 || f[7] != 0x02) return;
    const uint8_t* p = f + 8;
    uint8_t slave = p[18], func = p[19];
    uint16_t a = (p[30] << 8) | p[31], b = (p[32] << 8) | p[33];
    if (slave == INVERTER) {
      bool ok = true;
      for (int k = 0; k < 10; k++) if (!isalnum(p[k])) ok = false;
      if (ok) { memcpy(adapter, p, 10); adapter[10] = 0; }
    }
    if (func == 6 && slave == INVERTER) { echo = true; echoReg = a; echoVal = b; return; }
    if (func != 3 && func != 4) return;
    if (b != 60 || n < 8 + 34 + 120) return;   // short or foreign frame
    for (int i = 0; i < nb; i++) {
      if (blocks[i].slave == slave && blocks[i].func == func && blocks[i].base == a) {
        for (int k = 0; k < 60; k++) blocks[i].regs[k] = (p[34 + 2 * k] << 8) | p[35 + 2 * k];
        blocks[i].got = true;
      }
    }
  }

  // Request every missing block, up to `passes` times each.
  void fetch(Block* blocks, int n, int passes) {
    uint8_t req[64];
    for (int pass = 0; pass < passes; pass++)
    for (int i = 0; i < n; i++) {
      if (blocks[i].got) continue;
      size_t len = buildRead(req, blocks[i].slave, blocks[i].func, blocks[i].base, 60);
      c.write(req, len);
      uint32_t until = millis() + 3000;
      while (millis() < until && !blocks[i].got && c.connected()) { pump(blocks, n); delay(5); }
      delay(150);
    }
  }
};

static int16_t s16(uint16_t v) { return (int16_t)v; }

static void ascii(const uint16_t* regs, int n, char* out) {
  for (int i = 0; i < n; i++) { out[2 * i] = regs[i] >> 8; out[2 * i + 1] = regs[i] & 0xFF; }
  out[2 * n] = 0;
  for (int i = 0; i < 2 * n; i++) if (out[i] == 0) out[i] = '?';
}

// 10 letters/digits and not the XXXXXXXXXX an inverter reports after losing its serial
static bool serialLooksReal(const char* s) {
  if (strlen(s) != 10 || !strcmp(s, "XXXXXXXXXX")) return false;
  for (int i = 0; i < 10; i++) if (!isalnum((unsigned char)s[i])) return false;
  return true;
}

// all-zero HR0 block (firmware 0) = garbled reply or inverter restarting
static bool hr0Good(const uint16_t* hr0) { return !(hr0[19] == 0 && hr0[21] == 0); }

static const struct { uint8_t slave, func; uint16_t base; } REQ[] = {
  {INVERTER, 3, 0}, {INVERTER, 3, 60}, {INVERTER, 4, 0}, {INVERTER, 3, 240}, {INVERTER, 3, 300},
};
static const int NREQ = sizeof(REQ) / sizeof(REQ[0]);

bool givPoll(const char* host, Reading& r) {
  static Block blocks[NREQ];
  static Link link;
  for (int i = 0; i < NREQ; i++) { blocks[i] = {REQ[i].slave, REQ[i].func, REQ[i].base, false, {0}}; }
  if (!link.open(host)) return false;
  link.fetch(blocks, NREQ, 3);   // replies can arrive late or get lost: retry anything missing
  link.close();

  r = Reading();
  r.ts = time(nullptr);
  memcpy(r.inv.dongle, link.adapter, sizeof r.inv.dongle);
  const uint16_t* hr0 = blocks[0].got ? blocks[0].regs : nullptr;
  const uint16_t* hr60 = blocks[1].got ? blocks[1].regs : nullptr;
  const uint16_t* ir0 = blocks[2].got ? blocks[2].regs : nullptr;
  for (int i = 0; i < NREQ; i++) {
    if (!blocks[i].got || blocks[i].func != 3 || (i == 0 && !hr0Good(hr0))) continue;
    memcpy(r.hr + blocks[i].base, blocks[i].regs, sizeof blocks[i].regs);
    r.hrGot |= 1 << (blocks[i].base / 60);
  }
  if (hr0 && hr0Good(hr0)) {
    r.inv.valid = true;
    ascii(hr0 + 13, 5, r.inv.serial);
    r.inv.serialOk = serialLooksReal(r.inv.serial);
    r.inv.model = givModel(hr0[0]);
    r.inv.dsp = hr0[19]; r.inv.arm = hr0[21];
    for (int k = 0; k < 6; k++) r.inv.clock[k] = hr0[35 + k];
  }
  if (hr60) {
    r.inv.hr60 = true;
    r.inv.slotStart = hr60[34]; r.inv.slotEnd = hr60[35]; r.inv.acCharge = hr60[36];
    r.inv.chargeLimit = hr60[51]; r.inv.dischargeLimit = hr60[52]; r.inv.reserve = hr60[54];
    r.inv.target = hr60[56];
  }
  if (ir0) {
    r.inv.ir = true;
    r.inv.status = ir0[0];
    r.inv.fault = ((uint32_t)ir0[39] << 16) | ir0[40];
    r.inv.soc = ir0[59];
    r.inv.batteryW = s16(ir0[52]);
    r.inv.gridW = s16(ir0[30]); r.inv.loadW = ir0[42]; r.inv.inverterW = s16(ir0[24]);
    r.inv.batteryV = ir0[50] / 100.0f; r.inv.chargedToday = ir0[36] / 10.0f; r.inv.dischargedToday = ir0[37] / 10.0f;
    r.inv.pv1W = ir0[18]; r.inv.pv2W = ir0[20]; r.inv.pvToday = (ir0[17] + ir0[19]) / 10.0f;
  }
  return r.inv.valid || ir0 != nullptr;
}

// ---------------------------------------------------------------- write
static bool matches(uint16_t reg, uint16_t want, uint16_t got) {
  if (reg == 40) return (got + 60 - want) % 60 <= 5;   // the inverter's seconds keep ticking
  return got == want;
}

bool givWrite(const char* host, uint16_t reg, uint16_t value, int& got, String& msg) {
  static uint32_t lastWrite = 0;
  got = -1;
  const char* why = givAllowed(reg, value);
  if (why) { msg = why; return false; }

  static Link link;
  static Block blk;
  uint8_t inner[6] = {INVERTER, 6, (uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)(value >> 8), (uint8_t)value};
  uint8_t req[64];
  size_t len = frame(req, inner);
  uint16_t base = reg / 60 * 60;   // read back the 60-register block the register is in

  for (int attempt = 1; attempt <= 3; attempt++) {
    while (millis() - lastWrite < 1100) delay(20);   // the dongle drops back-to-back writes
    if (!link.open(host)) { msg = "dongle not reachable"; delay(1000); continue; }
    link.echo = false;
    link.c.write(req, len);
    lastWrite = millis();
    uint32_t until = millis() + 2000;
    while (millis() < until && !(link.echo && link.echoReg == reg) && link.c.connected()) { link.pump(nullptr, 0); delay(5); }
    delay(800);   // let the inverter apply it before reading back
    blk = {INVERTER, 3, base, false, {0}};
    link.fetch(&blk, 1, 3);
    link.close();
    if (!blk.got || (base == 0 && !hr0Good(blk.regs))) { msg = "no read-back"; continue; }
    got = blk.regs[reg - base];
    if (matches(reg, value, got)) { msg = attempt > 1 ? "ok after " + String(attempt) + " tries" : "ok"; return true; }
    msg = "read back " + String(got);
  }
  return false;
}

// ---------------------------------------------------------------- discovery
bool givProbe(const char* host, Found& f) {
  static Link link;
  static Block blk;
  blk = {INVERTER, 3, 0, false, {0}};
  if (!link.open(host, 1500)) return false;
  link.fetch(&blk, 1, 2);
  link.close();
  if (!blk.got || !hr0Good(blk.regs)) return false;
  memset(&f, 0, sizeof f);
  strlcpy(f.ip, host, sizeof f.ip);
  ascii(blk.regs + 13, 5, f.serial);
  memcpy(f.dongle, link.adapter, sizeof f.dongle);
  f.model = givModel(blk.regs[0]);
  f.arm = blk.regs[21];
  return true;
}

// lwIP allows only a handful of sockets, so connect to a few hosts at a time without blocking.
int givDiscover(Found* out, int maxFound, volatile int* progress) {
  IPAddress me = WiFi.localIP();
  const int BATCH = 5;
  int found = 0;
  *progress = 0;
  for (int start = 1; start <= 254 && found < maxFound; start += BATCH) {
    int fds[BATCH], hostOf[BATCH];
    for (int k = 0; k < BATCH; k++) {
      fds[k] = -1; int h = start + k; hostOf[k] = h;
      if (h > 254 || h == me[3]) continue;
      int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
      if (fd < 0) continue;
      lwip_fcntl(fd, F_SETFL, O_NONBLOCK);
      sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(PORT);
      a.sin_addr.s_addr = (uint32_t)IPAddress(me[0], me[1], me[2], h);
      lwip_connect(fd, (sockaddr*)&a, sizeof a);
      fds[k] = fd;
    }
    // wait up to 600 ms for connections to complete
    bool open[BATCH] = {false};
    uint32_t until = millis() + 600;
    while (millis() < until) {
      fd_set w; FD_ZERO(&w); int mx = -1, pending = 0;
      for (int k = 0; k < BATCH; k++) if (fds[k] >= 0 && !open[k]) { FD_SET(fds[k], &w); mx = max(mx, fds[k]); pending++; }
      if (!pending) break;
      timeval tv = {0, 50000};
      if (lwip_select(mx + 1, nullptr, &w, nullptr, &tv) <= 0) continue;
      for (int k = 0; k < BATCH; k++) {
        if (fds[k] < 0 || open[k] || !FD_ISSET(fds[k], &w)) continue;
        int err = 0; socklen_t l = sizeof err;
        lwip_getsockopt(fds[k], SOL_SOCKET, SO_ERROR, &err, &l);
        if (err == 0) open[k] = true; else { lwip_close(fds[k]); fds[k] = -1; }   // refused
      }
    }
    for (int k = 0; k < BATCH; k++) if (fds[k] >= 0) lwip_close(fds[k]);
    for (int k = 0; k < BATCH && found < maxFound; k++) {
      if (!open[k]) continue;
      String ip = IPAddress(me[0], me[1], me[2], hostOf[k]).toString();
      if (givProbe(ip.c_str(), out[found])) found++;
    }
    *progress = min(254, start + BATCH - 1);
  }
  *progress = 254;
  return found;
}
