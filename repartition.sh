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

echo "1/3 pushing $VERSION into ota_0, the slot that does not move"
curl -fsS -m 180 --data-binary @build/mintvpro.bin "http://$HOST/firmware"
echo

echo "2/3 waiting for $VERSION to be the running image"
for _ in $(seq 1 60); do
    sleep 2
    if health 2>/dev/null | grep -q "\"version\":\"$VERSION\""; then break; fi
done
health | grep -q "\"version\":\"$VERSION\"" || { echo "device did not come back on $VERSION" >&2; exit 1; }

echo "3/3 writing the table"
curl -fsS -m 30 --data-binary @build/partition_table/partition-table.bin "http://$HOST/repartition"
echo

sleep 12
echo "layout now:"
layout
health
