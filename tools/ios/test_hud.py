"""The HUD inside what the phone shows, on Linux: the picture fills the display, the HUD does not go under it.

Runs the phone's test build (CMake's KHDAYS_MOBILE_TEST) as test_rotate.py does -- the real window,
graphics-card and input path on SDL's hidden "offscreen" video driver and the CPU Vulkan driver (lavapipe),
the window turned by the pad script ("!size=", "!safe=") -- with the settings file's `show_fps = on`, and
reads the picture reports' HUD lines (host/app/window.c):

  picture N: visible safe area X0,Y0 to X1,Y1 (window pixels, C clear of the edges); P HUD pieces, O overlays
  picture N: hud piece <screen x,y wxh> at X0,Y0 to X1,Y1, inside|OUTSIDE
  picture N: overlay <what> at X0,Y0 to X1,Y1, inside|OUTSIDE

The visible safe area is the window's safe area kept 12 points clear of every edge (the rounded corners).
A HUD piece is each piece of the one-screen composition laid out in the HUD's frame (ui/composition.h): the
command deck, the player's and the party's health with the portraits, the hearts' counter (the HUD's 3D),
the map, the mission's panel and the INFORMATION banner, the countdown, the lower-screen view, a dialog's or
a list's screen, the title's small menu. An overlay is what the window draws over the picture: the FPS line,
the settings menu and its hints, the controller notice.

  field   a DualSense takes a new game into the first field, then the phone at every shape (the console's
          window lying and upright, the unfolded and folded phone lying and upright, at a third of their
          pixels: the CPU graphics driver is slow), with the lower-screen view on lying and upright, the
          settings menu open lying and upright, and the controller pulled out (its notice).
  title   the title menu at the phone's full pixel sizes: the console's window lying and upright, the folded
          phone upright (the small menu at the bottom, never cut at the sides).

Checked at each report: every HUD piece and overlay is inside the visible safe area; in the field the
command deck, the party's gauges and the hearts' counter are there (and, lying, the map); the FPS line is
there; with the view on, the view is a piece inside (lying) or the view below the picture ends above the
visible area's foot (upright); the settings menu is inside; the game is still running at the end.
"""
import argparse
from pathlib import Path
import re
import sys

from test_pad import FIELD, Checks, press
from test_rotate import CONSOLE_L, CONSOLE_P, FOLDED_L, FOLDED_P, UNFOLDED_L, UNFOLDED_P, play, points, turn

VISIBLE = re.compile(r"^picture (\d+): visible safe area (-?\d+),(-?\d+) to (-?\d+),(-?\d+) \(window pixels, (\d+) "
                     r"clear of the edges\); (\d+) HUD pieces, (\d+) overlays$")
PIECE = re.compile(r"^picture (\d+): (hud piece|overlay) (.+) at (-?\d+),(-?\d+) to (-?\d+),(-?\d+), (inside|OUTSIDE)$")
LAYOUT = re.compile(r"^picture (\d+): layout (portrait|landscape) .* lower view (none|below the picture|over the picture) "
                    r"(-?\d+),(-?\d+) (\d+)x(\d+),")
HEADER = re.compile(r"^picture (\d+) \(game frame \d+, scene \d+\): window (\d+)x(\d+),")

# the field's pieces by their source rectangles (ui/compositions/port_days.txt)
DECK = "upper 0,84 112x108"
PARTY = ("upper 128,84 128x107", "upper 128,64 128x127")
HEARTS = "3d hud -64,-64 192x141"
MAP = "lower 112,64 104x64"


def reports(lines, start=0):
    """Each report's window, visible area, layout and pieces, from line `start` on, in order."""
    out, header, layout = [], None, None
    for n in range(start, len(lines)):
        line = lines[n]
        if m := HEADER.match(line):
            header = (int(m[1]), int(m[2]), int(m[3]))
        elif m := LAYOUT.match(line):
            layout = (int(m[1]), m[2], m[3], tuple(int(m[k]) for k in range(4, 8)))
        elif m := VISIBLE.match(line):
            out.append({"n": int(m[1]), "line": n, "window": header, "layout": layout,
                        "visible": tuple(int(m[k]) for k in range(2, 6)), "corner": int(m[6]),
                        "count": (int(m[7]), int(m[8])), "pieces": []})
        elif (m := PIECE.match(line)) and out and int(m[1]) == out[-1]["n"]:
            out[-1]["pieces"].append((m[2], m[3], tuple(int(m[k]) for k in range(4, 8)), m[8] == "inside"))
    return out


