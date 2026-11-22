"""Play the phone's build on Linux with emulated gamepads and check what the game did.

Runs the test build (CMake's KHDAYS_MOBILE_TEST) with the real window, graphics-card and input path on
SDL's hidden "offscreen" video driver and the CPU Vulkan driver (lavapipe), as the phone starts: single
screen, no settings file, no save. SDL virtual gamepads are played by KHDAYS_PAD_SCRIPT
(port/host/app/pad_script.h), so every press goes the way a real controller's does: SDL's gamepad events,
the port's devices and bindings, the game.

  title   gamepads coming and going on the title: a DualSense, pulled out (the game waits), a Switch Pro
          without Capture in its place (Minus is the lower-screen view), a DualSense picked up as the
          second gamepad (it plays), the app sent to the background and back, then Cross starts NEW GAME.
  field   a DualSense from the title through a new game into the first field: Cross confirms, Options
          skips the opening and closes the tutorial, the touch pad shows the lower screen and puts it
          away, the right stick turns the camera, the left stick walks, Options opens the camp menu and
          Circle closes it.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

LAVAPIPE = Path("/usr/share/vulkan/icd.d/lvp_icd.json")


def press(frame, name, pad=""):
    return f"{frame}{pad}+{name} {frame + 8}{pad}-{name}"


TITLE = " ".join([
    "0*switchpro", "s1", press(80, "east"), press(110, "south"), press(140, "north"), press(170, "west"),
    "200/", "230*ps5", "t", press(100, "dpdown"), "160/", "220*switchpro-nocapture", press(280, "back"),
    "340#2*ps5", press(400, "touchpad", "#2"), "460!background", "480!foreground",
    press(560, "south", "#2")])

FIELD = " ".join(
    ["0*ps5", "t", press(130, "south"), press(330, "south"), press(530, "south"), press(680, "dpleft"),
     press(740, "south"), "s11", press(400, "start"), "s2", press(600, "south"), press(1200, "south"), "s10",
     press(300, "south"), press(600, "south"), "s2"] + [press(200 * n, "south") for n in range(1, 10)] +
    [press(2300, "start"), press(2500, "touchpad"), press(2700, "touchpad"), "2800>rx:25000", "2900>rx:0",
     "3000>lx:30000", "3200>lx:0", "3300>ly:-30000", "3500>ly:0", press(3700, "east"), press(3800, "start"),
     press(4000, "east")])


def play(exe, rom, script, size, seconds, extra=()):
    """Runs the game until the script is done (and a moment more); returns its log's lines."""
    env = dict(os.environ, KHDAYS_ROM=str(rom), SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="offscreen",
               KHDAYS_TEST_WINDOW="1", KHDAYS_NET_SERVER="", KHDAYS_MOBILE_WINDOW=size, KHDAYS_MOBILE_LAYOUT="2",
               VK_DRIVER_FILES=str(LAVAPIPE), KHDAYS_TRACE_SITUATION="1", KHDAYS_PAD_SCRIPT=script,
               KHDAYS_TRACE_MOVIE_HD="1")
    env.update(extra)
    for name in ("KHDAYS_HEADLESS", "KHDAYS_SETTINGS", "KHDAYS_INPUT"):
        env.pop(name, None)
    with tempfile.TemporaryDirectory(dir=Path.cwd()) as work:
        log = Path(work) / "trace"
        with log.open("wb") as out:
            game = subprocess.Popen([exe], cwd=work, env=env, stdout=out, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + seconds
                done = None
                while time.monotonic() < deadline and game.poll() is None:
                    time.sleep(1)
                    if done is None and b"pad script: done" in log.read_bytes():
                        done = time.monotonic()
                    if done is not None and time.monotonic() - done > 6:
                        break
            finally:
                exited = game.poll()
                game.kill()
                game.wait()
        return log.read_bytes().decode(errors="replace").splitlines(), exited


class Checks:
    def __init__(self, name, played):
        self.name, (self.lines, exited), self.failed = name, played, 0
        # (a negative status is a signal's number: -11 a crash)
        self.check("the game was still running when the test stopped it", exited is None, f"exit status {exited}")

    def index(self, pattern, start=0):
        found = re.compile(pattern)
        for n in range(start, len(self.lines)):
            if found.search(self.lines[n]):
                return n
        return -1

    def expect(self, what, pattern, start=0):
        """The first line at or after `start` that matches; -1 (and a failure) when none does."""
        n = self.index(pattern, start)
        print(f"{self.name}: {what}: {'passed' if n >= 0 else 'FAILED (no line matches ' + pattern + ')'}")
        self.failed += n < 0
        return n if n >= 0 else start

    def check(self, what, ok, detail=""):
        print(f"{self.name}: {what}: {'passed' if ok else 'FAILED'}{' (' + detail + ')' if detail else ''}")
        self.failed += not ok

    def tail(self):
        sys.stderr.write("\n".join(line[:300] for line in self.lines if not line.startswith("khdays-native: frame"))[-6000:])
        sys.stderr.write("\n")


def title_case(exe, rom):
    c = Checks("title", play(exe, rom, TITLE, "948x667", 150))
    at = c.expect("first run: the Kingdom Hearts II layout", r"no settings file yet: the Kingdom Hearts II gamepad layout")
    for physical, slot, action in (("b", "south", "a"), ("a", "east", "b"), ("y", "north", "x"), ("x", "west", "y")):
        at = c.expect(f"Switch Pro printed {action.upper()} drives DS {action.upper()}",
                      rf"gamepad button {slot} \({physical}\) -> {action}$", at)
    at = c.expect("the DualSense connects and plays", r"gamepad connected: .*type PS5.*touch pad click yes.*\(playing\)", at)
    at = c.expect("its D-pad reaches the game", r"gamepad button dpdown \(dpdown\) -> down", at)
    at = c.expect("pulled out: the touch keys come back", r"gamepads: 0 connected, none plays; touch keys shown", at)
    at = c.expect("and the game waits", r"the gamepad is gone: the game waits", at)
    at = c.expect("a Switch Pro without Capture connects", r"gamepad connected: .*the lower-screen view is on Minus / Back.*\(playing\)", at)
    at = c.expect("the touch keys hide", r"gamepads: 1 connected, playing: .*touch keys hidden", at)
    at = c.expect("and the game goes on", r"the game goes on", at)
    at = c.expect("its Minus is the lower-screen view", r"lower view: on", at)
    at = c.expect("a second gamepad connects without taking over", r"gamepad connected: .*type PS5[^;]*Share/Capture (?:yes|no)\); 2 connected", at)
    at = c.expect("picked up, it plays", r"gamepad now playing: .*type PS5", at)
    at = c.expect("its touch pad puts the view away", r"lower view: off", at)
    # the app sent to the background: an HD video and a monologue's voice are paused at once (this build
    # has no player: its stand-in says what it is asked), and go on at the return
    at = c.expect("the app goes to the background", r"window: the app: will enter background", at)
    at = c.expect("an HD video is paused at once", r"movie hd( \(no player in this build\))?: video pause asked", at)
    at = c.expect("and a voice", r"movie hd( \(no player in this build\))?: voice pause asked", at)
    at = c.expect("and it is said", r"window: the app is in the background: an HD video and a voice wait", at)
    away = c.expect("it is in the background", r"window: the app: did enter background", at)
    at = c.expect("and comes back", r"window: the app: did enter foreground", away)
    c.check("nothing resumes them while it is away",
            not any("resume asked" in line for line in c.lines[away:at]))
    at = c.expect("the HD video goes on at the return", r"movie hd( \(no player in this build\))?: video resume asked", at)
    at = c.expect("and the voice", r"movie hd( \(no player in this build\))?: voice resume asked", at)
    at = c.expect("and it is said", r"window: the app is back in front: an HD video and a voice go on", at)
    c.check("the gamepad's loss paused them before, and its return let them go on",
            c.index(r"video pause asked") < c.index(r"video resume asked") < away)
    at = c.expect("a picture is drawn and read back after the return", r"^picture \d+: \d+x\d+ read back, [\d.]+% lit", at)
    at = c.expect("Cross is DS A after that", r"gamepad button south \(a\) -> a$", at)
    # the title's trace: its page (bits 32-39) leaves 0 when NEW GAME is entered
    entered = [n for n in range(at, len(c.lines))
               if (m := re.match(r"situation: frame \d+, title 0x[\da-f]+ -> 0x([\da-f]+)$", c.lines[n])) and
               (int(m[1], 16) >> 32) & 255]
    c.check("and it enters NEW GAME", bool(entered))
    if c.failed:
        c.tail()
    return c.failed == 0


def field_case(exe, rom):
    c = Checks("field", play(exe, rom, FIELD, "948x667", 800, {"KHDAYS_TRACE_CONTROLS": "1", "KHDAYS_TRACE_PLAYER": "20"}))
    scenes = [int(m[1], 16) for line in c.lines if (m := re.match(r"situation: frame \d+, scene 0x[\da-f]+ -> 0x([\da-f]+)$", line))]
    scenes = [s for s in scenes if s]
    c.check("Cross, D-pad and Options take a new game to the field", scenes[:6] == [1, 11, 5, 2, 10, 5] and scenes[-1] == 2,
            "scenes " + " ".join(map(str, scenes)))
    at = c.expect("the first field's tutorial opens", r"situation: frame \d+, tutorial 0x0 -> 0x[\da-f]+")
    at = c.expect("Options is START", r"gamepad button start \(start\) -> start", at)
    at = c.expect("and closes the tutorial", r"situation: frame \d+, tutorial 0x[\da-f]+ -> 0x0", at)
    at = c.expect("the touch pad asks for the lower-screen view", r"gamepad button touchpad \(touchpad\) -> lower_view", at)
    on = c.expect("the view is on", r"lower view: on", at)
    # (the view inside the visible safe area, round 5: the safe area kept 12 points clear of the edges -- as large as
    # that takes, the DS screen's shape, in the picture's DS dots)
    shown = c.expect("the picture shows it, the stylus on it",
                     r"^picture \d+ .*the one-screen picture.*lower view shown.*stylus [\d.]+,[\d.]+ [\d.]+x[\d.]+", on)
    m = re.search(r"stylus ([\d.]+),([\d.]+) ([\d.]+)x([\d.]+)", c.lines[shown]) if shown >= 0 else None
    c.check("the view (the stylus) is the DS screen's shape, most of the picture's height, inside it",
            bool(m) and abs(float(m[3]) * 3 - float(m[4]) * 4) <= 4 and 170 <= float(m[4]) <= 192 and
            float(m[2]) >= 0 and float(m[2]) + float(m[4]) <= 192.5, m[0] if m else "no stylus")
    lit_on = c.expect("that picture is read back", r"^picture \d+: \d+x\d+ read back, [\d.]+% lit", shown)
    off = c.expect("the touch pad puts it away", r"lower view: off", shown)
    hidden = c.expect("the picture is the field's again", r"^picture \d+ .*the one-screen picture.*lower view off.*stylus 0,0 0x0", off)
    lit_off = c.expect("that picture is read back", r"^picture \d+: \d+x\d+ read back, [\d.]+% lit", hidden)
    colour = re.compile(r"average colour (\d+) (\d+) (\d+)")
    a, b = colour.search(c.lines[lit_on]), colour.search(c.lines[lit_off])
    c.check("the two pictures differ", bool(a and b) and sum(abs(int(a[n]) - int(b[n])) for n in (1, 2, 3)) >= 12,
            f"{a[0] if a else '?'} with the view, {b[0] if b else '?'} without")

    def span(first, last):
        i, j = c.index(first, off), c.index(last, off)
        return range(i, j) if 0 <= i < j else range(0)

    def values(lines, pattern):
        found = re.compile(pattern)
        return [m[1] for n in lines if (m := found.search(c.lines[n]))]

    before = values(range(off, max(off, c.index(r"pad 1 >rx:25000", off))), r"camera: .* yaw ([\da-f]+) ")
    during = values(span(r"pad 1 >rx:25000", r"pad 1 >rx:0"), r"camera: .* yaw ([\da-f]+) ")
    c.check("the camera is still before the right stick moves", len(set(before[-20:])) <= 1, f"{len(set(before[-20:]))} yaws")
    c.check("the field's controls are traced", c.index(r"^controls: frame \d+ camera: ", off) >= 0)
    c.check("the right stick turns the camera", len(set(during)) >= 5, f"{len(set(during))} yaws")
    walked = values(span(r"pad 1 >lx:30000", r"pad 1 >ly:0"), r"^player: .* at \(([^)]*)\)")
    c.check("the left stick walks", len(set(walked)) >= 3, f"{len(set(walked))} places")
    at = c.expect("Circle is DS B", r"gamepad button east \(b\) -> b$", off)
    at = c.expect("Options opens the camp menu", r"situation: frame \d+, camp_screen 0xff -> 0x0", at)
    at = c.expect("Circle closes it", r"situation: frame \d+, camp_screen 0x[\da-f]+ -> 0xfe", at)
    if c.failed:
        c.tail()
    return c.failed == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exe", type=Path)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--only", choices=("title", "field"))
    args = parser.parse_args()
    if not LAVAPIPE.is_file():
        print("No CPU Vulkan driver (lavapipe) here: skipped")
        return 77
    exe, rom = args.exe.resolve(), args.rom.resolve()
    results = [case(exe, rom) for name, case in (("title", title_case), ("field", field_case)) if args.only in (None, name)]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
