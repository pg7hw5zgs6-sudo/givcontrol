#!/usr/bin/env python3
"""Verify HR27 (eco mode, GivTCP map) on the AIO: one write, read back, watch the battery.

    python3 tools/verify_hr27.py DONGLE_IP watch 120     # just watch battery/grid for 120 s
    python3 tools/verify_hr27.py DONGLE_IP set 0 180     # write HR27=0, read back, watch 180 s
    python3 tools/verify_hr27.py DONGLE_IP set 1 120     # put it back

Can only ever write register 27 with 0 or 1. Read back and retried up to 3 times.
"""
import socket
import struct
import sys
import time

HOST, PORT, INV = "", 8899, 0x11
REG, ALLOWED = 27, (0, 1)


def crc16(d):
    c = 0xFFFF
    for b in d:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


def frame(func, reg, val):
    inner = struct.pack(">BBHH", INV, func, reg, val)
    body = bytes([1, 2]) + b"AB1234G567" + struct.pack(">Q", 8) + inner + struct.pack("<H", crc16(inner))
    return struct.pack(">HHH", 0x5959, 1, len(body)) + body


def read_blocks(wanted, wait=4.0):
    """wanted: [(func, base)] -> {(func, base): [60 regs]} for slave 0x11."""
    got = {}
    with socket.create_connection((HOST, PORT), timeout=5) as s:
        s.settimeout(0.3)
        buf = b""
        for _ in range(3):
            for func, base in wanted:
                if (func, base) in got:
                    continue
                s.sendall(frame(func, base, 60))
                end = time.time() + wait
                while time.time() < end and (func, base) not in got:
                    try:
                        buf += s.recv(4096)
                    except socket.timeout:
                        continue
                    while len(buf) >= 6:
                        if buf[:2] != b"\x59\x59":
                            i = buf.find(b"\x59\x59", 1); buf = buf[i:] if i > 0 else b""; continue
                        n = struct.unpack(">H", buf[4:6])[0]
                        if len(buf) < 6 + n:
                            break
                        f, buf = buf[:6 + n], buf[6 + n:]
                        p = f[8:]
                        if len(f) < 42 or f[7] != 2 or p[18] != INV or p[19] not in (3, 4):
                            continue
                        b, cnt = struct.unpack(">HH", p[30:34])
                        if cnt == 60 and len(p) >= 34 + 120:
                            regs = list(struct.unpack(">60H", p[34:154]))
                            if not (p[19] == 3 and b == 0 and regs[19] == 0 and regs[21] == 0):   # garbled HR0
                                got[(p[19], b)] = regs
                time.sleep(0.15)
    return got


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def snapshot():
    g = read_blocks([(3, 0), (4, 0)])
    hr, ir = g.get((3, 0)), g.get((4, 0))
    return (hr[27] if hr else None, hr[59] if hr else None,
            s16(ir[52]) if ir else None, s16(ir[30]) if ir else None, ir[42] if ir else None, ir[59] if ir else None)


def watch(seconds):
    end = time.time() + seconds
    while time.time() < end:
        eco, timed, bw, gw, load, soc = snapshot()
        print(f"{time.strftime('%H:%M:%S')}  HR27={eco} HR59={timed}  battery={bw} W (+=discharge)  grid={gw} W  load={load} W  soc={soc}%", flush=True)
        time.sleep(15)


def write(value):
    assert value in ALLOWED, "HR27 may only be 0 or 1"
    for attempt in range(1, 4):
        with socket.create_connection((HOST, PORT), timeout=5) as s:
            s.sendall(frame(6, REG, value))
            time.sleep(2.0)
        hr = read_blocks([(3, 0)]).get((3, 0))
        got = hr[REG] if hr else None
        print(f"{time.strftime('%H:%M:%S')}  write HR27={value} attempt {attempt}: read back {got}", flush=True)
        if got == value:
            return True
        time.sleep(1.5)
    return False


if __name__ == "__main__":
    HOST, cmd = sys.argv[1], sys.argv[2]
    if cmd == "watch":
        watch(int(sys.argv[3]))
    elif cmd == "set":
        ok = write(int(sys.argv[3]))
        print("RESULT", "ok" if ok else "FAILED", flush=True)
        if ok:
            watch(int(sys.argv[4]))