def summary(r):
    return "; ".join(f"{name} {box[0]},{box[1]}-{box[2]},{box[3]}{'' if ok else ' OUTSIDE'}"
                     for _, name, box, ok in r["pieces"])


def check_report(c, r, what, shape, field=False, view=None, settings=False, fps=True, notice=False, table=None,
                 corner=12):
    """One report: the window's shape, every piece inside, the pieces expected there."""
    (w, h), margins = shape
    c.check(f"{what}: the window is {w}x{h}", r["window"] is not None and r["window"][1:] == (w, h),
            f"window {r['window']}")
    x0, y0, x1, y1 = r["visible"]
    # the visible area: the safe area (its margins in the window's units, a pixel each on this video driver), kept
    # `corner` clear of every edge (KHDAYS_HUD_CORNER: 12 points on the phone)
    want = (max(margins[0], corner), max(margins[1], corner), w - max(margins[2], corner), h - max(margins[3], corner))
    c.check(f"{what}: the visible safe area is the safe area kept {corner} clear of the edges",
            all(abs(a - b) <= 1 for a, b in zip((x0, y0, x1, y1), want)), f"{r['visible']} against {want}")
    outside = [p for p in r["pieces"] if not p[3]]
    c.check(f"{what}: every HUD piece and overlay inside the visible safe area ({len(r['pieces'])})",
            r["pieces"] and not outside, summary(r) if outside or not r["pieces"] else "")
    names = [p[1] for p in r["pieces"] if p[0] == "hud piece"]
    overlays = [p[1] for p in r["pieces"] if p[0] == "overlay"]
    if field:
        c.check(f"{what}: the command deck is there", DECK in names, " | ".join(names))
        c.check(f"{what}: the party's gauges and portraits are there", any(p in names for p in PARTY))
        c.check(f"{what}: the hearts' counter is there", HEARTS in names)
    if fps:
        c.check(f"{what}: the FPS line is there", "corner line 0" in overlays, " | ".join(overlays))
    if settings:
        c.check(f"{what}: the settings menu is there", "settings menu" in overlays, " | ".join(overlays))
    if notice:
        c.check(f"{what}: the controller notice is there", "controller notice 0" in overlays, " | ".join(overlays))
    if view == "over":
        c.check(f"{what}: the lower-screen view is a piece over the picture",
                any(n.startswith("lower 0,0 256x192") for n in names) or
                (r["layout"] is not None and r["layout"][2] == "over the picture"), " | ".join(names))
    if view == "below":
        lay = r["layout"]
        ok = lay is not None and lay[2] == "below the picture"
        if ok:
            vx, vy, vw, vh = lay[3]
            ok = vx >= x0 - 1 and vy >= y0 - 1 and vx + vw <= x1 + 1 and vy + vh <= y1 + 1
        c.check(f"{what}: the lower-screen view below the picture, inside the visible safe area", ok, f"{lay}")
    if table is not None:
        for kind, name, box, ok in r["pieces"]:
            table.append(f"{what} | {w}x{h} | {x0},{y0}-{x1},{y1} | {kind} {name} | {box[0]},{box[1]}-{box[2]},{box[3]} | "
                         f"{'inside' if ok else 'OUTSIDE'}")


