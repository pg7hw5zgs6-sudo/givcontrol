#include "history.h"
#include <SPIFFS.h>

// One fixed file with a slot for every half-hour of two years: slot = (period start / 1800) mod
// SLOTS. Each half-hour overwrites the one from exactly two years earlier, so the file never grows
// and never fills the flash. A slot belongs to a period only if its stored start matches.
static const char* FILE_NAME = "/hh.ring";
static const uint32_t SLOTS_2Y = 730UL * 48;   // 35,040 half-hours
static uint32_t slots = 0;                     // may be fewer on a small data partition
static bool ready = false;
static int stored = 0;                         // slots holding a period from the last `slots` half-hours
static time_t oldestStart = 0;

static HHRec open_ = {};        // period in progress
static bool haveOpen = false;
static double acc[6];           // Wh being integrated
static uint32_t covered = 0;    // seconds of readings in the open period
static time_t lastT = 0;
static float lastP[6];
static int lastSoc = 255;
static bool lastSolar = false;

static time_t periodStart(time_t t) {
  struct tm lt; localtime_r(&t, &lt);
  return t - ((lt.tm_min % 30) * 60 + lt.tm_sec);
}

static uint32_t slotOf(time_t start) { return (uint32_t)(start / 1800) % slots; }

void hhBegin(size_t capBytes) {
  slots = min(SLOTS_2Y, (uint32_t)(capBytes / sizeof(HHRec)));
  size_t want = (size_t)slots * sizeof(HHRec);
  File f = SPIFFS.open(FILE_NAME, FILE_READ);
  size_t have = f ? f.size() : 0;
  if (f) f.close();
  if (have != want) {   // first boot (or a different size): preallocate the whole file once
    SPIFFS.remove(FILE_NAME);
    f = SPIFFS.open(FILE_NAME, FILE_WRITE);
    if (!f) return;
    static uint8_t zero[512];
    for (size_t done = 0; done < want; done += sizeof zero) f.write(zero, min(sizeof zero, want - done));
    f.close();
  }
  // count what's there and find the oldest, so the web page can say how much is kept
  f = SPIFFS.open(FILE_NAME, FILE_READ);
  HHRec r;
  time_t now = time(nullptr), limit = now - (time_t)slots * 1800;
  while (f.read((uint8_t*)&r, sizeof r) == sizeof r) {
    if (!r.start || (time_t)r.start < limit || (time_t)r.start > now) continue;
    stored++;
    if (!oldestStart || (time_t)r.start < oldestStart) oldestStart = r.start;
  }
  f.close();
  ready = true;
}

static void writeRec(const HHRec& r) {
  if (!ready) return;
  File f = SPIFFS.open(FILE_NAME, "r+");
  if (!f) return;
  HHRec old;
  f.seek(slotOf(r.start) * sizeof(HHRec));
  bool reused = f.read((uint8_t*)&old, sizeof old) == sizeof old && old.start && old.start != r.start;
  f.seek(slotOf(r.start) * sizeof(HHRec));
  f.write((const uint8_t*)&r, sizeof r);
  f.close();
  if (!reused) stored++;   // overwriting a two-year-old period keeps the count the same
  if (!oldestStart || reused) oldestStart = r.start - (time_t)(slots - 1) * 1800;
  if (stored == 1) oldestStart = r.start;
}

static uint16_t wh(double v) { return (uint16_t)constrain(lround(v), 0L, 65535L); }

static void fillOpen() {
  open_.imp = wh(acc[0]); open_.exp = wh(acc[1]); open_.chg = wh(acc[2]);
  open_.dis = wh(acc[3]); open_.load = wh(acc[4]); open_.pv = wh(acc[5]);
  open_.soc = lastSoc < 0 || lastSoc > 100 ? 255 : lastSoc;
  open_.flags = (covered >= 1500 ? HH_COMPLETE : 0) | (lastSolar ? HH_SOLAR : 0);
}

static void closeOpen() {
  if (!haveOpen) return;
  fillOpen();
  writeRec(open_);
  haveOpen = false;
}

static void startPeriod(time_t s) {
  open_ = {}; open_.start = s;
  for (double& a : acc) a = 0;
  covered = 0; haveOpen = true;
}

void hhSample(time_t t, int impW, int expW, int chgW, int disW, int loadW, int pvW, int soc, bool solar) {
  float p[6] = {(float)impW, (float)expW, (float)chgW, (float)disW, (float)loadW, (float)pvW};
  time_t s = periodStart(t);
  if (!haveOpen || (time_t)open_.start != s) {
    if (haveOpen && lastT && t - lastT <= 180 && (time_t)open_.start < s) {
      // the interval since the last reading crosses the boundary: split it exactly
      for (int k = 0; k < 6; k++) acc[k] += (lastP[k] + p[k]) / 2 * (s - lastT) / 3600.0;
      covered += s - lastT;
      lastT = s;
    }
    closeOpen();
    startPeriod(s);
  }
  if (lastT && t > lastT && t - lastT <= 180) {   // longer gaps are not filled in
    for (int k = 0; k < 6; k++) acc[k] += (lastP[k] + p[k]) / 2 * (t - lastT) / 3600.0;
    covered += t - lastT;
  }
  memcpy(lastP, p, sizeof p);
  lastT = t; lastSoc = soc; lastSolar = solar;
}

bool hhCurrent(HHRec& r) {
  if (!haveOpen) return false;
  fillOpen();
  r = open_; r.flags |= HH_OPEN;
  return true;
}

void hhForEach(time_t from, time_t to, void (*fn)(const HHRec&, void*), void* ctx) {
  if (ready) {
    time_t now = time(nullptr);
    time_t first = max(from, now - (time_t)slots * 1800);
    File f = SPIFFS.open(FILE_NAME, FILE_READ);
    if (f) {
      HHRec buf[48];   // read a day's run of consecutive slots at a time
      for (time_t t = periodStart(first); t < to && t <= now; ) {
        uint32_t s = slotOf(t), n = min((uint32_t)48, slots - s);   // stop at the end of the file
        n = min(n, (uint32_t)((to - t + 1799) / 1800));
        f.seek(s * sizeof(HHRec));
        size_t got = f.read((uint8_t*)buf, n * sizeof(HHRec)) / sizeof(HHRec);
        for (size_t i = 0; i < got; i++) {
          time_t st = t + (time_t)i * 1800;
          if ((time_t)buf[i].start == st && st >= from && st < to) fn(buf[i], ctx);
        }
        if (!got) break;
        t += (time_t)got * 1800;
      }
      f.close();
    }
  }
  HHRec c;
  if (hhCurrent(c) && (time_t)c.start >= from && (time_t)c.start < to) fn(c, ctx);
}

int hhCount(time_t& oldest) { oldest = oldestStart; return stored; }

int hhCapacityDays() { return slots / 48; }
