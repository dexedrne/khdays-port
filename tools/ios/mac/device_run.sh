#!/bin/bash
set -eu
cd "$(dirname "$0")/../../.."
: "${DEVICE_UDID:?Set DEVICE_UDID to the paired iPhone identifier}"
: "${ROM:?Set ROM to the local cartridge path}"
app=../build/iphoneos/game/Release-iphoneos/khdays-native.app
nice -n 10 python3 tools/verify_rom.py "$ROM"
codesign --verify --strict "$app"
nice -n 10 xcrun devicectl device install app --device "$DEVICE_UDID" "$app"
nice -n 10 xcrun devicectl device copy to --device "$DEVICE_UDID" --domain-type appDataContainer \
    --domain-identifier beer.vyvanse.khdays --source "$ROM" --destination Documents/Kh358.nds
exec nice -n 10 xcrun devicectl device process launch --device "$DEVICE_UDID" --terminate-existing beer.vyvanse.khdays
