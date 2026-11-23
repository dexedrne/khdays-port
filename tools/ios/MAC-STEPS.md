# Mac build and launch

Run the steps in order on an Apple Silicon Mac with Xcode, CMake 3.25+, Ninja, Python 3.11+ and pycparser. The first configure needs the pinned dependency downloads or their local caches. Set `SDL3_SOURCE` to reuse a local SDL 3.4.12 source checkout. Every script is headless and niced and opens no window.

1. From the Linux checkout, set `KHDAYS_MAC_HOST` to the SSH destination and run `tools/ios/sync.sh`. The default checkout on the Mac is `~/Developer/KHDAYS/src`; set `KHDAYS_MAC_DIR` to use another work directory. The sync includes the checked-out port changes and the pinned decompilation.
2. From that checkout **on the Mac**, run `tools/ios/mac/build.sh iphonesimulator`, then `tools/ios/mac/build.sh iphoneos`. Each builds arm64. With the Xcode Metal toolchain installed, both also compile the bundled shader libraries for their SDK.
3. Set `SIM_UDID` to the dedicated simulator identifier from `xcrun simctl list devices available` and run `xcrun simctl bootstatus "$SIM_UDID" -b`. This boots the simulator headlessly. For a launch diagnostic, set `ROM` to the local EU cartridge path and run `tools/ios/mac/sim_run.sh`. The simulator's Metal capabilities prevent a title run. Use the physical device for playback.
4. On the signing Mac, set `PROFILE` to the Days development provisioning profile and `SIGN_IDENTITY` to its installed signing identity. Set `SIGN_KEYCHAIN` if the identity is in a separate unlocked keychain. Run `tools/ios/mac/sign_app.sh` to sign the device Ninja product. The owner's existing two-Mac signing workflow can also provide the signed app at `~/Developer/KHDAYS/signed/khdays-native.app`; set `SIGNED_APP` to that path when using it.
5. On the paired Mac, set `ROM` to the local EU ROM, set `DEVICE_UDID` from `xcrun devicectl list devices`, unlock the phone and run `tools/ios/mac/device_run.sh`. It installs, launches, captures the console and reads the picture reports. Each capture gets its own folder under `../phone-logs/`. Set `CAPTURE_SECONDS=120` for a longer check.
6. For the owner's videos, set `VIDEOS` to their folder outside the checkout and run `tools/ios/mac/device_videos.sh`. Put the HD pack in Documents/mods/HD PACK and subtitles and cinematics under Documents/data/cutscenes.
7. Run `tools/ios/mac/device_movie_test.sh` for a scripted New Game opening. The script backs up the current save on the paired Mac, selects a fresh save folder for this launch, watches for hd802 open/start/end and compares the owner's save afterward. Ordinary launches continue to read Documents/khdays-env.txt. To capture normal play again, run `tools/ios/mac/device_log.sh 120`.
8. On Switch Pro, press printed A, B, X and Y once each and read the four `KHDAYS pad log` lines. Each printed label should match its DS action. On DualSense, Cross, Circle, Triangle and Square should report a, b, x and y.
9. In Settings → Video, check On-screen controls in each mode. Auto hides with either pad, On keeps the keys usable alongside it, and Off keeps them hidden after disconnect. Relaunch to verify persistence. Tap Settings on the touch overlay, or use three fingers to open Settings and go back. Swipe the list and tap either half of a choice to change it.

## Phone checklist: Rotation

Do each step in a field with the DualSense connected, unfolded and then folded. The lines are in `console.txt`.

