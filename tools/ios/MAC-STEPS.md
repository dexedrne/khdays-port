# Mac build and launch

Run the steps in order. The native app still needs an Xcode link and a simulator title check. Use an Apple Silicon Mac with Xcode, an installed iOS simulator runtime, CMake 3.25+, Python 3.11+ and pycparser (the Linux run used 3.12 of pycparser). The first Mac configure needs the pinned dependency downloads or their local caches. Set `SDL3_SOURCE` to reuse a local SDL 3.4.12 source checkout.

1. Set `KHDAYS_MAC_HOST=user@host` (and optionally `KHDAYS_MAC_DIR`, default `khdays`) for your Mac, then from the repository root run `tools/ios/sync.sh`. This includes tracked files from decompilation commit `a2b0478`. 
2. On the Mac run `cd ~/khdays/src` (or your `KHDAYS_MAC_DIR`). Set the cartridge path **on that Mac**, for example `export ROM="$HOME/Downloads/games-emulation/358kh.nds"`.
3. Build the simulator app with `tools/ios/mac/build.sh iphonesimulator`. From Linux the equivalent build command is `tools/ios/build.sh iphonesimulator`.
4. Run `xcrun simctl list devices available`, then `export SIM_UDID='<simulator identifier>'` and `tools/ios/mac/sim_run.sh`. The script boots the simulator headlessly, installs the app, places the ROM under Documents and waits for a visible, valid title menu. Once it passes, run `xcrun simctl shutdown "$SIM_UDID"`.
5. Build the device app with `tools/ios/mac/build.sh iphoneos`. Set `export TEAM_ID='<Apple development team ID>'` and run `tools/ios/mac/sign_app.sh`. The local signing identity and provisioning must permit `beer.vyvanse.khdays`.
6. Run `xcrun devicectl list devices`, unlock the paired iPhone, set `export DEVICE_UDID='<device identifier>'` and run `tools/ios/mac/device_run.sh`. This installs the app, puts the ROM in Documents/Kh358.nds and launches the game.

The Linux test reached the visible root menu at frame 806. The simulator script uses that same menu-state check and prints the observed frame.

On the phone check the title, bottom-screen stylus, save/relaunch, audio, controller reconnect and background/resume. Under Settings → Apps → 358/2 Days → Screen layout select stacked, side-by-side, or single screen. Restart the app and check both orientations for each layout.
