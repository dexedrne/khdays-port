#!/bin/bash
set -eu
cd "$(dirname "$0")/../../.."
: "${SIM_UDID:?Set SIM_UDID to a dedicated simulator identifier}"
: "${ROM:?Set ROM to the local cartridge path}"
app=../build/iphonesimulator/game-ninja/khdays-native.app
test -x "$app/khdays-native"
nice -n 10 python3 tools/verify_rom.py "$ROM"
ctl() { nice -n 10 xcrun simctl "$@"; }
ctl bootstatus "$SIM_UDID" -b
ctl install "$SIM_UDID" "$app"
data=$(ctl get_app_container "$SIM_UDID" beer.vyvanse.khdays data)
mkdir -p "$data/Documents"
cp "$ROM" "$data/Documents/Kh358.nds.new"
mv -f "$data/Documents/Kh358.nds.new" "$data/Documents/Kh358.nds"
log=$(mktemp "$PWD/../build/sim-title.XXXXXX")
export SIMCTL_CHILD_KHDAYS_TRACE_SITUATION=1
export SIMCTL_CHILD_SDL_AUDIODRIVER=dummy
ctl launch --terminate-running-process --stderr="$log" "$SIM_UDID" beer.vyvanse.khdays
for ((i=0;i<180;i++)); do
    if python3 tools/ios/test_title.py --check-log "$log"; then exit 0; fi
    sleep 1
done
echo "Simulator title menu unconfirmed. Log: $log" >&2
exit 1
