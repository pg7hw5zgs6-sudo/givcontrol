// Half-hour energy history, kept on flash so it survives restarts.
// Periods are aligned to :00 and :30 local time, like smart-meter / tariff half-hours. Energy is
// integrated from each reading (time-weighted, split exactly at the boundaries). A period with
// less than 25 minutes of readings is flagged incomplete rather than guessed.
#pragma once
#include <Arduino.h>

enum { HH_COMPLETE = 1, HH_SOLAR = 2, HH_OPEN = 4 };   // flags

struct __attribute__((packed)) HHRec {
  uint32_t start;      // period start (epoch seconds)
  uint8_t soc;         // battery % at the end of the period (255 = unknown)
  uint8_t flags;
  uint16_t imp, exp;   // grid import / export, Wh
  uint16_t chg, dis;   // battery charge / discharge, Wh
  uint16_t load, pv;   // home load, solar, Wh
};

// Open the store: two years of half-hours, or fewer if `capBytes` of flash can't hold that.
// The file is preallocated once (first boot) and never grows.
void hhBegin(size_t capBytes);
// One reading: powers in W (all >= 0), battery %, time of the reading.
void hhSample(time_t t, int impW, int expW, int chgW, int disW, int loadW, int pvW, int soc, bool solar);
// The period in progress (flag HH_OPEN). False if none.
bool hhCurrent(HHRec& r);
// Call fn for every stored period starting in [from, to), oldest first, then the open one.
void hhForEach(time_t from, time_t to, void (*fn)(const HHRec&, void*), void* ctx);
// Stored periods and the oldest one's start (0 if none), for the web page.
int hhCount(time_t& oldest);
// How many days the store holds when full (730 = two years).
int hhCapacityDays();
