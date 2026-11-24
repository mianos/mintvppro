#!/usr/bin/env bash
# Build and push the image to the clock, then report what it came back as. Run it
# on esp-builder:
#
#     ./deploy.sh                  # nmtv-clock.mianos.com
#     HOST=other ./deploy.sh
#     SKIP_BUILD=1 ./deploy.sh     # push the image already in build/
#
# The clock is told to pull nothing: the image is POSTed to /firmware, so a failure
# shows up here rather than as a silent no-op on the device.
set -euo pipefail
cd "$(dirname "$0")"

HOST=${HOST:-nmtv-clock.mianos.com}
[ "${SKIP_BUILD:-0}" = 1 ] || ./build.sh

echo "deploying $(git rev-parse --short HEAD) to $HOST"
curl -fsS -m 180 --data-binary @build/mintvpro.bin "http://$HOST/firmware"
echo
sleep 12
curl -fsS -m 10 "http://$HOST/healthz"
echo
