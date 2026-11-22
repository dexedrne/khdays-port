#!/bin/bash
# device_log.sh [SECONDS]: launch the installed app on the phone with its console captured for SECONDS
# (default 60), then fetch the picture reports' PNGs (Documents/saves/logs) from the app's container.
# Each run records console.txt and logs/ under OUT or a timestamped folder in ../phone-logs.
set -eu
cd "$(dirname "$0")/../../.."
: "${DEVICE_UDID:?Set DEVICE_UDID to the paired iPhone identifier}"
seconds=${1:-60}
out=${OUT:-../phone-logs/launch-$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out"
options=()
if [[ "${MOVIE_TEST:-0}" == 1 ]]; then
    options=(--environment-variables '{"KHDAYS_MOVIE_TEST":"1"}')
fi
nice -n 10 xcrun devicectl device process launch --device "$DEVICE_UDID" --terminate-existing --console \
    ${options[@]+"${options[@]}"} beer.vyvanse.khdays > "$out/console.txt" 2>&1 &
launch=$!
for ((i=0; i<seconds; ++i)); do
    kill -0 "$launch" 2>/dev/null || break
    sleep 1
done
kill "$launch" 2>/dev/null || true
wait "$launch" 2>/dev/null || true
nice -n 10 xcrun devicectl device copy from --device "$DEVICE_UDID" --domain-type appDataContainer \
    --domain-identifier beer.vyvanse.khdays --source "${APP_LOGS:-Documents/saves/logs}" --destination "$out/logs" ||
    echo "The pictures could not be fetched; the console is in $out/console.txt" >&2
echo "Phone console: $out/console.txt"
grep -q 'picture [0-9].*read back' "$out/console.txt" || { tail -30 "$out/console.txt"; exit 1; }
grep -E '^(KHDAYS pad log:|pace:|shaders:|picture|ios:|window:|host:|movies:|scenes:|lower view:)' "$out/console.txt" | cut -c1-400 || true
