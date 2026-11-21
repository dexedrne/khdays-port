# Mac build and launch

Run the steps in order on an Apple Silicon Mac with Xcode, CMake 3.25+, Ninja, Python 3.11+ and pycparser. The first configure needs the pinned dependency downloads or their local caches. Set `SDL3_SOURCE` to reuse a local SDL 3.4.12 source checkout. Every script is headless and niced and opens no window.

The iOS simulator cannot run the game: its Metal lacks features the graphics path needs. The simulator build is only a compile and link check. The picture and the controls are checked on Linux (`ctest -R 'khdays-(picture|pad)'`) and on the phone.

1. Set `KHDAYS_MAC_HOST=user@host` and optionally `KHDAYS_MAC_DIR` (default `khdays`). From the repository root run `tools/ios/sync.sh`. This sends the tracked files, the new port sources and decompilation commit `a2b0478`.
2. On the Mac run `cd ~/khdays/src` (or your `KHDAYS_MAC_DIR`). Set the cartridge path on that Mac, for example `export ROM="$HOME/Downloads/games-emulation/358kh.nds"`.
3. Optional link check: `tools/ios/mac/build.sh iphonesimulator`.
4. Build the device app with `tools/ios/mac/build.sh iphoneos`. Set `export TEAM_ID='<Apple development team ID>'` and run `tools/ios/mac/sign_app.sh`. The signing identity and provisioning must permit `beer.vyvanse.khdays`.
5. Run `xcrun devicectl list devices`, unlock the paired iPhone, set `export DEVICE_UDID='<device identifier>'` and run `tools/ios/mac/device_run.sh`. This installs the app, puts the ROM in Documents/Kh358.nds and launches the game.
6. Run `tools/ios/mac/device_log.sh 60`. It relaunches the app with its console captured for 60 seconds, writes `../phone-logs/console.txt`, fetches the saved pictures into `../phone-logs/logs/` and prints the report lines.

## Reading the phone's console

The window reports pictures 1, 30, 300, 900 and 1800, and the second picture after each change of size or safe area.

| Line | Meaning |
| --- | --- |
| `picture N (game frame F, scene S): window ... drawn as ...` | Window size in points and pixels, safe area, window flags, the layout used, the viewport and the safe-area fit. |
| `picture N: WxH read back, P% lit, within x,y to x,y` | The picture the renderer is about to show. On Linux the title is about 50% lit at picture 300. |
| `picture N: ... ALL BLACK` | The renderer's picture is empty: the fault is in the port's drawing or the Metal shaders. |
| `picture N: touch keys drawn / not drawn` | Whether the on-screen keys were drawn, and why not (a controller, the settings menu). |
| `ios: picture N: app state ...` and the `ios:   window ...` lines | Every UIKit window with its level, visibility, view and Metal drawable size. A lit picture with a black screen is explained here. |
| `window: the app: ...` and `window: event ...` | Background and foreground changes with the window flags (40 = minimized: SDL draws nothing), and the window's own events. |
| `picture N: the window did not take it` | SDL refused to present, with its reason. |
| `situation: frame F, title ...` | The game's own title state, as in the Linux title test. |

The saved pictures are `Documents/saves/logs/picture-NNNNN.png`, at most 1024 pixels wide.

To switch testing options on without a rebuild, put `khdays-env.txt` in the app's Documents folder with one `NAME=VALUE` per line. Useful names: `KHDAYS_PICTURE_LOG=60` (a report every 60 pictures), `KHDAYS_GPU_DEBUG=1`, `KHDAYS_PROFILE=1`, `KHDAYS_TRACE_SITUATION=` (an empty value switches the title trace off).

## What to check on the phone

Linux proves the logic with emulated controllers (`ctest -R 'khdays-(pad|input|picture|title)'`): the layout by position, the lower-screen view, a controller going and coming back, two controllers, the app leaving and returning. The phone must confirm what only real hardware shows. After a launch with the controller connected, `device_log.sh 120` captures every line named below.

1. **The picture.** The title shows (the Metal shader fix of commit `1310676`). `picture 300: ... % lit` and no `ALL BLACK`.
2. **The DualSense is seen as one.** `host: gamepad connected: ... (type PS5, ..., touch pad click yes, ...) (playing)` and `host: no settings file yet: the Kingdom Hearts II gamepad layout` on the first run.
3. **Cross confirms.** On the title, Cross starts NEW GAME and Circle goes back. The console shows `host: gamepad button south (a) -> a` for Cross and `east (b) -> b` for Circle. If they are swapped, the controller's positions are reported differently on the phone: send the lines.
4. **Field.** Left stick moves, right stick turns and tilts the camera, R1 locks on, R2 / L2 change target, R3 puts the camera behind Roxas, the D-pad moves the command deck, Options opens the menu.
5. **Lower-screen view.** The touch pad's click shows the DS lower screen over the picture (`lower view: on`), a second click puts it away. With the view on, a finger on it acts as the stylus.
6. **Switch Pro.** `type Switch Pro` in its line. The bottom button (B) confirms, as Cross does. Check whether its line says `Share/Capture yes`: if so Capture is the view; if it says `the lower-screen view is on Minus / Back`, Minus is the view and there is no SELECT.
7. **Controller off and on.** Switch the controller off in a field: the screen dims with "Controller disconnected" (`host: the gamepad is gone: the game waits`). Switch it on: the game goes on. Do it again and touch the screen instead: the touch keys play.
8. **Two controllers.** With both connected, pick up the other one and press a button: `host: gamepad now playing: ...`.
9. **Sleep and wake.** Lock the phone in a field, unlock: the picture returns, the controller plays without a stuck direction (`window: the app: did enter foreground`).
10. **Touch only.** Without a controller: the keys show, the Screen key shows the lower screen, a finger on it selects what is under it.
11. **Both shapes.** Folded and unfolded, landscape and portrait: the field fills the screen's width, the HUD stays inside the safe area.
12. Save in the camp menu, close the app, relaunch, load. Audio.

The first 300 controller presses are logged (`host: gamepad button <position> (<SDL name>) -> <action>`), so one launch tells which button the phone reports for each press.

Single screen is the default and the main way to play. Stacked and side by side remain under Settings → Apps → 358/2 Days → Screen layout; restart the app after changing it.
