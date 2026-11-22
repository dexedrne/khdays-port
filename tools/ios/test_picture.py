"""Draw the phone's window on Linux and check its picture.

Runs the test build (CMake's KHDAYS_MOBILE_TEST) with the real window and graphics-card path on SDL's
hidden "offscreen" video driver and the CPU Vulkan driver (lavapipe), at the foldable phone's window
sizes and layouts with safe-area margins, no settings file, no save and no input, and reads the window's
own picture reports (host/app/window.c) at picture 300 (the title).

Single screen (the phone's default) FILLS the window (the owner's rule, 2026-11-21): what is lit must be
the whole window, edge to edge, under the unsafe margins too -- give or take FILL_SLACK pixels of
rounding -- and the layout line must say how the picture covers it. The two-screen layouts (stacked, side
by side) keep the old fit: lit inside the safe area.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
# name, window, layout, unsafe margins (left, top, right, bottom), least lit share at the title
CASES = [
    ("landscape, single screen (the phone's default)", (2853, 2007), 2, (60, 0, 60, 21), 30.0),
    ("portrait, single screen", (2007, 2853), 2, (0, 59, 0, 34), 15.0),
    ("landscape, single screen, the phone console's window and safe area", (2669, 1878), 2, (0, 0, 0, 95), 30.0),
    ("folded portrait, single screen", (1320, 2868), 2, (0, 59, 0, 34), 15.0),
    ("folded landscape, single screen", (2868, 1320), 2, (59, 0, 59, 21), 30.0),
    ("landscape, stacked", (2853, 2007), 0, (60, 0, 60, 21), 30.0),
    ("portrait, stacked", (2007, 2853), 0, (0, 59, 0, 34), 30.0),
    ("landscape, side by side", (2853, 2007), 1, (60, 0, 60, 21), 30.0),
]
FILL_SLACK = 2  # pixels of rounding at an edge
FILL = re.compile(rb"^picture (\d+): layout .*; fill: (\d+)x(\d+) covering the window (\d+)x(\d+) at ([\d.]+)")
LIT = re.compile(rb"^picture (\d+): (\d+)x(\d+) read back, ([\d.]+)% lit, within (\d+),(\d+) to (\d+),(\d+),")
DRAWN = re.compile(rb"^picture (\d+) \(.*; drawn as ([^;]+);")
LAVAPIPE = Path("/usr/share/vulkan/icd.d/lvp_icd.json")


def run(exe, rom, name, size, layout, margins, least):
    env = dict(os.environ, KHDAYS_ROM=str(rom), SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="offscreen",
               KHDAYS_TEST_WINDOW="1", KHDAYS_NET_SERVER="", KHDAYS_MOBILE_WINDOW=f"{size[0]}x{size[1]}",
               KHDAYS_MOBILE_LAYOUT=str(layout), KHDAYS_MOBILE_INSETS=",".join(map(str, margins)),
               VK_DRIVER_FILES=str(LAVAPIPE))
    env.pop("KHDAYS_HEADLESS", None)
    with tempfile.TemporaryDirectory(dir=Path.cwd()) as work:
        log = Path(work) / "trace"
        with log.open("wb") as out, log.open("rb") as reader:
            game = subprocess.Popen([exe], cwd=work, env=env, stdout=out, stderr=subprocess.STDOUT)
            drawn, filled = {}, {}
            try:
                deadline = time.monotonic() + 420  # (upright, covering: the 3D and 2D at 15x on the CPU card)
                while time.monotonic() < deadline:
                    line = reader.readline()
                    if not line.endswith(b"\n"):
                        if game.poll() is not None:
                            break
                        reader.seek(reader.tell() - len(line))
                        time.sleep(0.05)
                        continue
                    how = DRAWN.match(line)
                    if how:
                        drawn[int(how[1])] = how[2].decode()
                    fill = FILL.match(line)
                    if fill:
                        filled[int(fill[1])] = fill
                    lit = LIT.match(line)
                    if not lit or int(lit[1]) < 300:
                        continue
                    share = float(lit[4])
                    x0, y0, x1, y1 = (int(lit[n]) for n in range(5, 9))
                    way = drawn.get(int(lit[1]), "?")
                    if layout == 2:
                        # filled: lit from edge to edge, the one-screen picture, the console says how it covers
                        placed = (x0 <= FILL_SLACK and y0 <= FILL_SLACK and x1 >= size[0] - 1 - FILL_SLACK and
                                  y1 >= size[1] - 1 - FILL_SLACK)
                        fill = filled.get(int(lit[1]))
                        said = fill is not None and (int(fill[4]), int(fill[5])) == size
                        placed = placed and said and way == "the one-screen picture"
                        how = (f"fill {int(fill[2])}x{int(fill[3])} at {float(fill[6]):.3f}" if fill else "no fill line")
                    else:
                        placed = (x0 >= margins[0] - 4 and y0 >= margins[1] - 4 and x1 <= size[0] - margins[2] + 4 and
                                  y1 <= size[1] - margins[3] + 4)
                        how = "inside the safe area" if placed else "out of the safe area"
                    ok = share >= least and placed and (int(lit[2]), int(lit[3])) == size
                    print(f"{name}: picture {int(lit[1])} {way}, {share}% lit within {x0},{y0} to {x1},{y1} ({how}): "
                          f"{'passed' if ok else 'FAILED'}")
                    return ok
            finally:
                game.kill()
                game.wait()
        print(f"{name}: no picture 300 was reported", file=sys.stderr)
        sys.stderr.write(log.read_bytes()[-3000:].decode(errors="replace"))
        return False


def main():
    sys.stdout.reconfigure(line_buffering=True)  # (each line seen as it comes, under CTest too)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    if not LAVAPIPE.is_file():
        print("No CPU Vulkan driver (lavapipe) here: skipped")
        return 77
    exe = args.exe.resolve()
    results = [run(exe, args.rom.resolve(), *case) for case in CASES]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
