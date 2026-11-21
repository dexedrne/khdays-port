#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
sdk=${1:-iphonesimulator}
case "$sdk" in iphoneos|iphonesimulator) ;; *) echo "Expected iphoneos or iphonesimulator" >&2; exit 2;; esac
build="$PWD/../build/$sdk"
args=(-G Xcode -DCMAKE_SYSTEM_NAME=iOS "-DCMAKE_OSX_SYSROOT=$sdk" -DCMAKE_OSX_ARCHITECTURES=arm64
      -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO)
if [ -n "${SDL3_SOURCE:-}" ]; then args+=("-DFETCHCONTENT_SOURCE_DIR_SDL3=$SDL3_SOURCE"); fi
jobs=2
[ -f external/khdays-decomp/include/nitro/hw.h ] || {
    echo "Sync the pinned decompilation before building." >&2
    exit 1
}
gen="$PWD/../build/generated"
nice -n 10 python3 port/tools/prepare.py --out "$gen"
nice -n 10 cmake -S port -B "$build/game" "${args[@]}" "-DKHDAYS_GEN=$gen" -DKHDAYS_NO_PAD_ICONS=ON
nice -n 10 cmake --build "$build/game" --config Release --target khdays-native --parallel "$jobs"
test -x "$build/game/Release-$sdk/khdays-native.app/khdays-native"
