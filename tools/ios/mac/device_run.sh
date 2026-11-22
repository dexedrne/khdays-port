#!/bin/bash
set -eu
cd "$(dirname "$0")/../../.."
: "${DEVICE_UDID:?Set DEVICE_UDID to the paired iPhone identifier}"
: "${ROM:?Set ROM to the local cartridge path}"
app=${SIGNED_APP:-../build/iphoneos/game-ninja/khdays-native.app}
nice -n 10 python3 tools/verify_rom.py "$ROM"
codesign --verify --strict "$app"
nice -n 10 xcrun devicectl device install app --device "$DEVICE_UDID" "$app"
nice -n 10 xcrun devicectl device copy to --device "$DEVICE_UDID" --domain-type appDataContainer \
    --domain-identifier beer.vyvanse.khdays --source "$ROM" --destination Documents/Kh358.nds
exec tools/ios/mac/device_log.sh "${CAPTURE_SECONDS:-60}"
