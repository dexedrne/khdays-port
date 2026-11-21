#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
sdk=${1:-iphonesimulator}
case "$sdk" in iphoneos|iphonesimulator) ;; *) echo "Expected iphoneos or iphonesimulator" >&2; exit 2;; esac
build="$PWD/../build/$sdk"
args=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS "-DCMAKE_OSX_SYSROOT=$sdk" -DCMAKE_OSX_ARCHITECTURES=arm64
      -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO)
if [ -n "${SDL3_SOURCE:-}" ]; then args+=("-DFETCHCONTENT_SOURCE_DIR_SDL3=$SDL3_SOURCE"); fi
jobs=2
[ -f external/khdays-decomp/include/nitro/hw.h ] || {
    echo "Sync the pinned decompilation before building." >&2
    exit 1
}
gen="$PWD/../build/generated"
nice -n 10 python3 port/tools/prepare.py --out "$gen"
nice -n 10 cmake -S port -B "$build/game-ninja" "${args[@]}" "-DKHDAYS_GEN=$gen" -DKHDAYS_NO_PAD_ICONS=ON
nice -n 10 cmake --build "$build/game-ninja" --config Release --target khdays-native --parallel "$jobs"
test -x "$build/game-ninja/khdays-native.app/khdays-native"
app="$build/game-ninja/khdays-native.app"
# App icon: compile the asset catalog into the bundle and merge the icon keys into its Info.plist.
xcrun actool tools/ios/Assets.xcassets --compile "$app" --platform "$sdk" --minimum-deployment-target 16.0 \
    --app-icon AppIcon --target-device iphone --target-device ipad --output-partial-info-plist "$build/icon-info.plist" >/dev/null
/usr/libexec/PlistBuddy -c "Merge $build/icon-info.plist" "$app/Info.plist"
test -f "$app/Assets.car"
