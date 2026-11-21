"""Renumber the committed Metal shaders' buffers to SDL's order (port/tools/build_shaders.py,
sdl_metal_order) without the Vulkan SDK: rewrites only the <name>_cs_msl arrays of
port/host/render/generated/*_cs.h. Running it again changes nothing.
    python3 tools/ios/fix_msl_order.py [--check]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port" / "tools"))
from build_shaders import c_array, sdl_metal_order  # noqa: E402


def kernel(msl: bytes) -> str:
    return next(line for line in msl.decode("utf-8").split("\n") if line.startswith("kernel void "))


def main() -> int:
    check = "--check" in sys.argv[1:]
    wrong = 0
    for header in sorted((ROOT / "port" / "host" / "render" / "generated").glob("*_cs.h")):
        name = header.stem + "_msl"
        text = header.read_text(encoding="utf-8")
        found = re.search(
            rf"static const unsigned char {name}\[\] = \{{(.*?)\}};\nstatic const unsigned int {name}_size = \d+u;\n",
            text, re.S)
        if not found:
            sys.exit(f"{header.name}: no {name} array")
        msl = bytes(int(b, 16) for b in re.findall(r"0x[0-9a-f]{2}", found[1]))
        line = kernel(msl)
        uniform = re.search(r"constant \w+& \w+ \[\[buffer\((\d+)\)\]\]", line)
        if uniform and int(uniform[1]) == 0:
            print(f"{header.name}: already in SDL's order ({line.count('[[buffer(')} buffers)")
            continue
        wrong += 1
        if check:
            print(f"{header.name}: the uniform block is at buffer {uniform[1] if uniform else '?'}, not 0")
            continue
        fixed = sdl_metal_order(msl[:-1]) + b"\0"
        header.write_text(text[: found.start()] + c_array(name, fixed) + text[found.end():], encoding="utf-8",
                          newline="\n")
        print(f"{header.name}: renumbered ({len(msl)} -> {len(fixed)} bytes)")
    return 1 if check and wrong else 0


if __name__ == "__main__":
    sys.exit(main())
