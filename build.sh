#!/usr/bin/env bash
# Build wrapper: ./build.sh [flash|monitor]. Sets the esp32 target on first run.
#
# The build belongs on esp-builder. The Mac still has an IDF checkout, but it is a
# different version and its fans are unpopular, so the path is resolved per host.
set -e
set -o pipefail

case "$(uname -s)" in
    Darwin) IDF_PATH=${IDF_PATH:-$HOME/.espressif/v6.0.1/esp-idf}
            echo "note: building on the Mac; esp-builder is the build host" >&2 ;;
    *)      IDF_PATH=${IDF_PATH:-$HOME/esp-idf} ;;
esac
export IDF_PATH
# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >/dev/null 2>&1
cd "$(dirname "$0")"

if [ -z "${PORT:-}" ]; then
    PORT=$(ls /dev/cu.usbserial* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1 || true)
fi

if [ ! -f sdkconfig ] || ! grep -q 'CONFIG_IDF_TARGET="esp32"' sdkconfig 2>/dev/null; then
    idf.py set-target esp32
fi
idf.py build 2>&1 | tee /tmp/nmtv-clock_build.log | tail -n 40

case "$1" in
    flash)
        [ -n "$PORT" ] || { echo "no serial port found; set PORT=" >&2; exit 1; }
        idf.py -p "$PORT" -b 921600 flash ;;
    monitor)
        [ -n "$PORT" ] || { echo "no serial port found; set PORT=" >&2; exit 1; }
        idf.py -p "$PORT" monitor ;;
esac
