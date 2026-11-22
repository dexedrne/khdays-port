# Mac build and launch

Run the steps in order on an Apple Silicon Mac with Xcode, CMake 3.25+, Ninja, Python 3.11+ and pycparser. The first configure needs the pinned dependency downloads or their local caches. Set `SDL3_SOURCE` to reuse a local SDL 3.4.12 source checkout. Every script is headless and niced and opens no window.

The iOS simulator cannot run the game: its Metal lacks features the graphics path needs. The simulator build is only a compile and link check. The picture and the controls are checked on Linux (`ctest -R 'khdays-(picture|pad)'`) and on the phone.

The order is build, then device. Nothing is checked in the simulator.

**Build**

1. Set `KHDAYS_MAC_HOST=user@host` and optionally `KHDAYS_MAC_DIR` (default `khdays`). From the repository root run `tools/ios/sync.sh`. This sends the tracked files, the new port sources and decompilation commit `a2b0478`. It never sends a cartridge or a video.
2. On the Mac run `cd ~/khdays/src` (or your `KHDAYS_MAC_DIR`). Set the cartridge path on that Mac, for example `export ROM="$HOME/Downloads/games-emulation/358kh.nds"`.
3. Build the device app with `tools/ios/mac/build.sh iphoneos`. This is the first time `port/host/movies/movie_hd_apple.m` is compiled and linked for real: a failure here names the file and line. (`tools/ios/mac/build.sh iphonesimulator` is an optional link check only.)
4. Set `export TEAM_ID='<Apple development team ID>'` and run `tools/ios/mac/sign_app.sh`. The signing identity and provisioning must permit `beer.vyvanse.khdays`.

**Device**

5. Run `xcrun devicectl list devices`, unlock the paired iPhone, set `export DEVICE_UDID='<device identifier>'` and run `tools/ios/mac/device_run.sh`. This installs the app, puts the ROM in Documents/Kh358.nds and launches the game.
6. HD videos, once: `export VIDEOS=<folder with the hd*.mp4 files, outside the source tree>` and run `tools/ios/mac/device_videos.sh`. It copies each file to `Documents/data/cutscenes/cinematics/` in the app's container and names any that fail. The script is untested; the other way is the Files app: On My iPhone > 358/2 Days > `data/cutscenes/cinematics`.
7. Run `tools/ios/mac/device_log.sh 120`. It relaunches the app with its console captured for 120 seconds, writes `../phone-logs/console.txt`, fetches the saved pictures into `../phone-logs/logs/` and prints the report lines (`picture`, `ios:`, `window:`, `host:`, `movies:`, `scenes:`, `lower view:`). Everything else, the `movies: avf:` lines included, is in `console.txt`.
8. Go through the two checklists below while a `device_log.sh` capture runs, then send `../phone-logs/`.

## Phone checklist: Rotation

Do each step in a field with the DualSense connected, unfolded and then folded. The lines are in `console.txt`.

| Do | Expect | A failure looks like |
| --- | --- | --- |
| Turn the phone upright, then lying again. | `window: orientation portrait after a size change (change N, ...)`, then `... landscape ...`. The picture is across the top upright and fills the height lying. A report follows: `picture N: layout portrait ..., game picture X,Y WxH`. | No `window: orientation` line (iOS did not turn the app: check the orientation lock), a picture stretched or off the screen, `ALL BLACK`. |
| Hold the left stick while turning. | Roxas keeps walking. `host: sound: stream opened (1 this run)` stays the only such line. | A second `stream opened`, the music stopping, the stick let go. |
| Click the touch pad, turn upright and back. | `lower view: on`; the report says `lower view below the picture` upright and `over the picture` lying. | The view missing after the turn, or under the notch or home bar. |
| Switch the controller off, turn the phone, switch it on. | `host: the gamepad is gone: the game waits`, the dimmed picture laid out again at once and shown whole (smaller, not cut at the sides), then `host: the game goes on`. | The picture keeps its old shape, or its sides are cut off, until the controller is back. |
| Lock the phone, turn it, unlock. | `window: the app is in the background: ...`, then `window: the app: did enter foreground`, `window: the app is back in front: ...` and `window: orientation ... after the return to the foreground`. The picture fits the new shape. | A stretched picture after unlocking that stays until a button is pressed. |
| Switch the controller off and touch the screen; press the Screen key, upright. | `host: gamepads: 0 connected, none plays; touch keys shown`. `picture N: touch keys drawn (...), their top at y T`. Unfolded upright the view is `over the picture`; folded upright it is `below the picture` and ends above `T`. Every part of the view takes the finger as the stylus. | A key or the Move pad lying over the view; a part of the view that presses a key instead. |
| Open the settings (PS button) upright. | The Controls page shows every hint whole, one under the other. | Hints cut off mid-word. On the folded screen the narrow columns of the binding table are still cut: known. |

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
| Turn the phone during a movie. | The video is laid out again, at the top upright; the sound does not stop. | A black picture or silence after the turn. |

