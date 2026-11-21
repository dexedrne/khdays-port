#!/bin/bash
# device_log.sh [SECONDS]: launch the installed app on the phone with its console captured for SECONDS
# (default 60), then fetch the picture reports' PNGs (Documents/saves/logs) from the app's container.
# Output: ${OUT:-../phone-logs}/console.txt and logs/. Headless; opens no window.
set -eu
cd "$(dirname "$0")/../../.."
: "${DEVICE_UDID:?Set DEVICE_UDID to the paired iPhone identifier}"
seconds=${1:-60}
out=${OUT:-../phone-logs}
mkdir -p "$out"
nice -n 10 xcrun devicectl device process launch --device "$DEVICE_UDID" --terminate-existing --console \
    beer.vyvanse.khdays > "$out/console.txt" 2>&1 &
launch=$!
sleep "$seconds"
kill "$launch" 2>/dev/null || true
wait "$launch" 2>/dev/null || true
nice -n 10 xcrun devicectl device copy from --device "$DEVICE_UDID" --domain-type appDataContainer \
    --domain-identifier beer.vyvanse.khdays --source Documents/saves/logs --destination "$out/logs" ||
    echo "The pictures could not be fetched; the console is in $out/console.txt" >&2
grep -E '^(picture|ios:|window:|host:)' "$out/console.txt" | cut -c1-400 || true
