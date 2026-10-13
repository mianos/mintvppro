#!/usr/bin/env bash
# Build wrapper: ./build.sh [flash]. Sets the esp32 target on first run.
set -e
set -o pipefail
export IDF_PATH=/Users/robertfowler/.espressif/v6.0.1/esp-idf
. "$IDF_PATH/export.sh" >/dev/null 2>&1
cd "$(dirname "$0")"
PORT=${PORT:-/dev/tty.usbserial-0001}
if [ ! -f sdkconfig ] || ! grep -q 'CONFIG_IDF_TARGET="esp32"' sdkconfig 2>/dev/null; then
    idf.py set-target esp32
fi
idf.py build 2>&1 | tee /tmp/nmtv_clock_build.log | tail -n 40
if [ "$1" = "flash" ]; then
    idf.py -p "$PORT" -b 921600 flash
fi
