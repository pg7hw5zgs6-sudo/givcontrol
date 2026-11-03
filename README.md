# GivControl

A local, cloud-free controller for GivEnergy inverters and batteries, running on a small ESP32-C6 board,
for owners left without the GivEnergy portal. It talks to your inverter's Wi-Fi dongle over your own
network and gives you a web page with GivTCP-style controls: power flow, eco mode, charge and discharge
slots, charge/discharge rates, reserve, pause, charge now and clock sync, plus two years of half-hourly
energy history.

Open it at `http://givcontrol-c6.local/` or `http://<board IP>/`.

> **Use at your own risk.** GivControl writes settings directly to your inverter. A wrong value can stop
> your battery charging or discharging as you expect. It is an independent community project and is not
> affiliated with or endorsed by GivEnergy. Settings are only marked "verified" where they have been
> tested on a real unit (see below); treat everything else as untested.
>
> **Only for the original All-in-One (V1).** GivControl has been tested only on a first-generation
> GivEnergy All-in-One. Later All-in-One versions and other GivEnergy inverters are not supported.
>
> **No warranty, no support.** This software is provided "as is", without warranty of any kind, express or
> implied (see sections 15 and 16 of the [licence](LICENSE)). The author accepts no liability for any
> damage, loss, cost or warranty issue with your inverter, battery or electricity supply arising from its
> use. There is no support: issues and pull requests are welcome but may not be answered.

## What you need

- An **ESP32-C6 dev board with 4 MB flash** (tested on an ESP32-C6FH4 board using Espressif's
  `esp32-c6-devkitc-1` definition). The onboard RGB LED on GPIO8 shows status.
- A **GivEnergy All-in-One V1** (first generation) with its Wi-Fi/LAN dongle on the same network (TCP
  port 8899). Other models and later AIO versions are not supported.
- A computer with Python 3 and a USB cable for the first flash.

## First setup

1. **Flash the board** (once, over USB):
   ```bash
   python3 -m venv .venv && .venv/bin/pip install platformio==6.1.18 "click<8.2"
   tools/pio-c6.sh -t upload --upload-port <your serial port>
   ```
   The port looks like `/dev/cu.usbmodemXXXX` (macOS), `/dev/ttyACM0` (Linux) or `COM3` (Windows).
   If it won't connect, hold **BOOT** while pressing **RESET**, then try again.
2. **Wi-Fi**: on your phone, join the Wi-Fi network **GivControl-Setup**. Pick your home Wi-Fi on the page
   that opens (or go to `http://192.168.4.1`) and enter its password. The dongle address can stay blank.
3. **Finding the inverter**: the board scans your network for GivEnergy dongles and checks each one with a
   harmless read. If it finds one it uses it; if it finds several (for example an inverter and a Gateway)
   you pick one in Settings. You can also type the address in Settings > GivEnergy dongle.
4. **Changing settings**: controls start locked. In Settings > Controls, tick the box to confirm nothing
   else (GivTCP, Home Assistant, another app) is changing the battery settings, and unlock. Then change a
   value and press **Save**: a ✓ or ✗ appears next to it once the inverter has been read back. **Undo last
   change** puts back what the last change replaced. Lock again from the same place.

Status LED: blue = no Wi-Fi / setting up, green = OK, magenta = writing, red = inverter fault or battery
stopped.

## What works on which inverter

The model is read from the inverter and shown on the dashboard.

| Model | Readings | Settings you can change |
|---|---|---|
| All-in-One V1 | yes | **Verified:** eco mode, AC charge, charge slot 1, discharge slot 1, charge and discharge rate, reserve, pause / charge now, clock sync, serial restore. **Untested** (marked, but editable once unlocked): timed export, charge/discharge slot 2, charge target, export limit, pause mode, extra slots 1-10 |
| Hybrid, AC-coupled (single phase) | yes (same register map, not checked on a real unit) | none yet: read only |
| 3-phase, EMS, Gateway | not supported (different register map) | none |

If you test a setting on your own inverter, please open an issue with the model, firmware and result.

## Built-in protection

- The unlock tick is the only step you see. Behind it:
- **Never written:** HR163 (software restart). On an All-in-One it wiped the serial, charge slots and
  limits and left the battery discharging overnight. Anything not on the list for your model is also refused.
