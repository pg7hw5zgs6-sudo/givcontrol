#!/bin/sh
# Build / flash GivControl (firmware-c6/) with its own PlatformIO folder, so pioarduino's Arduino core 3.x
# and esptool 5 never replace the packages other PlatformIO projects use.
#   tools/pio-c6.sh                      build
#   tools/pio-c6.sh -t upload --upload-port /dev/cu.usbmodemXXXX     (USB)
#   GIVCONTROL_PIN=yourpin tools/pio-c6.sh -e esp32c6-ota -t upload   (over Wi-Fi, givcontrol-c6.local)
# The firmware file for the web page's Install button is firmware-c6/.pio/build/esp32c6/firmware.bin
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export PLATFORMIO_CORE_DIR="$HOME/.platformio-c6"
cd "$HERE/firmware-c6" || exit 1
case " $* " in *" -e "*) exec "$HERE/.venv/bin/pio" run "$@" ;; esac
exec "$HERE/.venv/bin/pio" run -e esp32c6 "$@"
