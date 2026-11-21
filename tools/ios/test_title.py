"""Run the game headlessly and wait for the visible root title menu."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
TITLE = re.compile(rb"^situation: frame (\d+), title 0x[\da-f]+ -> 0x([\da-f]+)$")


def title_visible(lines):
    for line in lines:
        match = TITLE.fullmatch(line.rstrip())
        if match:
            state = int(match[2], 16)
            # situation_take.c packs shown, valid and page at bits 44, 40 and 32.
            if (state >> 44) & 15 == 2 and (state >> 40) & 1 and (state >> 32) & 255 == 0:
                print(f"Title menu shown at frame {int(match[1])}")
                return True
    return False


def report_tail(log):
    with log.open("rb") as trace:
        trace.seek(max(0, log.stat().st_size - 4000))
        print(trace.read().decode(errors="replace"), file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    check = subprocess.run([sys.executable, ROOT / "tools/verify_rom.py", args.rom])
    if check.returncode:
        return check.returncode
    exe = args.exe.resolve()
    if not exe.is_file():
        sys.exit(f"Missing native executable: {exe}")
    env = dict(os.environ, KHDAYS_ROM=str(args.rom.resolve()), SDL_AUDIODRIVER="dummy",
               KHDAYS_HEADLESS="1", KHDAYS_NET_SERVER="", KHDAYS_TRACE_SITUATION="1")
    with tempfile.TemporaryDirectory(dir=Path.cwd()) as work:
        log = Path(work) / "trace"
        with log.open("wb") as out, log.open("rb") as reader:
            game = subprocess.Popen([exe], cwd=work, env=env, stdout=out, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 90
                while time.monotonic() < deadline:
                    offset = reader.tell()
                    line = reader.readline()
                    if line.endswith(b"\n"):
                        if title_visible([line]):
                            return 0
                        continue
                    if game.poll() is not None:
                        report_tail(log)
                        sys.exit(f"Game exited with status {game.returncode} before the title")
                    reader.seek(offset)
                    time.sleep(.1)
                report_tail(log)
                sys.exit("The game never reported a visible root title menu")
            finally:
                if game.poll() is None:
                    game.kill()
                game.wait()


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--check-log":
        with open(sys.argv[2], "rb") as trace:
            sys.exit(0 if title_visible(trace) else 1)
    sys.exit(main())
