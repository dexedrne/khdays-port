"""Precompile the port's embedded Metal programs for the selected Apple SDK."""
import argparse
from pathlib import Path
import subprocess
import zlib

from fix_msl_order import GENERATED, array


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", choices=("iphoneos", "iphonesimulator"))
    parser.add_argument("work", type=Path)
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    destination = args.bundle / "Shaders"
    destination.mkdir(exist_ok=True)
    for header in sorted(GENERATED.glob("*.h")):
        _, source = array(header.read_text(), header.stem + "_msl", header.name)
        assert source.endswith(b"\0")
        source = source[:-1]
        metal = args.work / (header.stem + ".metal")
        air = metal.with_suffix(".air")
        library = destination / f"{zlib.crc32(source):08x}.metallib"
        metal.write_bytes(source)
        xcrun = ["xcrun", "--sdk", args.sdk]
        minimum = "-mios-simulator-version-min=16.0" if args.sdk == "iphonesimulator" else "-mios-version-min=16.0"
        subprocess.run([*xcrun, "metal", minimum, "-c", metal, "-o", air], check=True)
        subprocess.run([*xcrun, "metallib", air, "-o", library], check=True)


if __name__ == "__main__":
    main()
