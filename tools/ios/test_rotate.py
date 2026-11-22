"""Turn the phone while the game runs, on Linux, and check every picture after every turn.

Runs the test build (CMake's KHDAYS_MOBILE_TEST) with the real window, graphics-card and input path on
SDL's hidden "offscreen" video driver and the CPU Vulkan driver (lavapipe), as test_pad.py does. The pad
script (port/host/app/pad_script.h) resizes the window in the middle of the game ("!size=WxH") and
gives it the turned phone's unsafe margins ("!safe=L,T,R,B"), several times in one run, and asks for a
picture report after each turn ("!report"). Nothing restarts between the turns.

  title   on the title, at the unfolded phone's pixel sizes (2853x2007 and 2007x2853) and the folded
          one's (2868x1320 and 1320x2868): seven shapes in one run.
  field   a DualSense takes a new game into the first field (at the sizes in points, a third of the
          pixels: the CPU graphics driver is slow), then: the lower-screen view on and off, lying and
          upright, unfolded and folded, with the right stick held through two turns. Then the
          controller is pulled out: the phone turned upright while the game waits (the picture made for
          the lying phone is shown whole, not with its sides cut off), a tap plays by touch, and the
          Screen key shows the view -- which stays clear of the touch keys upright (over the picture
          unfolded, below it folded).
  inset   the same field with `lower_view = inset` in the settings file: the view smaller, at the right.
  waits   on the title, the phone turned while the game stands still: under the notice of a lost
          controller -- also while the app is in the background, where nothing is presented -- and
          under the settings menu.

Checked after each turn: the picture read back has the window's new size, is lit, and lies inside the
safe area; the console says the new orientation; the game's picture spans the safe area's width upright
(at its top) and its height lying (in its middle); the lower-screen view is below the picture upright and
over it lying, inside the safe area, and the stylus rectangle is the view's; the camera keeps turning
while the stick is held across the turns; the sound stream is opened once and never closed; the game is
still running at the end.
"""
import argparse
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from test_pad import FIELD, LAVAPIPE, Checks, press

ROOT = Path(__file__).resolve().parents[2]

# the unfolded phone and the folded one, in pixels, with the unsafe margins (left, top, right, bottom)
UNFOLDED_L = ((2853, 2007), (60, 0, 60, 21))
UNFOLDED_P = ((2007, 2853), (0, 59, 0, 34))
FOLDED_L = ((2868, 1320), (59, 0, 59, 21))
FOLDED_P = ((1320, 2868), (0, 59, 0, 34))
# the phone console's window (round 4, unfolded lying: 951x669 points at 2.806) with its safe area: the home
# bar's 34 points at the foot; turned, the same margins
CONSOLE_L = ((2669, 1878), (0, 0, 0, 95))
CONSOLE_P = ((1878, 2669), (0, 0, 0, 95))


