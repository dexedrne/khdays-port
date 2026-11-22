#!/bin/bash
# device_videos.sh: put the player's own HD videos on the phone, where the app looks for them:
# Documents/data/cutscenes/cinematics/hd<movie>.mp4 in the app's container. The app must be installed
# (device_run.sh) first. The videos are private: VIDEOS is a folder outside this source tree, they are
# never copied into the tree or into the .app, and nothing here is committed.
#   export DEVICE_UDID='<device identifier>' VIDEOS="$HOME/khdays-videos"; tools/ios/mac/device_videos.sh
# One file at a time (the form device_run.sh uses for the cartridge), each said on the console; a file
# that does not go over is named and the script ends with status 1 after trying the rest.
# Headless and niced; opens no window. UNTESTED: written on Linux. If devicectl does not make the
# folders of the destination itself, make them once in the Files app (On My iPhone > 358/2 Days >
# data > cutscenes > cinematics) and run it again, or drop the files there in the Files app.
set -eu
cd "$(dirname "$0")/../../.."
: "${DEVICE_UDID:?Set DEVICE_UDID to the paired iPhone identifier}"
: "${VIDEOS:?Set VIDEOS to the folder with the hd*.mp4 files (outside the source tree)}"
tree=$(pwd -P)
if [ ! -d "$VIDEOS" ]; then
    echo "device_videos: FAILED: VIDEOS=$VIDEOS is not a folder" >&2
    exit 1
fi
videos=$(cd "$VIDEOS" && pwd -P)
case "$videos/" in
"$tree"/*)
    echo "device_videos: FAILED: $videos is inside the source tree; keep the videos outside it" >&2
    exit 1
    ;;
esac
count=0
failed=0
for file in "$videos"/hd*.mp4; do
    [ -f "$file" ] || continue
    name=$(basename "$file")
    count=$((count + 1))
    if [ ! -s "$file" ]; then
        echo "device_videos: FAILED: $name is empty (still being copied to this Mac?)" >&2
        failed=$((failed + 1))
        continue
    fi
    if nice -n 10 xcrun devicectl device copy to --device "$DEVICE_UDID" --domain-type appDataContainer \
        --domain-identifier beer.vyvanse.khdays --source "$file" \
        --destination "Documents/data/cutscenes/cinematics/$name"; then
        echo "device_videos: $name copied"
    else
        echo "device_videos: FAILED: $name was not copied (devicectl's reason is above)" >&2
        failed=$((failed + 1))
    fi
done
if [ "$count" -eq 0 ]; then
    echo "device_videos: FAILED: no hd*.mp4 in $videos" >&2
    exit 1
fi
echo "device_videos: $((count - failed)) of $count videos copied to Documents/data/cutscenes/cinematics"
echo "device_videos: the app's start line 'movies: HD videos looked for ... the game's movies with theirs N of 42' says what it found (device_log.sh)"
[ "$failed" -eq 0 ]