def field_case(exe, rom, out):
    start = FIELD.index(press(2300, "start"))
    into_field = FIELD[:start] + press(2300, "start")  # (the tutorial closed: the field's own picture)
    console_l, console_p = points(CONSOLE_L), points(CONSOLE_P)
    lying, upright, folded_l, folded_p = points(UNFOLDED_L), points(UNFOLDED_P), points(FOLDED_L), points(FOLDED_P)
    steps = [
        "2400!report",                                   # the console's window lying
        press(2450, "touchpad"), "2520!report",          # the view on, over the picture
        turn(2560, console_p), "2680!report",            # upright: the view below
        press(2720, "touchpad"), "2790!report",          # upright, the view off: the picture covers
        turn(2830, lying), "2950!report",                # the unfolded phone lying (its side margins)
        turn(2990, upright), "3110!report",              # upright
        turn(3150, folded_l), "3270!report",             # folded lying
        turn(3310, folded_p), "3430!report",             # folded upright
        press(3470, "touchpad"), "3540!report",          # folded upright, the view below
        press(3580, "touchpad"),
        turn(3620, console_l), "3740!report",            # the console's window again
        press(3780, "guide"), "3850!report",             # the settings menu
        turn(3890, console_p), "4010!report",            # turned upright under it
        press(4050, "east"), press(4100, "east"), press(4150, "east"),
        turn(4200, console_l), "4320!report",            # lying, the menu closed
        "4360/", "4440!report",                          # the controller pulled out: its notice
    ]
    plan = [("console lying", console_l, {"field": True}),
            ("console lying, the view on", console_l, {"field": True, "view": "over"}),
            ("console upright, the view below", console_p, {"field": True, "view": "below"}),
            ("console upright, the view off", console_p, {"field": True}),
            ("unfolded lying", lying, {"field": True}),
            ("unfolded upright", upright, {"field": True}),
            ("folded lying", folded_l, {"field": True}),
            ("folded upright", folded_p, {"field": True}),
            ("folded upright, the view below", folded_p, {"field": True, "view": "below"}),
            ("console lying again", console_l, {"field": True}),
            ("console lying, the settings menu", console_l, {"settings": True, "fps": False}),
            ("console upright, the settings menu", console_p, {"settings": True, "fps": False}),
            ("console lying, the menu closed", console_l, {"field": True}),
            ("console lying, the controller pulled out", console_l, {"notice": True})]
    c = Checks("field", play(exe, rom, into_field + " " + " ".join(steps), console_l, 1000,
                             {"KHDAYS_TRACE_CONTROLS": "1"}, "[video]\nshow_fps = on\n", out and out / "field"))
    at = c.expect("the tutorial closes", r"situation: frame \d+, tutorial 0x[\da-f]+ -> 0x0")
    got = reports(c.lines, at if at >= 0 else 0)
    # the script's reports, in order (the periodic ones at pictures 300, 900, 1800 left out by the frame)
    asked = [n for n, line in enumerate(c.lines) if re.search(r"pad script: .*!report", line)]
    table = []
    for (what, shape, opts), ask in zip(plan, asked):
        following = [r for r in got if r["line"] > ask]
        if not following:
            c.check(f"{what}: reported", False)
            continue
        check_report(c, following[0], what, shape, table=table, **opts)
    c.check("every report the script asked for came", len(asked) == len(plan), f"{len(asked)} of {len(plan)}")
    if out is not None:
        (out / "field").mkdir(parents=True, exist_ok=True)
        (out / "field" / "hud-table.txt").write_text("\n".join(table) + "\n")
    if c.failed:
        c.tail()
    return c.failed == 0


def title_case(exe, rom, out):
    script = " ".join(["0*ps5", "t", "200!report", turn(240, CONSOLE_P), "400!report", turn(440, FOLDED_P),
                       "600!report", turn(640, FOLDED_L), "800!report"])
    # at the phone's full pixel sizes, the corners' 12 points at the console's density (2.806): 34 pixels
    c = Checks("title", play(exe, rom, script, CONSOLE_L, 900, {"KHDAYS_HUD_CORNER": "34"}, "[video]\nshow_fps = on\n",
                             out and out / "title"))
    asked = [n for n, line in enumerate(c.lines) if re.search(r"pad script: .*!report", line)]
    got = reports(c.lines)
    table = []
    plan = [("title, console lying", CONSOLE_L), ("title, console upright", CONSOLE_P),
            ("title, folded upright", FOLDED_P), ("title, folded lying", FOLDED_L)]
    for (what, shape), ask in zip(plan, asked):
        following = [r for r in got if r["line"] > ask]
        if not following:
            c.check(f"{what}: reported", False)
            continue
        r = following[0]
        check_report(c, r, what, shape, table=table, corner=34)
        names = [p[1] for p in r["pieces"] if p[0] == "hud piece"]
        c.check(f"{what}: the title's small menu (the lower screen) is a piece", any(n.startswith("lower 0,0 256x192") for n in names),
                " | ".join(names))
    c.check("every report the script asked for came", len(asked) == len(plan), f"{len(asked)} of {len(plan)}")
    if out is not None:
        (out / "title").mkdir(parents=True, exist_ok=True)
        (out / "title" / "hud-table.txt").write_text("\n".join(table) + "\n")
    if c.failed:
        c.tail()
    return c.failed == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exe")
    parser.add_argument("rom")
    parser.add_argument("--only", choices=("field", "title"))
    parser.add_argument("--out", type=Path, help="where to keep the console and pictures (none by default)")
    args = parser.parse_args()
    exe, rom = Path(args.exe), Path(args.rom)
    if not rom.is_file():
        print(f"no cartridge at {rom}: skipped")
        return 77
    cases = [("title", lambda: title_case(exe, rom, args.out)), ("field", lambda: field_case(exe, rom, args.out))]
    ok = True
    for name, run in cases:
        if args.only in (None, name):
            print(f"== {name}", flush=True)
            ok = run() and ok
    print("PASSED" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