def points(shape):
    """A shape at the phone's size in points (a third of its pixels)."""
    (w, h), margins = shape
    return (w // 3, h // 3), tuple(math.ceil(m / 3) for m in margins)


def turn(frame, shape):
    (w, h), margins = shape
    return f"{frame}!safe={','.join(map(str, margins))} {frame + 1}!size={w}x{h}"


def play(exe, rom, script, shape, seconds, extra=(), settings=None, pictures=None, videos=None, mod=None):
    """Runs the game until the script is done (and a moment more); returns its log's lines and status."""
    (w, h), margins = shape
    env = dict(os.environ, KHDAYS_ROM=str(rom), SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="offscreen",
               KHDAYS_TEST_WINDOW="1", KHDAYS_NET_SERVER="", KHDAYS_MOBILE_WINDOW=f"{w}x{h}", KHDAYS_MOBILE_LAYOUT="2",
               KHDAYS_MOBILE_INSETS=",".join(map(str, margins)), VK_DRIVER_FILES=str(LAVAPIPE),
               KHDAYS_TRACE_SITUATION="1", KHDAYS_PAD_SCRIPT=script)
    env.update(extra)
    for name in ("KHDAYS_HEADLESS", "KHDAYS_SETTINGS", "KHDAYS_INPUT", "KHDAYS_SHOT_WINDOW"):
        env.pop(name, None)
    if pictures is not None:
        pictures.mkdir(parents=True, exist_ok=True)
        for old in pictures.glob("picture-*.png"):
            old.unlink()
        env["KHDAYS_SHOT_WINDOW"] = str(pictures)
    with tempfile.TemporaryDirectory(dir=Path.cwd()) as work:
        if settings is not None:
            (Path(work) / "saves").mkdir()
            (Path(work) / "saves" / "settings.ini").write_text(settings)
        if mod is not None:
            (Path(work) / "mods").mkdir()
            (Path(work) / "mods" / mod.name).symlink_to(mod, target_is_directory=True)
        if videos is not None:
            # the player's HD videos where the phone looks for them: a link, never a copy
            (Path(work) / "data" / "cutscenes").mkdir(parents=True)
            (Path(work) / "data" / "cutscenes" / "cinematics").symlink_to(videos, target_is_directory=True)
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
        data = log.read_bytes()
        if pictures is not None:
            (pictures / "console.txt").write_bytes(
                b"\n".join(line for line in data.splitlines() if not line.startswith(b"khdays-native: frame")))
        return data.decode(errors="replace").splitlines(), exited


HEADER = re.compile(r"^picture (\d+) \(game frame \d+, scene \d+\): window (\d+)x(\d+), .*; drawn as ([^;]+); "
                    r"(\d+) columns, .* logical (\d+)x(\d+) in output")
LAYOUT = re.compile(r"^picture (\d+): layout (portrait|landscape) \(safe area (\d+)x(\d+) pixels\), "
                    r"game picture (-?\d+),(-?\d+) (\d+)x(\d+), lower view (none|below the picture|over the picture) "
                    r"(-?\d+),(-?\d+) (\d+)x(\d+), stylus (-?\d+),(-?\d+) (\d+)x(\d+) \(window pixels\); (.*)$")
LIT = re.compile(r"^picture (\d+): (\d+)x(\d+) read back, ([\d.]+)% lit, within (\d+),(\d+) to (\d+),(\d+),")
SAVED = re.compile(r"^picture (\d+): saved to (\S+)")
KEYS = re.compile(r"^picture (\d+): touch keys (drawn|not drawn) \(settings (open|closed), a controller "
                  r"(connected|not connected)[^)]*\), their top at y (-?\d+)")


class Report:
    """One picture report of the window's: its three lines after a script's "!report"."""

    def __init__(self, lines, start):
        self.ok = False
        self.at = start
        for n in range(start, min(len(lines), start + 400)):
            head = HEADER.match(lines[n])
            if not head:
                continue
            number = head[1]
            self.way, self.logical = head[4], (int(head[6]), int(head[7]))
            self.window = (int(head[2]), int(head[3]))
            for m in range(n + 1, min(len(lines), n + 12)):
                layout, lit, saved = LAYOUT.match(lines[m]), LIT.match(lines[m]), SAVED.match(lines[m])
                keys = KEYS.match(lines[m])
                if keys and keys[1] == number:
                    self.settings, self.controller = keys[3], keys[4]
                    self.keys, self.keys_top = keys[2], int(keys[5])
                if layout and layout[1] == number:
                    self.orientation = layout[2]
                    self.picture = tuple(int(layout[k]) for k in range(5, 9))
                    self.view_where = layout[9]
                    self.view = tuple(int(layout[k]) for k in range(10, 14))
                    self.stylus = tuple(int(layout[k]) for k in range(14, 18))
                    self.fill = layout[18]
                if lit and lit[1] == number:
                    self.read = (int(lit[2]), int(lit[3]))
                    self.share = float(lit[4])
                    self.box = tuple(int(lit[k]) for k in range(5, 9))
                    self.at = m
                if saved and saved[1] == number:
                    self.saved = saved[2]
            self.ok = hasattr(self, "orientation") and hasattr(self, "read")
            return


def inside(rect, area, slack):
    """Whether x, y, w, h lies in the area x0, y0, x1, y1, give or take `slack`."""
    x, y, w, h = rect
    return x >= area[0] - slack and y >= area[1] - slack and x + w <= area[2] + slack and y + h <= area[3] + slack


FILL_SLACK = 2  # pixels of rounding at an edge of the window
CORNER = 12  # the visible safe area: the safe area kept this clear of every edge (mobile_touch.h, KHDAYS_HUD_CORNER)


def visible(shape):
    """The visible safe area x0, y0, x1, y1 the HUD and the lower-screen view keep to (the round 5 HUD stage)."""
    (w, h), m = shape
    return (max(m[0], CORNER), max(m[1], CORNER), w - max(m[2], CORNER), h - max(m[3], CORNER))


def check_shape(c, lines, what, shape, at, view, least, one_picture, inset=False, touch=False, black_edges=None):
    """Reads the report after line `at` and checks it for `shape`; `view`: None (off), "below" or "over";
    `inset`: the settings file's smaller view at the right, else the full one in the middle; `touch`:
    played by touch (no controller: the touch keys are drawn). Single screen FILLS the window (the owner's
    rule, 2026-11-21): what is lit is the whole window, edge to edge, under the unsafe margins too (`black_edges`:
    the reason the game's own black may leave an edge unlit, None for none), the game's picture covers it (upright
    with the view on: across its width at its top, the view below), and only the touch keys keep to the safe area.
    `one_picture`: the one-screen picture, else the upper screen of a scene that shows both. Returns the line the
    report ended at."""
    (w, h), margins = shape
    safe = (margins[0], margins[1], w - margins[2], h - margins[3])
    safe_w, safe_h = safe[2] - safe[0], safe[3] - safe[1]
    upright = safe_h > safe_w
    at = c.expect(f"{what}: a report is asked for", r"pad script: .* !report", at)
    r = Report(lines, at)
    c.check(f"{what}: the picture is reported", r.ok)
    if not r.ok:
        return at
    # a picture is placed by whole dots of its own (SDL's viewport): right to within one of them
    dot = max(r.picture[2] / r.logical[0], 1.0)
    slack = math.ceil(dot) + 2
    window = (0, 0, w, h)
    detail = (f"{r.way}, {r.share}% lit within {r.box[0]},{r.box[1]} to {r.box[2]},{r.box[3]}; game picture "
              f"{r.picture[0]},{r.picture[1]} {r.picture[2]}x{r.picture[3]}; view {r.view_where} {r.view[0]},{r.view[1]} "
              f"{r.view[2]}x{r.view[3]}; stylus {r.stylus[0]},{r.stylus[1]} {r.stylus[2]}x{r.stylus[3]}; {r.fill}")
    c.check(f"{what}: the picture read back is {w}x{h}", r.read == (w, h) and r.window == (w, h), f"{r.read[0]}x{r.read[1]}")
    c.check(f"{what}: the console says {'portrait' if upright else 'landscape'}",
            r.orientation == ("portrait" if upright else "landscape"), r.orientation)
    c.check(f"{what}: at least {least}% is lit", r.share >= least, detail)
    edges = (r.box[0] <= FILL_SLACK and r.box[1] <= FILL_SLACK and r.box[2] >= w - 1 - FILL_SLACK and
             r.box[3] >= h - 1 - FILL_SLACK)
    if black_edges is None and upright and view == "below":
        # the room under the view: the lower screen, dimmed, covering it -- its own dark rows (the field's DAY panel,
        # black on the DS) may leave the window's foot unlit; the other three edges must be lit
        c.check(f"{what}: what is lit reaches the window's top and sides", r.box[0] <= FILL_SLACK and r.box[1] <= FILL_SLACK and
                r.box[2] >= w - 1 - FILL_SLACK, detail)
        if not edges:
            print(f"{c.name}: {what}: the foot not lit to the edge, allowed: the dimmed lower screen's own black rows "
                  f"under the view ({detail})")
    elif black_edges is None:
        c.check(f"{what}: what is lit is the whole window, edge to edge", edges, detail)
    elif not edges:
        print(f"{c.name}: {what}: not lit to every edge, allowed: {black_edges} ({detail})")
    c.check(f"{what}: the console says the picture covers the window", r.fill.startswith(f"fill: ") and
            f"covering the window {w}x{h} " in r.fill, r.fill)
    if upright and view is not None:
        # (the view below it, or over it when played by touch and the keys leave too little room below)
        c.check(f"{what}: upright with the view, the picture spans the window's width at its top",
                r.picture[0] <= 1 and r.picture[0] + r.picture[2] >= w - 1 and r.picture[1] <= 1, detail)
    else:
        c.check(f"{what}: the game's picture covers the window",
                r.picture[0] <= 1 and r.picture[1] <= 1 and r.picture[0] + r.picture[2] >= w - 1 and
                r.picture[1] + r.picture[3] >= h - 1, detail)
        c.check(f"{what}: it is centred", abs(r.picture[0] + r.picture[2] / 2 - w / 2) <= slack and
                abs(r.picture[1] + r.picture[3] / 2 - h / 2) <= slack, detail)
    c.check(f"{what}: it is the {'one-screen picture' if one_picture else 'upper screen of both'}",
            r.way == ("the one-screen picture" if one_picture else "the upper screen filling the window, both screens' scene"),
            r.way)
    if view is None:
        c.check(f"{what}: no lower-screen view, no stylus", r.view_where == "none" and r.stylus[2] == 0, detail)
    else:
        c.check(f"{what}: the lower-screen view is {view} the picture", r.view_where == f"{view} the picture", detail)
        c.check(f"{what}: the view lies in the window", r.view[2] > 0 and inside(r.view, window, slack), detail)
        vis = visible(shape)
        vis_w, vis_h = vis[2] - vis[0], vis[3] - vis[1]
        c.check(f"{what}: the view lies inside the visible safe area {vis}", inside(r.view, vis, slack), detail)
        c.check(f"{what}: the view has the DS screen's shape", abs(r.view[2] * 3 - r.view[3] * 4) <= 8, detail)
        c.check(f"{what}: the stylus rectangle is the view's", r.stylus == r.view, detail)
        if view == "below":
            c.check(f"{what}: the view starts under the picture", r.view[1] >= r.picture[1] + r.picture[3] - 1, detail)
        else:
            c.check(f"{what}: the view lies in the picture",
                    inside(r.view, (r.picture[0], r.picture[1], r.picture[0] + r.picture[2], r.picture[1] + r.picture[3]), 2),
                    detail)
        # the style: full in the middle, or the inset -- 0.62 of that -- at the right
        middle = r.view[0] + r.view[2] / 2
        if inset and view == "over":
            c.check(f"{what}: the inset view is the smaller one, in the picture's right half",
                    r.view[3] <= 0.7 * r.picture[3] and middle > r.picture[0] + r.picture[2] / 2 + r.view[2] / 8, detail)
        elif inset:
            c.check(f"{what}: the inset view is the smaller one, at the visible safe area's right",
                    r.view[2] <= 0.7 * w and r.view[0] + r.view[2] >= vis[2] - slack - 4 * dot - 2 and
                    r.view[0] + r.view[2] <= vis[2] + slack, detail)
        elif view == "over":
            # (as large as the visible safe area takes it: its height, or its width where that is narrower)
            c.check(f"{what}: the full view is as large as the visible safe area takes, in its middle",
                    r.view[3] >= min(vis_h, vis_w * 3 / 4, r.picture[3]) - 2 * slack and
                    abs(middle - (vis[0] + vis[2]) / 2) <= slack + 1, detail)
        else:
            c.check(f"{what}: the full view is in the middle, as wide as the visible safe area or down to its foot",
                    abs(middle - (vis[0] + vis[2]) / 2) <= slack + 1 and
                    (r.view[2] >= vis_w - 2 * slack or touch or r.view[1] + r.view[3] >= vis[3] - 3 * dot - slack),
                    detail)
    if touch:
        ok = hasattr(r, "keys")
        c.check(f"{what}: the touch keys are drawn (no controller)", ok and r.keys == "drawn" and r.controller == "not connected",
                f"{r.keys}, a controller {r.controller}" if ok else "no line about the keys")
        if ok and view is not None and upright:
            # upright, the view is the DS's lower screen to a finger: no key or stick zone lies over it
            c.check(f"{what}: the view ends above the touch keys", r.view[1] + r.view[3] <= r.keys_top + 1,
                    f"the view's foot at {r.view[1] + r.view[3]}, the keys' top at {r.keys_top}")
        if ok:
            c.check(f"{what}: the touch keys are in the safe area, at its foot", safe[1] < r.keys_top < safe[3],
                    f"their top at {r.keys_top}")
    if hasattr(r, "saved"):
        print(f"{c.name}: {what}: saved as {Path(r.saved).name}")
    return r.at


def check_run(c, turns, pad_stays=True):
    """What holds for a whole run: every turn said, nothing restarted, nothing lost."""
    said = [m[1] for line in c.lines if (m := re.match(r"window: orientation (portrait|landscape) after ", line))]
    # (the first is the window's opening; a turn may be said twice: its margins, then its size)
    squeezed = [s for n, s in enumerate(said) if n == 0 or s != said[n - 1]]
    # (two shapes of one orientation in a row -- folded lying, then the console's window lying -- are one word)
    turns = [s for n, s in enumerate(turns) if n == 0 or s != turns[n - 1]]
    c.check("each turn is said on the console with its layout", squeezed == turns, " ".join(squeezed))
    opened = sum("host: sound: stream opened" in line for line in c.lines)
    closed = sum("host: sound: stream closed" in line or "host: no sound" in line for line in c.lines)
    c.check("the sound stream is opened once and never closed", opened == 1 and closed == 0, f"{opened} opened, {closed} closed")
    bad = [line for line in c.lines
           if re.search(r"device was lost|cannot be read back|did not take it|ALL BLACK|stopped answering|cannot open", line)]
    # (the first pictures of a run are black: the game has drawn nothing yet; and a periodic sample -- pictures 300, 900,
    # 1800 -- can fall on the game's own black between two scenes, scene 5, the filled picture's slower pace on the CPU
    # card moving them: seen at picture 1800, game frame 2432, scene 5)
    bad = [line for line in bad if not re.match(r"picture ([1-9]|[1-9]\d): ", line) and
           not re.match(r"picture (300|900|1800): \d+x\d+ read back, ALL BLACK", line)]
    c.check("no picture is lost, black or refused", not bad, bad[0][:160] if bad else "")
    if pad_stays:
        gone = sum("the gamepad is gone" in line or "gamepad disconnected" in line for line in c.lines)
        c.check("the gamepad stays connected", gone == 0)


def words(orders):
    return ["portrait" if (s[0][1] - s[1][1] - s[1][3]) > (s[0][0] - s[1][0] - s[1][2]) else "landscape" for s in orders]


def title_case(exe, rom, pictures):
    order = [UNFOLDED_L, UNFOLDED_P, UNFOLDED_L, FOLDED_P, FOLDED_L, CONSOLE_L, CONSOLE_P, UNFOLDED_P, UNFOLDED_L]
    # the boot logos first (frame 300, before the title's menu), then the title through every shape
    script = ["0*ps5", "300!report", "t", "60!report"]
    for n, shape in enumerate(order[1:]):
        script += [turn(120 + 150 * n, shape), f"{120 + 150 * n + 100}!report"]
    end = 120 + 150 * (len(order) - 1)
    script += [press(end, "touchpad"), f"{end + 80}!report"]
    # two fingers on the view (the lower screen whole over the covering picture): its middle and a point a quarter
    # of the way in, in the window's points (its pixels here) -- each must land on the DS's lower screen there
    taps = [(1426, 1003), (755, 500)]
    script += [f"{end + 120 + 10 * n}!touch={x},{y}" for n, (x, y) in enumerate(taps)]
    c = Checks("title", play(exe, rom, " ".join(script), order[0], 300, pictures=pictures and pictures / "title"))
    # the logos: the upper screen covering the window (its lower one, the video codec's, with the view)
    at = check_shape(c, c.lines, "the boot logos, unfolded lying", order[0], 0, None, 30.0, True)
    at = c.expect("the title's menu shows", r"pad script: frame \d+: anchor t0 begins", at)
    names = ["unfolded lying", "unfolded upright", "unfolded lying again", "folded upright", "folded lying",
             "the phone console's window lying", "the phone console's window upright", "unfolded upright again",
             "unfolded lying at the end"]
    for name, shape in zip(names, order):
        at = check_shape(c, c.lines, name, shape, at, None, 30.0, True)
    # the lower-screen view on the title: its whole lower screen (the menu) over the picture
    at = check_shape(c, c.lines, "unfolded lying, the view on", order[-1], at, "over", 30.0, True)
    asked = max((i for i in range(min(at, len(c.lines) - 1) + 1) if re.match(r"pad script: .* !report", c.lines[i])), default=0)
    r = Report(c.lines, asked)  # (the view's report again: where its rectangle is)
    for x, y in taps:
        n = c.expect(f"a finger at {x},{y} on the view is the stylus", rf"touch: a finger at {x},{y} \(window points\) is the stylus", at)
        m = re.search(r"stylus at DS ([\d.]+),([\d.]+)", c.lines[n])
        if m and r.ok:
            vx, vy, vw, vh = r.view
            want = ((x - vx) * 256 / vw, (y - vy) * 192 / vh)
            c.check(f"the finger at {x},{y} lands on the DS's lower screen where the view shows it",
                    abs(float(m[1]) - want[0]) <= 1.5 and abs(float(m[2]) - want[1]) <= 1.5,
                    f"DS {m[1]},{m[2]}, the view at {vx},{vy} {vw}x{vh} puts it at {want[0]:.1f},{want[1]:.1f}")
    check_run(c, words(order))
    if c.failed:
        c.tail()
    return c.failed == 0


def field(exe, rom, pictures, name, settings):
    start = FIELD.index(press(2300, "start"))
    into_field = FIELD[:start] + press(2300, "start")  # (the tutorial closed: the field's own picture)
    lying, upright, folded_up, folded_lying = points(UNFOLDED_L), points(UNFOLDED_P), points(FOLDED_P), points(FOLDED_L)
    steps = [
        "2400!report",                                    # lying, no view
        press(2450, "touchpad"), "2520!report",           # lying, the view over the picture
        turn(2560, upright), "2680!report",               # upright, the view below it
        press(2720, "touchpad"), "2790!report",           # upright, no view
        "2820>rx:25000",                                  # the right stick held from here ...
        turn(2880, lying), "3000!report",                 # lying
        turn(3040, folded_up), "3160!report",             # folded upright
        "3200>rx:0",                                      # ... to here, through two turns
        press(3230, "touchpad"), "3300!report",           # folded upright, the view below
        turn(3340, folded_lying), "3460!report",          # folded lying, the view over the picture
        turn(3500, upright), "3620!report",               # unfolded upright, the view below again
        press(3660, "touchpad"), "3730!report",           # no view
        turn(3770, lying), "3890!report",                 # as it began
        "3930/",                                          # the controller pulled out: the game waits
        turn(3960, upright), "4050!report",               # turned upright while it waits
        "4090!touch=334,300", "4170!report",              # a tap (on no key): played by touch
        "4200!touch=366,911", "4280!report",              # the Screen key (unfolded upright): the view on
        turn(4320, folded_up), "4440!report",             # folded upright: the view below, above the keys
        turn(4480, lying), "4600!report",                 # lying: over the picture
    ]
    c = Checks(name, play(exe, rom, into_field + " " + " ".join(steps), lying, 900,
                          {"KHDAYS_TRACE_CONTROLS": "1"}, settings, pictures and pictures / name))
    scenes = [int(m[1], 16) for line in c.lines if (m := re.match(r"situation: frame \d+, scene 0x[\da-f]+ -> 0x([\da-f]+)$", line))]
    scenes = [s for s in scenes if s]
    c.check("the DualSense takes a new game to the field", scenes[:6] == [1, 11, 5, 2, 10, 5] and scenes[-1] == 2,
            "scenes " + " ".join(map(str, scenes)))
    at = c.expect("the tutorial closes", r"situation: frame \d+, tutorial 0x[\da-f]+ -> 0x0")
    inset = settings is not None and "lower_view = inset" in settings
    plan = [("lying", lying, None), ("lying, the view on", lying, "over"), ("turned upright, the view on", upright, "below"),
            ("upright, the view off", upright, None), ("turned lying, the stick held", lying, None),
            ("folded upright, the stick held", folded_up, None), ("folded upright, the view on", folded_up, "below"),
            ("folded, turned lying, the view on", folded_lying, "over"),
            ("unfolded upright, the view on", upright, "below"), ("upright, the view off again", upright, None),
            ("lying at the end", lying, None)]
    for what, shape, view in plan:
        at = check_shape(c, c.lines, what, shape, at, view, 25.0, True, inset)
    # the controller pulled out with the phone lying; turned upright while the game waits (it sends no
    # frame of the new width): its picture is shown whole across the safe area, not cut at the sides
    at = c.expect("the controller is pulled out: the game waits", r"the gamepad is gone: the game waits", at)
    at = check_shape(c, c.lines, "turned upright while the game waits", upright, at, None, 8.0, True)
    at = c.expect("a tap: played by touch from here", r"host: the game goes on", at)
    at = check_shape(c, c.lines, "upright, by touch", upright, at, None, 25.0, True, inset, True)
    at = c.expect("the Screen key shows the view", r"lower view: on", at)
    # unfolded upright the room between the picture and the keys is too small for the view: over the picture
    at = check_shape(c, c.lines, "upright, by touch, the view on", upright, at, "over", 25.0, True, inset, True)
    at = check_shape(c, c.lines, "folded upright, by touch, the view on", folded_up, at, "below", 25.0, True, inset, True)
    at = check_shape(c, c.lines, "lying, by touch, the view on", lying, at, "over", 25.0, True, inset, True)
    # the right stick held through two turns: the camera turns before the first, between them and after the second
    held = c.index(r"pad 1 >rx:25000")
    let_go = c.index(r"pad 1 >rx:0", held)
    turns = [n for n in range(held, let_go) if re.match(r"window: orientation (portrait|landscape) after a size change", c.lines[n])]
    c.check("two turns happen while the right stick is held", held >= 0 and let_go > held and len(turns) == 2, f"{len(turns)} turns")
    yaw = re.compile(r"camera: .* yaw ([\da-f]+) ")

    def yaws(first, last):
        return {m[1] for n in range(first, last) if (m := yaw.search(c.lines[n]))}

    if len(turns) == 2:
        before, between, after = yaws(held, turns[0]), yaws(turns[0], turns[1]), yaws(turns[1], let_go)
        c.check("the camera turns before the first turn", len(before) >= 3, f"{len(before)} yaws")
        c.check("the stick is still held after the first turn: the camera keeps turning", len(between) >= 5, f"{len(between)} yaws")
        c.check("and after the second", len(after) >= 5, f"{len(after)} yaws")
        c.check("no button or stick event comes with a turn",
                not any(re.search(r"host: gamepad (button|now playing)", c.lines[n]) for n in range(held, let_go)))
    still = yaws(let_go + 40, c.index(r"pad 1 \+touchpad", let_go)) if let_go > 0 else {"?", "??"}
    c.check("the camera stops when the stick is let go", len(still) <= 1, f"{len(still)} yaws")
    order = [lying, upright, lying, folded_up, folded_lying, upright, lying, upright, lying]
    check_run(c, words(order), pad_stays=False)
    c.check("the gamepad is lost once, where the script pulls it out",
            sum("the gamepad is gone" in line for line in c.lines) == 1)
    if c.failed:
        c.tail()
    return c.failed == 0


def waits_case(exe, rom, pictures):
    lying, upright, folded_up = points(UNFOLDED_L), points(UNFOLDED_P), points(FOLDED_P)
    script = ["0*ps5", "t", "60/", turn(120, upright), "200!report", turn(260, lying), "320!report",
              # the app hidden, the phone turned meanwhile, the app back: no picture is presented between
              "360!background", turn(380, upright), "440!foreground", "500!background", turn(520, lying),
              "580!foreground",
              "680*ps5", press(740, "guide"), turn(820, upright), "900!report", turn(960, folded_up), "1040!report",
              turn(1100, lying), "1180!report", press(1240, "guide"), "1340!report"]
    c = Checks("waits", play(exe, rom, " ".join(script), lying, 240, pictures=pictures and pictures / "waits"))
    at = c.expect("the controller is lost on the title: the game waits", r"the gamepad is gone: the game waits")
    lost = at

    def report(what, shape, settings, controller, least):
        nonlocal at
        (w, h), _ = shape
        at = c.expect(f"{what}: a report is asked for", r"pad script: .* !report", at)
        r = Report(c.lines, at)
        ok = r.ok and hasattr(r, "settings")
        c.check(f"{what}: the picture is reported", ok)
        if ok:
            detail = f"{r.read[0]}x{r.read[1]}, {r.share}% lit, settings {r.settings}, a controller {r.controller}"
            c.check(f"{what}: the picture read back is {w}x{h}, lit, the settings menu {settings}",
                    r.read == (w, h) and r.share >= least and r.settings == settings and r.controller == controller, detail)
            c.check(f"{what}: the console says {'portrait' if h > w else 'landscape'}",
                    r.orientation == ("portrait" if h > w else "landscape"), r.orientation)
            at = r.at
            if hasattr(r, "saved"):
                print(f"{c.name}: {what}: saved as {Path(r.saved).name}")

    report("waiting, turned upright", upright, "closed", "not connected", 20.0)
    report("waiting, turned lying", lying, "closed", "not connected", 20.0)
    # turned while the app was in the background: the picture reported at the return (drawn by the
    # return itself: the game, waiting, sends none) has the turned window's size, not the size SDL's
    # backbuffer kept from before
    for what, shape in (("turned upright in the background", upright), ("turned lying in the background", lying)):
        (w, h), _ = shape
        at = c.expect(f"{what}: the app goes to the background", r"window: the app: did enter background", at)
        turned = c.expect(f"{what}: the turn is taken while it is hidden",
                          r"window: orientation (portrait|landscape) after a (size|safe-area|pixel-size) change", at)
        at = c.expect(f"{what}: the app comes back", r"window: the app: did enter foreground", turned)
        c.expect(f"{what}: the picture is laid out and drawn anew at the return",
                 r"window: orientation (portrait|landscape) after the return to the foreground", at)
        r = Report(c.lines, at)  # (the second of the return's two pictures; said before that line)
        c.check(f"{what}: a picture is reported at the return", r.ok)
        if r.ok:
            c.check(f"{what}: it has the turned window's size, {w}x{h}", r.read == (w, h) and r.window == (w, h),
                    f"{r.read[0]}x{r.read[1]} read back, window {r.window[0]}x{r.window[1]}")
            c.check(f"{what}: it is lit and says {'portrait' if h > w else 'landscape'}",
                    r.share >= 20.0 and r.orientation == ("portrait" if h > w else "landscape"), f"{r.share}% lit, {r.orientation}")
            at = r.at
    back = c.expect("the controller comes back: the game goes on", r"host: the game goes on", at)
    c.check("the game waited through both turns", not any("the game goes on" in line for line in c.lines[lost:back]))
    at = back
    report("the settings menu, turned upright", upright, "open", "connected", 50.0)
    report("the settings menu, folded upright", folded_up, "open", "connected", 50.0)
    report("the settings menu, turned lying", lying, "open", "connected", 50.0)
    report("the menu closed", lying, "closed", "connected", 30.0)
    check_run(c, words([lying, upright, lying, upright, lying, upright, lying]), pad_stays=False)
    if c.failed:
        c.tail()
    return c.failed == 0


def videos_folder():
    """The player's HD videos (KHDAYS_HD_VIDEOS, else the work folder's private/cinematics), or None."""
    named = os.environ.get("KHDAYS_HD_VIDEOS")
    folder = Path(named) if named else ROOT.parent / "private" / "cinematics"
    packed = ROOT.parent / "private" / "hd" / "data" / "cutscenes" / "cinematics"
    if not named and (packed / "hd802.mp4").is_file():
        folder = packed
    return folder.resolve() if (folder / "hd802.mp4").is_file() else None


def movie_case(exe, rom, pictures):
    """A new game's opening movie as the player's HD video (hd802), turned through the five shapes while it
    plays, then paused: it covers the window in each, its pause panel whole in the safe area."""
    videos = videos_folder()
    if videos is None:
        print("movie: no HD videos (KHDAYS_HD_VIDEOS or private/cinematics with hd802.mp4): skipped")
        return True
    order = [UNFOLDED_L, UNFOLDED_P, CONSOLE_L, FOLDED_P, FOLDED_L, UNFOLDED_L]
    script = ["0*ps5", "t", press(130, "south"), press(330, "south"), press(530, "south"), press(680, "dpleft"),
              press(740, "south"), "s11", "200!report"]
    for n, shape in enumerate(order[1:]):
        script += [turn(300 + 200 * n, shape), f"{300 + 200 * n + 120}!report"]
    script += [press(300 + 200 * (len(order) - 1), "start")]
    c = Checks("movie", play(exe, rom, " ".join(script), order[0], 700, {"KHDAYS_TRACE_MOVIE_HD": "1"},
                             pictures=pictures and pictures / "movie", videos=videos))
    if any("external HD video playback is unavailable" in line for line in c.lines):
        print("movie: this build has no HD player (no FFmpeg libraries found): skipped")
        return True
    at = c.expect("the opening plays as the player's HD video", r"movies: start hd802\.mp4")
    if (videos / "subtitles/en/hd802.txt").is_file():
        c.expect("the HD pack supplies the opening subtitles", r"movies: 802: [1-9]\d* subtitle lines from")
    names = ["unfolded lying", "unfolded upright", "the phone console's window lying", "folded upright",
             "folded lying", "unfolded lying again"]
    for name, shape in zip(names, order):
        (w, h), _ = shape
        at = c.expect(f"{name}: a report is asked for", r"pad script: .* !report", at)
        r = Report(c.lines, at)
        c.check(f"{name}: the movie is reported", r.ok)
        if not r.ok:
            continue
        detail = (f"{r.way}, {r.share}% lit within {r.box[0]},{r.box[1]} to {r.box[2]},{r.box[3]}; picture "
                  f"{r.picture[0]},{r.picture[1]} {r.picture[2]}x{r.picture[3]}; {r.fill}")
        c.check(f"{name}: it is the HD movie, read back {w}x{h}", r.way == "an HD movie" and r.read == (w, h), detail)
        c.check(f"{name}: what is lit is the whole window, edge to edge",
                r.box[0] <= FILL_SLACK and r.box[1] <= FILL_SLACK and r.box[2] >= w - 1 - FILL_SLACK and
                r.box[3] >= h - 1 - FILL_SLACK, detail)
        c.check(f"{name}: the video covers the window, centred, its overflow cut",
                r.fill.startswith("fill: ") and f"covering the window {w}x{h} " in r.fill and r.picture[0] <= 1 and
                r.picture[1] <= 1 and r.picture[0] + r.picture[2] >= w - 1 and r.picture[1] + r.picture[3] >= h - 1 and
                abs(r.picture[0] + r.picture[2] / 2 - w / 2) <= 2 and abs(r.picture[1] + r.picture[3] / 2 - h / 2) <= 2,
                detail)
        at = r.at
    at = c.expect("Options pauses the video", r"movies: pause hd802\.mp4 at", at)
    panel = c.expect("its pause panel shows", r"movies: the pause panel at", at)
    m = re.search(r"pause panel at (-?\d+),(-?\d+) (\d+)x(\d+)", c.lines[panel])
    if m:
        (w, h), margins = order[-1]
        x, y, pw, ph = (int(m[k]) for k in range(1, 5))
        c.check("the pause panel is whole in the safe area, 4:3",
                x >= margins[0] - 1 and y >= margins[1] - 1 and x + pw <= w - margins[2] + 1 and
                y + ph <= h - margins[3] + 1 and abs(pw * 3 - ph * 4) <= 8, c.lines[panel])
    check_run(c, words(order))
    if c.failed:
        c.tail()
    return c.failed == 0


def hd_title_case(exe, rom, pictures):
    """Read the private title art and tap its actual Settings button after a live turn."""
    mod = ROOT.parent / "private" / "hd" / "mods" / "HD PACK"
    layout = mod / "layouts/title/en/root_2_0.txt"
    if not layout.is_file():
        print("hd-title: no private theater title layout: skipped")
        return True
    bounds = re.search(r"^button open_settings ([^\n]+)", layout.read_text(), re.M)
    if not bounds:
        print("hd-title: private theater layout is missing its Settings button")
        return False
    left, top, right, bottom = map(float, bounds[1].split())
    x0, y0, x1, y1 = visible(UNFOLDED_P)
    scale = min((x1-x0)/1920, (y1-y0)/1080)
    x = round((x0+x1)/2 + ((left+right)/2-960)*scale)
    y = round((y0+y1)/2 + ((top+bottom)/2-540)*scale)
    script = " ".join(["0*ps5", "t", "200!report", turn(280, UNFOLDED_P), "480!report",
                       f"600!touch={x},{y}", "680!report"])
    c = Checks("hd-title", play(exe, rom, script, CONSOLE_L, 300, mod=mod,
                                pictures=pictures and pictures / "hd-title"))
    at = 0
    for shape in (CONSOLE_L, UNFOLDED_P):
        at = c.expect("read theater title", r"pad script: .* !report", at)
        r = Report(c.lines, at)
        w, h = shape[0]
        c.check(f"HD title fills {w}x{h}", r.ok and r.read == (w, h) and r.way == "the one-screen picture" and
                r.box[0] <= FILL_SLACK and r.box[1] <= FILL_SLACK and
                r.box[2] >= w - 1 - FILL_SLACK and r.box[3] >= h - 1 - FILL_SLACK,
                f"lit bounds {r.box}" if r.ok else "missing readback")
        at = r.at + 1
    at = c.expect("tap the portrait Settings button", r"pad script: .* !touch=", at)
    c.expect("the tap opened Settings inside the safe area", r"overlay settings menu at .*inside", at)
    if c.failed:
        c.tail()
    return c.failed == 0


def ds_movie_case(exe, rom, pictures):
    """Check the drawable itself for the 16-row borders around the native 256x160 video."""
    script = ["0*ps5", "t", press(130, "south"), press(330, "south"), press(530, "south"),
              press(680, "dpleft"), press(740, "south"), "s11", press(400, "start"), "s2",
              "650!report", turn(720, UNFOLDED_P), "950!report"]
    c = Checks("ds-movie", play(exe, rom, " ".join(script), CONSOLE_L, 300,
                                settings="[video]\nmovies = ds\n",
                                pictures=pictures and pictures / "ds-movie"))
    at = c.expect("the field movie is playing", r"one screen: frame \d+, movie", c.expect(
        "the first field has started", r"anchor s2 begins"))
    for shape in (CONSOLE_L, UNFOLDED_P):
        at = c.expect("read the playing movie", r"pad script: .* !report", at)
        r = Report(c.lines, at)
        w, h = shape[0]
        c.check(f"native movie fills {w}x{h}", r.ok and r.read == (w, h) and
                r.box[0] <= FILL_SLACK and r.box[1] <= FILL_SLACK and
                r.box[2] >= w - 1 - FILL_SLACK and r.box[3] >= h - 1 - FILL_SLACK,
                f"lit bounds {r.box}" if r.ok else "missing readback")
        at = r.at + 1
    if c.failed:
        c.tail()
    return c.failed == 0


def main():
    sys.stdout.reconfigure(line_buffering=True)  # (each line seen as it comes, under CTest too)
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exe", type=Path)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--only", choices=("title", "field", "inset", "waits", "movie", "ds-movie", "hd-title"))
    parser.add_argument("--pictures", type=Path, help="keep each case's reported pictures and console in this folder")
    args = parser.parse_args()
    if not LAVAPIPE.is_file():
        print("No CPU Vulkan driver (lavapipe) here: skipped")
        return 77
    exe, rom = args.exe.resolve(), args.rom.resolve()
    pictures = args.pictures.resolve() if args.pictures else None
    cases = (("title", lambda: title_case(exe, rom, pictures)),
             ("field", lambda: field(exe, rom, pictures, "field", None)),
             ("inset", lambda: field(exe, rom, pictures, "inset", "[video]\nlower_view = inset\n")),
             ("waits", lambda: waits_case(exe, rom, pictures)),
             ("movie", lambda: movie_case(exe, rom, pictures)),
             ("ds-movie", lambda: ds_movie_case(exe, rom, pictures)),
             ("hd-title", lambda: hd_title_case(exe, rom, pictures)))
    results = [case() for name, case in cases if args.only in (None, name)]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