The longer sections below explain the lines.

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
11. **Both shapes, turned while playing.** Folded and unfolded, turn the phone in a field, with the lower-screen view on and off. Nothing restarts: the music plays on and a held stick keeps its direction. See "Rotation" below.
12. Save in the camp menu, close the app, relaunch, load. Audio.

The first 300 controller presses are logged (`host: gamepad button <position> (<SDL name>) -> <action>`), so one launch tells which button the phone reports for each press.

### Rotation

The app allows all four orientations on iPhone and iPad (`Info.plist`, `SDL_HINT_ORIENTATIONS` in `ios_main.m`). Turning, folding or unfolding the phone lays the picture out again at once.

| Held | Game picture | Lower-screen view (touch pad click) |
| --- | --- | --- |
| Lying (landscape) | Fills the safe area's height, in the middle, as wide as the screen allows. | Over the picture: whole in the middle, or the inset at the right. |
| Upright (portrait) | Across the safe area's whole width, at its top (under the notch or status bar). | Below the picture, as large as fits; the inset style is smaller, at the right. Over the picture when the space below is under half a DS screen. Played by touch, the space below ends where the touch keys begin, so the view never lies under a key: unfolded that leaves too little and the view goes over the picture; folded it stays below. |

Upright, the title and every other place that shows both DS screens whole has them one above the other, as on the DS. An HD video is at the top too.

What the console says at each turn:

- `window: orientation portrait after a size change (change N, ...): window WxH, PxQ pixels, safe area ...; layout portrait (...), game picture X,Y WxH, lower view below the picture X,Y WxH, stylus X,Y WxH (window pixels)`. One line per change of size, pixel size or safe area, said after the picture is laid out anew.
- `window: the display's orientation: portrait (3)` when SDL reports the display turned.
- A full picture report (`picture N ...`, `picture N: layout ...`, `picture N: ... % lit`) twelve pictures later, when the game's frames have the new width. Its PNG is in `Documents/saves/logs/`.
- `window: orientation ... after the return to the foreground` each time the app comes back: the picture is laid out and drawn twice again whether or not anything changed, because a turn made while the app was hidden is drawn into SDL's old-size backbuffer otherwise.
- `host: sound: stream opened (1 this run)` appears once. A second one after a turn is a fault.

Check on the phone: (a) the picture is at the top upright and fills the height lying, in all four orientations, folded and unfolded; (b) the HUD and the view stay inside the notch, the corners and the home bar; (c) the music does not stop or stutter at a turn; (d) a stick held during the turn still moves Roxas or the camera after it; (e) with the controller off (`Controller disconnected`) and under the settings menu, the screen is laid out again when turned; (f) without a controller, the touch keys move to the new bottom corners, a held key stays held, and a finger on the view below the picture is the stylus.

Known limits: while the game stands still (the settings menu, the controller notice) the picture behind was made for the old shape; after a turn it is shown whole at the size that fits (smaller, with bars) until the game runs again. Lying, played by touch, the touch keys still lie over the lower-screen view at the bottom corners, as before. The settings menu does not keep inside the safe area, and on the folded screen upright its binding table's narrow columns are cut off.

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
