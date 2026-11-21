"""Draw the phone's window on Linux and check its picture.

Runs the test build (CMake's KHDAYS_MOBILE_TEST) with the real window and graphics-card path on SDL's
hidden "offscreen" video driver and the CPU Vulkan driver (lavapipe), at the foldable phone's window
sizes and layouts with safe-area margins, no settings file, no save and no input, and reads the window's
own picture reports (host/app/window.c): by picture 300 the title must be lit inside the safe area.
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
    ("landscape, stacked", (2853, 2007), 0, (60, 0, 60, 21), 30.0),
    ("portrait, stacked", (2007, 2853), 0, (0, 59, 0, 34), 30.0),
    ("landscape, side by side", (2853, 2007), 1, (60, 0, 60, 21), 30.0),
]
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
            drawn = {}
            try:
                deadline = time.monotonic() + 150
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
                    lit = LIT.match(line)
                    if not lit or int(lit[1]) < 300:
                        continue
                    share = float(lit[4])
                    x0, y0, x1, y1 = (int(lit[n]) for n in range(5, 9))
                    inside = (x0 >= margins[0] - 4 and y0 >= margins[1] - 4 and x1 <= size[0] - margins[2] + 4 and
                              y1 <= size[1] - margins[3] + 4)
                    way = drawn.get(int(lit[1]), "?")
                    ok = share >= least and inside and (int(lit[2]), int(lit[3])) == size
                    print(f"{name}: picture {int(lit[1])} {way}, {share}% lit within {x0},{y0} to {x1},{y1}: "
                          f"{'passed' if ok else 'FAILED'}")
                    return ok
            finally:
                game.kill()
                game.wait()
        print(f"{name}: no picture 300 was reported", file=sys.stderr)
        sys.stderr.write(log.read_bytes()[-3000:].decode(errors="replace"))
        return False


def main():
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
