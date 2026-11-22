#!/bin/bash
# Capture New Game's opening with a clean, isolated save folder.
set -euo pipefail
cd "$(dirname "$0")/../../.."
: "${DEVICE_UDID:?Set DEVICE_UDID to the paired iPhone identifier}"
mkdir -p ../phone-logs
out=$(mktemp -d ../phone-logs/movie.XXXXXX)
domain=(--device "$DEVICE_UDID" --domain-type appDataContainer --domain-identifier beer.vyvanse.khdays)
copy() { nice -n 10 xcrun devicectl device copy "$1" "${domain[@]}" --source "$2" --destination "$3"; }
copy from Documents/saves/khdays.sav "$out/owner-before.sav"
test -s "$out/owner-before.sav"
cat > "$out/environment.txt" <<EOF
KHDAYS_USER_FOLDER=saves/$(basename "$out")
KHDAYS_TRACE_MOVIE_HD=1
KHDAYS_PAD_SCRIPT=0*ps5 t 130+south 138-south 330+south 338-south 530+south 538-south 680+dpleft 688-dpleft 740+south 748-south
EOF
copy to "$out/environment.txt" Documents/khdays-movie-test.txt
OUT="$out" APP_LOGS="Documents/saves/$(basename "$out")/logs" MOVIE_TEST=1 tools/ios/mac/device_log.sh "${CAPTURE_SECONDS:-420}"
copy from Documents/saves/khdays.sav "$out/owner-after.sav"
cmp "$out/owner-before.sav" "$out/owner-after.sav"
grep -F "the save, the settings and the logs are in saves/$(basename "$out")" "$out/console.txt"
for event in open start end; do grep -E "movies: $event .*hd802[.]mp4" "$out/console.txt"; done
