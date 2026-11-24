#!/usr/bin/env bash
# Change the partition table over Wi-Fi, no cable. Run on esp-builder:
#
#     ./repartition.sh
#
# This only works because nvs, otadata and the ota_0 offset stay put: the new image
# is pushed into ota_0 first and the device reboots into it, then the table is
# written, then it reboots again. The endpoint verifies what it wrote and puts the
# old table back if it does not match, so every step can be retried without a cable.
set -euo pipefail
cd "$(dirname "$0")"

HOST=${HOST:-nmtv-clock.mianos.com}
VERSION=$(git rev-parse --short HEAD)

layout() { curl -fsS -m 10 "http://$HOST/repartition"; }
health() { curl -fsS -m 10 "http://$HOST/healthz"; }

# The endpoint only exists in images built after it: an older one answers 404,
# which is fine, it just cannot be on the new layout yet.
if before=$(layout 2>/dev/null); then
    echo "current layout:"
    printf '%s\n' "$before"
    if printf '%s\n' "$before" | awk '$1 == "ota_0" {print $NF}' | grep -qx 0x1f0000; then
        echo "already on the new layout"
        exit 0
    fi
else
    echo "no /repartition endpoint yet, the running image predates it"
fi

./build.sh

# The image has to end up in the slot whose offset does not move. A push goes to
# whichever slot is not running, so if it lands in ota_1 the clock is switched off
# and on by pushing once more; two pushes always reach ota_0.
echo "1/3 getting $VERSION running from ota_0, the slot that does not move"
part=""
for _ in 1 2 3; do
    curl -fsS -m 180 --data-binary @build/mintvpro.bin "http://$HOST/firmware"
    echo
    for _ in $(seq 1 45); do
        sleep 2
        state=$(health 2>/dev/null || true)
        if printf '%s' "$state" | grep -q "\"version\":\"$VERSION\""; then break; fi
    done
    part=$(printf '%s' "$state" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("partition","?"))' 2>/dev/null || true)
    [ "$part" = "ota_0" ] && break
    echo "landed in ${part:-nowhere}, pushing again to reach ota_0"
done
[ "$part" = "ota_0" ] || { echo "could not get the image into ota_0" >&2; exit 1; }

echo "2/3 $VERSION is running from ota_0"

echo "3/3 writing the table"
curl -fsS -m 30 --data-binary @build/partition_table/partition-table.bin "http://$HOST/repartition"
echo

sleep 12
echo "layout now:"
layout
health