| Do | Expect | A failure looks like |
| --- | --- | --- |
| Turn the phone upright, then lying again. | `window: orientation portrait after a size change (change N, ...)`, then `... landscape ...`. The game covers the drawable in both orientations; upright it is the same single-screen picture, centred (round 9: no lower screen or room below it). A report follows: `picture N: layout portrait ..., game picture X,Y WxH`. | No `window: orientation` line (iOS did not turn the app: check the orientation lock), a picture stretched or off the screen, `ALL BLACK`. |
| Hold the left stick while turning. | Roxas keeps walking. `host: sound: stream opened (1 this run)` stays the only such line. | A second `stream opened`, the music stopping, the stick let go. |
| Click the touch pad, turn upright and back. | `lower view: on`; the report says `lower view over the picture` both ways up (round 9: never below it). | The view missing after the turn, or under the notch or home bar. |
| Switch the controller off, turn the phone, switch it on. | `host: the gamepad is gone: the game waits`, the dimmed picture laid out again at once and shown whole (smaller, not cut at the sides), then `host: the game goes on`. | The picture keeps its old shape, or its sides are cut off, until the controller is back. |
| Lock the phone, turn it, unlock. | `window: the app is in the background: ...`, then `window: the app: did enter foreground`, `window: the app is back in front: ...` and `window: orientation ... after the return to the foreground`. The picture fits the new shape. | A stretched picture after unlocking that stays until a button is pressed. |
| Switch the controller off and touch the screen; press the Screen key, upright. | `host: gamepads: 0 connected, none plays; touch keys shown`. `picture N: touch keys drawn (...), their top at y T`. Upright as lying the view is `over the picture`, inside the safe area. Every part of the view takes the finger as the stylus. | A key or the Move pad lying over the view; a part of the view that presses a key instead. |
| Open the settings (PS button) upright. | The Controls page shows every hint whole, one under the other. | Hints cut off mid-word. On a folded phone, check that each binding has a device label and a usable value on its own line. |

## Phone checklist: HD cutscenes

Needs step 6. Start a new game for movie 802.

| Do | Expect | A failure looks like |
| --- | --- | --- |
| Launch. | `movies: HD videos looked for in data/cutscenes/cinematics of the working folder: the game's movies with theirs 38 of 42 ...; none for 819 838 839 841`. | `0 of 42`: the files are not in `Documents/data/cutscenes/cinematics`. |
| Start a new game and watch the opening. | `movies: avf: hd802.mp4: ... picture track ... decodable 1`, `movies: open ...hd802.mp4: 1920x1080 ...`, `movies: sound output of hd802.mp4: the device takes X ms at a time; heard Y ms after its turn to play`, `movies: start hd802.mp4 ... its clock the sound played`, at the end `movies: end hd802.mp4 ...` and `movies: 802: the HD video ended; the game goes on`. Picture and sound in step. | `movies: open ... FAILED: ...` or `movies: hd802.mp4 FAILED at ...` with an `NSError` in the `movies: avf:` line before it; the game's own small movie plays instead. Lips ahead of or behind the voice: send the `sound output` line and say which earphones. |
| Read the `end` and `close` lines. | `late (passed over)` small against `shown`. | Many late pictures: the phone decodes too slowly or the window draws too seldom. |
| Press Options during a movie, then Continue. | `movies: pause hd802.mp4 at T s`, the pause's Continue and Skip, then `movies: resume hd802.mp4 at T s` with the same T. | The sound going on under the pause, or a jump after it. |
| Lock the phone during a movie, wait ten seconds, unlock. | `window: the app is in the background: an HD video and a voice wait`, `movies: pause ...`, then after unlocking `window: the app is back in front: an HD video and a voice go on` and `movies: resume ...` at the same time of the video. If iOS took the decoder away: `movies: avf: ...: the pictures' reader was interrupted at ... opening it again there at once` and `... reader opened again at ... (try 1 of 40)`; the video goes on. | `movies: hd802.mp4 FAILED ...` after unlocking, or `... could not be opened again in 40 tries`: send the `movies: avf:` lines. The video ending at once on return: its clock counted the time away. |
| Switch the controller off during a movie, then on. | `movies: pause ...` with `host: the gamepad is gone: the game waits`, then `movies: resume ...`. | The video running on behind the notice. |
| Turn the phone during a movie. | The video covers the new drawable; the sound does not stop. | A black picture or silence after the turn. |

The longer sections below explain the lines.

## Phone checklist: 120 Hz and the picture's cost

The picture rate (`port/host/render/picture_pace.h`): 120 pictures a second while they hold their time, 60 when they do not or the phone is hot, the game's own at a critical thermal state, back up after a hold. The game itself keeps the DS's 60 VBlanks a second throughout. It applies at `picture_pace = smooth`; `picture_pace = ds` keeps the DS's pace exactly. A settings file that names `internal_resolution` keeps its value: for the phone's defaults set `internal_resolution = auto` and `picture_pace = smooth` (or delete `Documents/saves/settings.ini`).

