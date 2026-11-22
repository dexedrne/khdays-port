"""The phone's picture rate on Linux: 120 while the pictures hold it, 60 when the phone is hot or too slow,
the game's own at a critical thermal state, and back up after each -- the game itself at the DS's rate
throughout.

Runs the test build (CMake's KHDAYS_MOBILE_TEST) with the real window and graphics-card path on SDL's hidden
"offscreen" video driver and the CPU Vulkan driver (lavapipe), as test_pad.py does, on the title (its 3D runs
at the game's 30 a second, so the pictures between its frames are drawn), with no settings file (the phone's
first-run defaults: internal resolution auto, picture_pace smooth). KHDAYS_SCREEN_LOCK=120 makes the clock
take the screen for a 120 Hz one (the phone's ProMotion screen with Info.plist's
CADisableMinimumFrameDurationOnPhone). The window is small (256x180) because the CPU driver cannot draw
anything larger 120 times a second: the rule (host/render/picture_pace.h) is the same at any size.

The pad script (host/app/pad_script.h) changes the phone's condition as the title's VBlanks pass:
  "!thermal=2"  the thermal state serious: 60 at once, the display asked for 60
  "!thermal=0"  nominal again: 120 after the hold
  "!slow=8"     each picture 8 ms longer (12 ms or so on this driver, over 90% of a 120 Hz refresh): 60
  "!slow=0"     quick again 5 s later: 120, but only 10 s after the step down (the hold doubled: the last step
                up failed within 15 s)
  "!thermal=3"  critical: the game's own pictures
  "!thermal=0"  nominal: back up a step at a time, 60 then 120

Checked: each change on the console ("pace: the picture rate now ...") in that order, with its reason, the
holds kept; the summaries ("pace: ...", every 5 s here, 10 on the phone) at 120 draw about 120 pictures a second, at 60 about 60,
while the game's VBlanks stay at 60 a second throughout; no allocation or read-back of the graphics card a
picture once the title shows and no VSync switch (a new swapchain each); the game still running at the end.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import time

LAVAPIPE = Path("/usr/share/vulkan/icd.d/lvp_icd.json")
SIZE = "256x180"
# anchored at the title's menu (t), in VBlanks
# (all within the title's first two minutes: then it plays its opening again, without 3D)
STEPS = [(600, "thermal=2"), (1200, "thermal=0"), (1800, "slow=8"), (2100, "slow=0"), (2700, "thermal=3"),
         (3300, "thermal=0"), (5100, "report")]
SCRIPT = "0*ps5 t " + " ".join(f"{frame}!{what}" for frame, what in STEPS)
CHANGE = re.compile(r"^pace: the picture rate now (.+?) \(was (.+?)\): (.*)$")
SUMMARY = re.compile(r"^pace: ([\d.]+) s: ([\d.]+) pictures a second \(([\d.]+) of the game's packages, ([\d.]+) "
                     r"between them\), the game's 3D frames ([\d.]+) and VBlanks ([\d.]+) a second; a picture ([\d.]+) ms "
                     r".*; ([\d.]+) allocations and ([\d.]+) read-backs a second, VSync switched (\d+) times; thermal state (\w+); "
                     r"the 3D at (\d+)x, "
                     r"the 2D at (\d+)x; the picture rate (.+?) \(the screen")
STEP = re.compile(r"^pad script: frame \d+ \(scene \d+\): pad 1 !(\S+)")


class Checks:
    def __init__(self):
        self.failed = 0

    def check(self, what, ok, detail=""):
        print(f"{'passed' if ok else 'FAILED'}: {what}{' (' + detail + ')' if detail else ''}")
        self.failed += not ok


def play(exe, rom, seconds, out):
    """Runs the game until the script's last step and a moment more; returns (seconds, line) pairs and its status."""
    env = dict(os.environ, KHDAYS_ROM=str(rom), SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="offscreen",
               KHDAYS_TEST_WINDOW="1", KHDAYS_NET_SERVER="", KHDAYS_MOBILE_WINDOW=SIZE, KHDAYS_MOBILE_LAYOUT="2",
               VK_DRIVER_FILES=str(LAVAPIPE), KHDAYS_PAD_SCRIPT=SCRIPT, KHDAYS_SCREEN_LOCK="120",
               KHDAYS_TRACE_SITUATION="1", KHDAYS_PACE_LOG="5")
    for name in ("KHDAYS_HEADLESS", "KHDAYS_SETTINGS", "KHDAYS_INPUT", "KHDAYS_SHOT_WINDOW", "KHDAYS_PICTURE_RATE",
                 "KHDAYS_TEST_THERMAL", "KHDAYS_PROFILE_GPU", "KHDAYS_AUTO_PIXELS"):
        env.pop(name, None)
    lines = []
    with tempfile.TemporaryDirectory(dir=Path.cwd()) as work:
        game = subprocess.Popen([exe], cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        start = time.monotonic()

        def read():
            for raw in game.stdout:
                lines.append((time.monotonic() - start, raw.decode(errors="replace").rstrip("\n")))

        reader = threading.Thread(target=read, daemon=True)
        reader.start()
        done = None
        try:
            while time.monotonic() - start < seconds and game.poll() is None:
                time.sleep(0.5)
                if done is None and any("!report" in l for _, l in lines[-200:]):
                    done = time.monotonic()
                if done is not None and time.monotonic() - done > 3:
                    break
        finally:
            exited = game.poll()
            game.kill()
            game.wait()
            reader.join(timeout=5)
    if out is not None:
        out.write_text("".join(f"{t:9.3f} {l}\n" for t, l in lines if not l.startswith("khdays-native: frame")))
    return lines, exited


def main():
    sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exe", type=Path)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--out", type=Path, help="the console with each line's seconds, kept there")
    args = parser.parse_args()
    if not LAVAPIPE.is_file():
        print("No CPU Vulkan driver (lavapipe) here: skipped")
        return 77
    lines, exited = play(args.exe.resolve(), args.rom.resolve(), 300, args.out)
    c = Checks()
    c.check("the game was still running when the test stopped it", exited is None, f"exit status {exited}")
    c.check("the phone's first-run defaults: internal resolution auto",
            any("internal resolution auto" in l for _, l in lines))

    # the condition's steps as the script played them, and the changes of rate, in time
    steps = [(t, m[1]) for t, l in lines if (m := STEP.match(l))]
    changes = [(t, m[1], m[2], m[3]) for t, l in lines if (m := CHANGE.match(l))]
    summaries = [(t, m) for t, l in lines if (m := SUMMARY.match(l))]
    for t, now, was, why in changes:
        print(f"  {t:7.1f} s: {was} -> {now}: {why[:150]}")
    c.check("every step of the script was played", [w for _, w in steps] == [w for _, w in STEPS],
            ", ".join(w for _, w in steps))
    if len(steps) != len(STEPS):
        return 1
    at = {}
    for (t, what), n in zip(steps, range(len(steps))):
        at[n] = t

    def changes_between(a, b):
        return [x for x in changes if a <= x[0] < b]

    end = lines[-1][0] if lines else 0
    span = [(at[0], at[1]), (at[1], at[2]), (at[2], at[3]), (at[3], at[4]), (at[4], at[5]), (at[5], at[6])]
    before = changes_between(0, at[0])
    c.check("120 on the title before any step (no step down there)",
            not before or before[-1][1] == "120 a second", "; ".join(f"{x[1]}: {x[3][:60]}" for x in before))

    hot = changes_between(*span[0])
    c.check("thermal state serious: down to 60 at once (within 2 s), for the heat",
            len(hot) == 1 and hot[0][1] == "60 a second" and hot[0][0] - at[0] < 2.0 and "thermal state is serious" in hot[0][3],
            "; ".join(f"{x[0] - at[0]:.1f} s {x[1]}: {x[3][:70]}" for x in hot))
    c.check("the display asked for 60 then", any("display asked for 60" in l for t, l in lines if at[0] <= t < at[1]))

    cool = changes_between(*span[1])
    c.check("nominal again: back to 120 after the 5 s hold, at no time before",
            len(cool) == 1 and cool[0][1] == "120 a second" and cool[0][0] - at[1] >= 5.0 and
            "no picture missed" in cool[0][3], "; ".join(f"{x[0] - at[1]:.1f} s {x[1]}: {x[3][:70]}" for x in cool))
    c.check("the display asked for 120 again", any("display asked for 120" in l for t, l in lines if at[1] <= t < at[2]))

    slow = changes_between(*span[2])
    c.check("pictures too slow for 120 (8 ms more each): down to 60 within 2 s, for the missed times, and not lower",
            len(slow) == 1 and slow[0][1] == "60 a second" and slow[0][0] - at[2] < 3.0 and "missed their time" in slow[0][3],
            "; ".join(f"{x[0] - at[2]:.1f} s {x[1]}: {x[3][:90]}" for x in slow))

    quick = changes_between(*span[3])
    c.check("quick again: back to 120, the hold doubled (10 s after the step down, as the last step up was "
            "under 15 s before it)", len(quick) == 1 and quick[0][1] == "120 a second" and
            (quick[0][0] - slow[0][0] if slow else 0) >= 10.0,
            "; ".join(f"{x[0] - at[3]:.1f} s after the step, {x[0] - slow[0][0]:.1f} s after the step down {x[1]}"
                      for x in quick) if slow else "no step down before")

    critical = changes_between(*span[4])
    c.check("thermal state critical: the game's own pictures at once",
            len(critical) == 1 and critical[0][1].startswith("the game's own") and critical[0][0] - at[4] < 2.0,
            "; ".join(f"{x[0] - at[4]:.1f} s {x[1]}" for x in critical))

    back = changes_between(at[5], end + 1)
    c.check("nominal again: up a step at a time, 60 then 120, each after its hold",
            [x[1] for x in back] == ["60 a second", "120 a second"] and back[0][0] - at[5] >= 5.0 and
            back[1][0] - back[0][0] >= 5.0, "; ".join(f"{x[0] - at[5]:.1f} s {x[1]}" for x in back))

    # the summaries: pictures a second by the rate they report, the game's VBlanks at 60 throughout
    rows = []
    for t, m in summaries:
        rows.append((t, float(m[2]), float(m[3]), float(m[4]), float(m[6]), float(m[7]), float(m[8]), float(m[9]),
                     m[11], m[14], int(m[10])))
        print(f"  {t:7.1f} s: {m[2]} pictures a second ({m[3]} packages, {m[4]} between), VBlanks {m[6]}, "
              f"{m[7]} ms a picture, {m[8]} allocations {m[9]} read-backs a second, VSync switched {m[10]} times, "
              f"thermal {m[11]}, the 3D at {m[12]}x, 2D at {m[13]}x, rate {m[14]}")
    # the title's menu shown and its first pictures done, up to the script's end
    steady = [r for r in rows if at[0] - 5.0 <= r[0] <= at[6]]
    c.check("the game's VBlanks at 60 a second in every summary after the start (the game's rate kept)",
            steady and all(57.0 <= r[4] <= 63.0 for r in steady), ", ".join(f"{r[4]:.1f}" for r in steady))

    def whole(rate):
        # a summary whose 5 seconds were all at that rate: the change before it is older
        out = []
        for r in steady:
            last = [x for x in changes if x[0] <= r[0]]
            if r[9] == rate and (not last or r[0] - last[-1][0] >= 5.5):
                out.append(r)
        return out

    at120, at60 = whole("120 a second"), whole("60 a second")
    c.check("5 s at 120: over 100 pictures a second, about half of them between the game's packages",
            at120 and all(r[1] >= 100.0 and r[3] >= 40.0 for r in at120),
            ", ".join(f"{r[1]:.0f} ({r[3]:.0f} between)" for r in at120))
    c.check("5 s at 60: 50 to 65 pictures a second, none between", not at60 or all(50.0 <= r[1] <= 65.0 and r[3] < 2.0
                                                                                  for r in at60),
            ", ".join(f"{r[1]:.0f} ({r[3]:.0f} between)" for r in at60) or "none whole")
    c.check("no allocation on the graphics card a picture once the title shows (under one a second)",
            steady and all(r[6] < 1.0 for r in steady), ", ".join(f"{r[6]:.1f}" for r in steady))
    c.check("VSync never switched on and off (each switch a new swapchain) while the rate changed",
            steady and all(r[10] == 0 for r in steady), ", ".join(str(r[10]) for r in steady))
    c.check("the thermal state in the summaries follows the script",
            any(r[8] == "serious" for r in rows) and any(r[8] == "critical" for r in rows) and rows[-1][8] == "nominal")
    print(f"{c.failed} checks FAILED" if c.failed else "all checks passed")
    return 1 if c.failed else 0


if __name__ == "__main__":
    sys.exit(main())