- **Valid values only:** times must be real HH:MM, numbers stay within each setting's range.
- **One register at a time**, about a second apart, each read back and retried up to 3 times.
- **Change log** of every write (time, old, new, result) on the board, downloadable as CSV, plus an
  events list that survives restarts.

## Solar

Solar is read directly from the inverter's PV inputs (PV1/PV2 power and today's PV energy). The Solar tile
appears the first time generation is seen and stays after that; Settings > Solar can also set it to
Always show or Hide. Systems without solar never see it.

## Half-hour history (two years)

Energy is recorded per half-hour, aligned to :00 and :30 like smart-meter and tariff periods: grid import
and export, battery charge and discharge, home and solar in Wh, plus battery % at the end of each period.
Each minute's reading is integrated into the open half-hour; a half-hour with under 25 minutes of readings
is marked incomplete.

- Kept for **exactly two years**, then the oldest is overwritten: one fixed 616 KB file that never grows.
- The web page shows Day, Week and Month views with totals, and how much history is stored.
- `GET /api/halfhours?from=&to=[&agg=day]`, and `/halfhours.csv` for comparing with a supplier bill.

## Firmware updates over Wi-Fi

Settings > Firmware shows the version and installs a new `.bin` without a USB cable.
- Set an **update PIN** first (6+ characters, there is no default). Without one, network updates are off.
- The image is checked before it is accepted: built for this chip, fits the slot, checksum and SHA-256.
- **Rollback:** new firmware is kept only once Wi-Fi is up and the inverter has answered within 2 minutes;
  otherwise the previous version boots again.
- Developers: `GIVCONTROL_PIN=yourpin tools/pio-c6.sh -e esp32c6-ota -t upload` installs over Wi-Fi.
  The file for the web page is `firmware-c6/.pio/build/esp32c6/firmware.bin`.

## Flash layout (`firmware-c6/partitions_givcontrol.csv`)

| Partition | Offset | Size | |
|---|---|---|---|
| nvs | 0x9000 | 20 KB | Wi-Fi, dongle, settings, saved serial |
| otadata | 0xE000 | 8 KB | |
| app0 / app1 | 0x10000 / 0x190000 | 1.5 MB each | firmware is about 1.25 MB |
| spiffs | 0x310000 | 960 KB | history, events, change log |

The layout can only be changed over USB. The first USB flash onto a board with a different layout clears
the data area once; Wi-Fi and settings are kept.

## Inverter serial restore

Some inverters lose their serial (it reads XXXXXXXXXX) after a full restart. GivControl saves the serial
the first time it reads a real one, per dongle. "Restore serial" appears only when the serial is lost
**and** a saved one exists for that dongle, and it writes back exactly that value.

## Building (developers)

The C6 needs Arduino core 3.x, so `firmware-c6/platformio.ini` uses a pinned pioarduino release
(54.03.21, core 3.2.1). `tools/pio-c6.sh` builds with its own PlatformIO folder (`~/.platformio-c6`)
so it doesn't disturb other PlatformIO projects.

```bash
tools/pio-c6.sh                                         # build
tools/pio-c6.sh -t upload --upload-port <port>          # flash over USB
```

To add a register for a model: read it, change the setting with another tool and watch which register
changes, then add it to the model's table (`AIO_REGS`) in `firmware-c6/src/giv.cpp` and to
`curValue` / `fmtValue` and `REGS` in `main.cpp`. `tools/verify_hr27.py` and
`tools/verify_discharge_slot1.py` show how a single register was tested from a computer with read-back.

## Protocol notes

GivEnergy's dongle (TCP 8899) wraps Modbus RTU in its own framing; the inverter answers as slave 0x11.
Reads use function 3 (holding) and 4 (input) in blocks of 60; writes use function 6, one register at a
time. The dongle relays everyone's traffic (the cloud, GivTCP, Home Assistant), so replies are matched on
slave, function and base register rather than request order, and garbled all-zero blocks are discarded.

## Future work

- **All-in-One + Gateway:** a Gateway may hold the grid, home and solar figures for an AIO. It has its own
  register map; reading it (never writing) is future work.
- Verifying more settings on Hybrid and AC-coupled models.

## Licence

GivControl is free software under the [GNU General Public License v3.0](LICENSE): you can use, change and
share it, and if you distribute a modified version you must publish its source under the same licence.
It comes with no warranty.

The Noto Sans fonts in `tools/fonts/` (and the font arrays generated from them) are under the
[SIL Open Font License 1.1](tools/fonts/OFL.txt).
