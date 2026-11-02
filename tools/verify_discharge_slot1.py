#!/usr/bin/env python3
"""Verify discharge slot 1 (HR56 start, HR57 end, GivTCP map) on the AIO, with timed export (HR59) off.

    python3 tools/verify_discharge_slot1.py DONGLE_IP test 30   # slot = now .. now+30 min, watch, restore
    python3 tools/verify_discharge_slot1.py DONGLE_IP restore   # put 00:00-00:00 back

Can only write HR56/HR57 with valid HH:MM values, and refuses unless HR59 reads 0 (so the slot has no
effect). Each write is read back and retried up to 3 times. Reuses the framing in verify_hr27.py.
"""
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import verify_hr27 as g   # frame / read_blocks / s16 (it never writes on import)

ALLOWED = (56, 57)


def write(reg, value):
    assert reg in ALLOWED, "only HR56/HR57"
    assert 0 <= value <= 2359 and value % 100 < 60, "not a valid HH:MM"
    import socket
    for attempt in range(1, 4):
        with socket.create_connection((g.HOST, g.PORT), timeout=5) as s:
            s.sendall(g.frame(6, reg, value))
            time.sleep(2.0)
        hr = g.read_blocks([(3, 0)]).get((3, 0))
        got = hr[reg] if hr else None
        print(f"{time.strftime('%H:%M:%S')}  write HR{reg}={value:04d} attempt {attempt}: read back {got}", flush=True)
        if got == value:
            return True
        time.sleep(1.5)
    return False


def snap():
    b = g.read_blocks([(3, 0), (4, 0)])
    hr, ir = b.get((3, 0)), b.get((4, 0))
    return (hr[56] if hr else None, hr[57] if hr else None, hr[59] if hr else None, hr[27] if hr else None,
            g.s16(ir[52]) if ir else None, g.s16(ir[30]) if ir else None, ir[59] if ir else None)


def watch(seconds, label):
    end, rows = time.time() + seconds, []
    while time.time() < end:
        r = snap()
        rows.append(r)
        print(f"{time.strftime('%H:%M:%S')}  [{label}] slot={r[0]}-{r[1]} HR59={r[2]} eco={r[3]}  battery={r[4]} W (+=discharge)  grid={r[5]} W (-=import)  soc={r[6]}%", flush=True)
        time.sleep(20)
    return rows


if __name__ == "__main__":
    g.HOST, cmd = sys.argv[1], sys.argv[2]
    if cmd == "restore":
        ok = write(56, 0) and write(57, 0)
        print("RESTORE", "ok" if ok else "FAILED")
        sys.exit(0 if ok else 1)
    mins = int(sys.argv[3])
    s0 = snap()
    if s0[2] != 0:
        sys.exit(f"HR59 (timed export) reads {s0[2]}, not 0: not testing")
    if (s0[0], s0[1]) != (0, 0):
        sys.exit(f"discharge slot 1 already set ({s0[0]}-{s0[1]}): not touching it")
    base = watch(60, "before")
    t = time.localtime()
    start = t.tm_hour * 100 + t.tm_min
    e = (t.tm_hour * 60 + t.tm_min + mins) % 1440
    end = e // 60 * 100 + e % 60
    ok = write(56, start) and write(57, end)
    print("WRITE", "ok" if ok else "FAILED", f"slot {start:04d}-{end:04d}", flush=True)
    after = watch(240, "slot set") if ok else []
    rok = write(56, 0) and write(57, 0)
    print("RESTORE", "ok" if rok else "FAILED", flush=True)
    final = snap()
    print(f"final: slot={final[0]}-{final[1]} HR59={final[2]} eco={final[3]}", flush=True)
    bat = lambda rows: [r[4] for r in rows if r[4] is not None]
    if after:
        print(f"battery W before: {bat(base)}  with slot: {bat(after)}", flush=True)
        forced = any(w > 1500 for w in bat(after)) and not any(w > 1500 for w in bat(base))
        print("NO FORCED DISCHARGE (as expected with timed export off)" if not forced else "BATTERY BEHAVIOUR CHANGED", flush=True)