| Do | Expect | A failure looks like |
| --- | --- | --- |
| Launch without `internal_resolution` in the settings file. | `host: no settings file yet: internal resolution auto (the 3D kept to about 1088 x 768)`, `ios: the screen shows at most 120 pictures a second; thermal state 0 ...`, `ios: the display link asks for 120 refreshes a second`, `ios: the screen refreshes every 8.33 ms (120.0 Hz)`, `clock: the screen's lock has refresh times (8.33 ms a refresh)`. No status bar upright; the home indicator hidden until the screen is touched. | `at most 60 pictures`: the screen is not ProMotion, or `CADisableMinimumFrameDurationOnPhone` is missing from the app's Info.plist. `refreshes every 16.67 ms` with `asks for 120`: iOS kept the display at 60 (Low Power Mode does this). |
| Play a field for a minute. | Every 10 s a `pace:` line: `the picture rate 120 a second`, about 120 pictures a second, half of them `between` the game's packages, `the 3D at 4x, the 2D at 5x` unfolded lying, `0.0 allocations and 0.0 read-backs a second`, VBlanks about 60. | `pace: the picture rate now 60 a second ... missed their time (... ms each on average ...)`: the phone cannot draw 120 at this size. Send the lines: `made` is the CPU's part, `presented` mostly the card's. |
| Play ten minutes or more, unfolded. | Perhaps `ios: the thermal state is now 2` and `pace: the picture rate now 60 a second ...: the thermal state is serious`; once cooler (`... now 1` or `0`), `pace: the picture rate now 120 a second` after at least 5 s. | The rate dropping below 60 at state 2, or never coming back after `now 0`. |
| One launch with `KHDAYS_PROFILE_GPU=1` in `khdays-env.txt`. | The `pace:` lines add `the card X ms of 2D (N pictures) and Y ms of 3D (M) a picture, waited for`. The waits slow the picture: take the numbers, then remove the line. | |
| Fold and unfold in a field. | `the 3D at 3x` folded lying, `4x` unfolded lying; at `internal_resolution = auto` either way up the 2D is capped (`the 2D at 4x` unfolded), because upright the filled picture is scaled by the display's height. | `the 2D at 15x` upright with `internal_resolution = window` pinned: the display's height at the DS's 192 rows overflows the auto budget; leave `auto` or a fixed scale. |
| Turn upright and play a field for a minute (round 9). | The `pace:` lines hold 60 a second upright as they do lying; `picture N: layout portrait ...; fill: 256x192 covering the window ...` with `game picture` wider than the window and taller (the DS's 4:3 picture covering, its sides cut, centred) -- or with `[video] picture = fit`, `fit: 256x192 inside the safe area` and `game picture` the window's width, centred. No lower screen below the picture. | A second picture under the game (round 8's room), `lower view below the picture`, or black bands at the top and bottom with Fill. |
| Open the pause menu (Options in the field), upright and lying. | Lying, the upper screen (Panels, Holo-Missions, Files, Tutorials, Config, Save, Title Screen) covers the window; upright it is fitted by width, whole and centred, black above and below (round 9: a raw two-screen menu cannot fill a portrait window without cutting its list) and the lower status screen is a touch-pad click away over it. | The menu's left edge or its items cut off upright, or the status screen under it (round 8's room). |

**Round 9's single screen.** Upright there is no room below the picture: portrait is laid out as lying (`[video] picture = fill` covers the window, Fit centres the whole picture). The game's own two-screen menus (the camp/pause menu, the shop, a mission's results) are fitted by width upright so nothing is cut, with the lower screen over them on demand.

**Camera: wider (round 8).** Settings → Video → **Camera: wider**, 100 to 160 (115 the phone's first run), `[video] camera_zoom` in `saves/settings.ini`. It widens the game's 3D field of view (the projection's x and y taken smaller) -- the same value in both orientations. The HUD's own 3D (a portrait's model), the 2D HUD, menus, prompts and touch keys keep their size and place; an orthographic picture (a fade, Sora's memories) still covers the screen. A value of 100 is the DS's camera. Check that the field's edges have no bare strip (the game culls to the DS's own view, so a very wide setting may show the world's end), then set the value the owner wants.

`KHDAYS_PICTURE_RATE=120`, `60` or `game` in `khdays-env.txt` pins the rate (for comparing); `auto` is the rule. `KHDAYS_PACE_LOG=N` makes the summary every N seconds. `KHDAYS_AUTO_PIXELS` and `KHDAYS_AUTO_2D_PIXELS` change auto's budgets (the 3D's pixels, 835,584 by default; the 2D's, twice that; 0 for the 2D at the window's scale). `SDL_IOS_HIDE_HOME_INDICATOR=2` dims the home indicator instead of hiding it.

## Phone checklist: a coat's scene and the character shadows (round 7)

A KH2 model with its `clip_world` volume (Roxas's cutscene coat `e_rox_hb`, the hooded models) used to hand every polygon after the volume to the compute rasterizer (`raster3d.hlsl`); the card now draws the volume's inside, the model and the wall itself. The soft masks of **Enhanced character shadows** are only that compute pass's: a picture the card draws whole builds none of them.

| Do | Expect | A failure looks like |
| --- | --- | --- |
| Launch with `KHDAYS_PROFILE_GPU=1` in `khdays-env.txt` and play a cutscene with Roxas in the coat (Day 6 or later, or a Mission Mode coat), or turn `clip_world` on in a test coat's `.secondary.txt`. | One line `native 3D: the clip volume of a mod model (N polygons) drawn by the card: its black inside, the model's own polygons and its wall; the whole list, no compute pass`, and the `pace:` lines' `ms of 3D` are the native rasterizer's. The coat does not show the room through its inside. | `... ; raster3d.hlsl draws the rest` or no line: the volume is not the first of its run (send the list's scene), or the card could not make the two clip pipelines and `host: the card's rasterizer cannot draw the 3D` came first. The coat's inside showing the floor: send the `clip_world` line and a picture. |
| Same launch with `KHDAYS_3D=compute` for one scene. | The same picture (the inside's polygon ID may differ where only a translucent piece of the coat blends on it); the `pace:` lines show the compute pass's cost for comparison. | A visibly different inside or coat. Send both pictures. |
| Settings → Enhancements → Enhanced character shadows **On**, in a field with an NPC. | Once `host: the character shadows' masks are not made: the card draws the game's own shadows with the stencil (said once)`; the shadows look as before; the `pace:` 3D cost does not jump. | The line `the enhanced character shadows are left out (...)`: send the `SDL_GetError` text. `pace:` 60 with no compute pass: send the lines. |

## Reading the phone's console

The window reports pictures 1, 30, 300, 900 and 1800, and the second picture after each change of size or safe area.

| Line | Meaning |
| --- | --- |
| `picture N (game frame F, scene S): window ... drawn as ...` | Window size in points and pixels, safe area, window flags, the layout used, the viewport and the safe-area fit. |
| `picture N: WxH read back, P% lit, within x,y to x,y` | The picture the renderer is about to show. On Linux the title is about 50% lit at picture 300. |
| `picture N: ... ALL BLACK` | The renderer's picture is empty: the fault is in the port's drawing or the Metal shaders. |
| `picture N: touch keys drawn / not drawn (...), their top at y T` | Whether the on-screen keys were drawn, and why not (a controller, the settings menu, the wait for a controller); `T` is where the keys and the Move pad begin, in window pixels. |
| `ios: picture N: app state ...` and the `ios:   window ...` lines | Every UIKit window with its level, visibility, view and Metal drawable size. A lit picture with a black screen is explained here. |
| `window: the app: ...` and `window: event ...` | Background and foreground changes with the window flags (40 = minimized: SDL draws nothing), and the window's own events. |
| `window: the app is in the background / back in front: an HD video and a voice ...` | An HD video and a monologue's voice wait while the app is hidden and go on at the return, unless the settings menu, the wait for a controller or the video's own pause still holds them (the line says which). |
| `picture N: the window did not take it` | SDL refused to present, with its reason. |
| `ios: the screen refreshes every T ms (H Hz); the clock's lock takes its times` | Since upstream 0.1.5 the game's clock follows the screen's refreshes (upstream's "screen's lock": the DS's 59.83 Hz run at the screen's 60, each picture made about 6 ms before a refresh). Windows asks its compositor; the phone has a display link (`ios_main.m`). Said once, and again when the rate changes. |
| `clock: the screen's lock has refresh times (T ms a refresh)` / `clock: the screen's lock is off: no refresh times reported here` | Whether the clock has a screen to lock to, said each time that changes (off in the background, on again in front). `KHDAYS_SCREEN_LOCK=off` in `Documents/khdays-env.txt` turns it off (then `ios: KHDAYS_SCREEN_LOCK=off: the screen's refreshes are not watched`); `KHDAYS_TRACE_LOCK=1` adds a `lock:` line every 300 VBlanks with where the VBlanks fall. If the game stutters or its sound wavers where it did not in round 4, try `off` first. |
| `situation: frame F, title ...` | The game's own title state, as in the Linux title test. |
| `pace: the picture rate now R (was R2): why` | The picture rate changed: 120 or 60 a second, or the game's own pictures; why: the thermal state, or how many pictures of the last second missed their time and by how much, or two quick seconds after the hold. |
| `pace: 10.0 s: N pictures a second (P of the game's packages, B between them), ...` | Every 10 s: pictures a second, the game's 3D frames and VBlanks a second, a picture's time on the window's thread (made, drawn, presented, the most), the game's thread's time a VBlank, the card's times (`KHDAYS_PROFILE_GPU=1` only), allocations and read-backs a second, the thermal state, the 3D's and the 2D's scale, the rate, since when and why. |
| `ios: the screen shows at most N pictures a second`, `ios: the display link asks for N refreshes a second`, `ios: the thermal state is now N` | The screen's most (120 on ProMotion), the rate asked of the display as the picture pace changes (60 lets the screen slow down too), the thermal state as iOS changes it (0 nominal, 1 fair, 2 serious, 3 critical). |

The saved pictures are `Documents/saves/logs/picture-NNNNN.png`, at most 1024 pixels wide.

To switch testing options on without a rebuild, put `khdays-env.txt` in the app's Documents folder with one `NAME=VALUE` per line. Useful names: `KHDAYS_PICTURE_LOG=60` (a report every 60 pictures), `KHDAYS_GPU_DEBUG=1`, `KHDAYS_PROFILE=1`, `KHDAYS_TRACE_SITUATION=` (an empty value switches the title trace off).

## What to check on the phone

Linux proves the logic with emulated controllers (`ctest -R 'khdays-(pad|input|picture|title)'`): DualSense positions, Nintendo printed labels, the lower-screen view, a controller going and coming back, two controllers, the app leaving and returning. The phone must confirm what only real hardware shows. After a launch with the controller connected, `device_log.sh 120` captures every line named below.

1. **The picture.** The title shows (the Metal shader fix of commit `1310676`). `picture 300: ... % lit` and no `ALL BLACK`.
2. **The DualSense is seen as one.** `host: gamepad connected: ... (type PS5, ..., touch pad click yes, ...) (playing)` and `host: no settings file yet: the Kingdom Hearts II gamepad layout` on the first run.
3. **Cross confirms.** On the title, Cross starts NEW GAME and Circle goes back. The console shows `host: gamepad button south (a) -> a` for Cross and `east (b) -> b` for Circle. If they are swapped, the controller's positions are reported differently on the phone: send the lines.
4. **Field.** Left stick moves, right stick turns and tilts the camera, R1 locks on, R2 / L2 change target, R3 puts the camera behind Roxas, the D-pad moves the command deck, Options opens the menu.
5. **Lower-screen view.** The touch pad's click shows the DS lower screen over the picture (`lower view: on`), a second click puts it away. With the view on, a finger on it acts as the stylus.
6. **Switch Pro.** `type Switch Pro` in its line. Press the four printed buttons in order. The console should say `KHDAYS pad log: … printed A -> a`, then B → b, X → x and Y → y. A confirms and B goes back. Check whether its line says `Share/Capture yes`: if so Capture is the view; if it says `the lower-screen view is on Minus / Back`, Minus is the view and there is no SELECT.
7. **Controller off and on.** Switch the controller off in a field: the screen dims with "Controller disconnected" (`host: the gamepad is gone: the game waits`). Switch it on: the game goes on. Do it again and touch the screen instead: the touch keys play.
8. **Two controllers.** With both connected, pick up the other one and press a button: `host: gamepad now playing: ...`.
9. **Sleep and wake.** Lock the phone in a field, unlock: the picture returns, the controller plays without a stuck direction (`window: the app: did enter foreground`).
10. **Touch only.** Without a controller: the keys show, the Screen key shows the lower screen, a finger on it selects what is under it.
11. **Both shapes, turned while playing.** Folded and unfolded, turn the phone in a field, with the lower-screen view on and off. Nothing restarts: the music plays on and a held stick keeps its direction. See "Rotation" below.
12. Save in the camp menu, close the app, relaunch, load. Audio.

The first 300 controller presses are logged (`host: gamepad button <position> (<SDL name>) -> <action>`), so one launch tells which button the phone reports for each press.

### Rotation

The app allows all four orientations on iPhone and iPad (`Info.plist`, `SDL_HINT_ORIENTATIONS` in `ios_main.m`). Turning, folding or unfolding the phone lays the picture out again at once.

| Held | Game picture | Lower-screen view (touch pad click) |
| --- | --- | --- |
| Lying (landscape) | Covers the whole drawable, centred. | Over the picture: whole in the middle, or the inset at the right. |
| Upright (portrait) | Covers the whole drawable with the view off. With the lower view below, the game spans the width above it. | Below the picture, as large as fits; the inset style is smaller, at the right. Over the picture when the space below is under half a DS screen. Played by touch, the space below ends where the touch keys begin, so the view never lies under a key: unfolded that leaves too little and the view goes over the picture; folded it stays below. |

Movies cover the drawable in both orientations unless Movies → Fit is selected. The single-screen title keeps the menu choice visible; the controller can show the full lower view.

What the console says at each turn:

- `window: orientation portrait after a size change (change N, ...): window WxH, PxQ pixels, safe area ...; layout portrait (...), game picture X,Y WxH, lower view below the picture X,Y WxH, stylus X,Y WxH (window pixels)`. One line per change of size, pixel size or safe area, said after the picture is laid out anew.
- `window: the display's orientation: portrait (3)` when SDL reports the display turned.
- A full picture report (`picture N ...`, `picture N: layout ...`, `picture N: ... % lit`) twelve pictures later, when the game's frames have the new width. Its PNG is in `Documents/saves/logs/`.
- `window: orientation ... after the return to the foreground` each time the app comes back: the picture is laid out and drawn twice again whether or not anything changed, because a turn made while the app was hidden is drawn into SDL's old-size backbuffer otherwise.
- `host: sound: stream opened (1 this run)` appears once. A second one after a turn is a fault.

Check on the phone: (a) the game covers the display in both orientations with the lower view closed, in all four orientations, folded and unfolded; (b) the HUD and the view stay inside the notch, the corners and the home bar; (c) the music does not stop or stutter at a turn; (d) a stick held during the turn still moves Roxas or the camera after it; (e) with the controller off (`Controller disconnected`) and under the settings menu, the screen is laid out again when turned; (f) without a controller, the touch keys move to the new bottom corners, a held key stays held, and a finger on the view below the picture is the stylus.

Check readability of the folded portrait binding table and the lower-view touch targets near the on-screen keys on the device.

Single screen is the default and the main way to play. Stacked and side by side remain under Settings → Apps → 358/2 Days → Screen layout; restart the app after changing it.

## HD cutscenes

The phone plays the player's own HD videos in place of the game's movies, as the Windows build does. The player is `port/host/movies/movie_hd_core.c` (shared, proven on Linux against a made-up decoder) over `movie_hd_apple.m` (AVFoundation, which only the phone or a Mac can run). Nothing has to be switched on: `[video] movies = hd` is the default, and a movie without its video plays as the game's own.

**Where the videos go.** `Documents/data/cutscenes/cinematics/hd<movie>.mp4` in the app's container, one per game movie (`hd802.mp4` to `hd848.mp4`). The cartridge has 42 movies; with the 38-file set, 819, 838, 839 and 841 have no video and play as the game's. H.264 with AAC sound, 1280x720 or 1920x1080, is what was checked.

**At the start**, one line says what was found:

```
movies: HD videos looked for in data/cutscenes/cinematics of the working folder: the game's movies with theirs 38 of 42 (0 the game's own by the map); none for 819 838 839 841; the map's scenes with theirs 2 of 97; its monologues' voices 0 of 7. ...
```

`0 of 42` means the folder is not where the app looks (it must be inside `Documents`, the app's working folder).

**When a movie plays**, in this order:

```
movies: avf: hd802.mp4: 710385942 bytes, 299.133 s, 2 tracks; picture track 1: codec 'avc1', coded 1920x1080, clean aperture ..., 30.000 frames a second, ..., decodable 1; read as NV12 (420v) from 0.000 s to its end, 299.133 s
movies: avf: hd802.mp4: first picture at 0.000 s: buffer 1920x1080, luma pitch 1920, chroma 960x540 pitch 1920, shown 1920x1080 at 0,0, colour matrix ...
movies: avf: hd802.mp4: sound track 2: codec 'aac ', 2 channels, 48000 Hz, ...; read as interleaved 32-bit floats, 2 channels 48000 Hz, ...
movies: avf: hd802.mp4: first sound at 0.000000 s: ... frames a block, 2 channels 48000 Hz floats
movies: open data/cutscenes/cinematics/hd802.mp4: 1920x1080, 30.000 frames a second, 299.133 s long, sound 2 channels 48000 Hz as floats, whole, fades in 0.50 s out 0.50 s, first picture at 0.000 s
movies: 802 shown instead from data/cutscenes/cinematics/hd802.mp4 (1920x1080, with its sound)
movies: start hd802.mp4 at 0.000 s, its clock the sound played
movies: end hd802.mp4 at 299.1 s: N pictures shown, M late (passed over), 299.11 s of sound
movies: close hd802.mp4 at ... (ended): ... pictures decoded, ... shown, ... late (passed over), its picture at most ... ms off its clock; ...
```

`movies: sound output of <file>: the device takes X ms at a time; heard Y ms after its turn to play` follows the opening and is said again when the route changes (earphones). The video's clock is the sound heard: what SDL's device has taken, less the buffers the system holds ahead and the route's delay as iOS reports it. Y is an estimate (two device buffers plus `AVAudioSession.outputLatency`) that only the phone can confirm.

`movies: pause <file> at T s` / `movies: resume <file> at T s` appear when the settings menu, START's pause, a lost controller or the app going to the background holds the video. The window says the last one first: `window: the app is in the background: an HD video and a voice wait`.

**What to check.**

1. A new game's opening movie (802) shows in HD with sound, in step with the picture, and the game goes on after it.
2. `late (passed over)` in the `end` line stays small against `shown`. Many late pictures mean the phone decodes too slowly or the window draws too seldom.
3. START over a movie opens the pause (Continue, Skip); Continue goes on, Skip ends it.
4. Lock the phone during a movie and unlock: `movies: pause`, then `movies: resume`, and the video goes on. Nothing is read while the app is hidden. If the system took the decoder away meanwhile, the first read after the return says `movies: avf: ...: the pictures' reader was interrupted at ... s ...; opening it again there at once (0 of 40 tries used)`, then `... reader opened again at ... s after the interruption (try 1 of 40)`, and the video goes on from where it was. An interruption is not counted as a fault of the file: the reader is tried again every 0.25 s the app is awake, up to 40 times, before `... could not be opened again in 40 tries` and the video fails. Any other reader failure is tried twice (`try 1 of 2`). Send the lines if it does not go on.
5. Colours: the picture should look as in any other player (BT.709, limited range). The `colour matrix` in the first-picture line says what the file is marked as.

**If a movie stays the game's own**, the console says why, by name:

- `movies: 8xx: no HD video (data/cutscenes/cinematics/hd8xx.mp4); the game's movie plays`: the file is not there.
- `movies: [video] movies is not hd (or there is no window): the game's own movies play`: the setting. `movies = hd` under `[video]` in `Documents/saves/settings.ini`.
- `movies: open ... FAILED: ...` with an `NSError` domain, code and text from a `movies: avf:` line before it: AVFoundation refused the file. Send the lines.
- `movies: hd8xx.mp4 FAILED at ... s after ... pictures: ...`: it stopped part-way; the game's movie shows from there.

**Known limits with the 38-file set.** These files hold the game's movies alone. The port's scene list also names the HD collection's theater videos (`hd000` to `hd136`) for 97 story scenes and 7 monologues; they are not in this set, so those scenes play as the game's own (`scenes: ...: no HD video ...; the game's scene plays`, once each). Two scenes after movies 830 and 836 are shown by the theater's longer videos of the same names; with these shorter files the game's own scenes play (`movies: 830: hd830 ended with the game's movie: a scene after it is not skipped`). The Theater menu's HD list needs a mod's theater files and is the game's own list without them; a movie started from it still plays in HD. No subtitles or theater music play unless their files are beside the videos (`subtitles/<language>/hd<movie>.txt`, `music/<video>.txt`).
