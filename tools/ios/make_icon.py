#!/usr/bin/env python3
"""Turn any image (for example the KH Melon Mix logo) into a private app-icon asset catalog for the iOS build.

    python3 tools/ios/make_icon.py path/to/logo.png ../my-icon/Assets.xcassets
    ICON_CATALOG=$PWD/../my-icon/Assets.xcassets tools/ios/mac/build.sh iphoneos

The image is trimmed to its visible part and centred on black at 1024x1024 (iOS icons are square and opaque).
Needs Pillow (pip install pillow)."""
import json, sys
from pathlib import Path
from PIL import Image

src, out = Path(sys.argv[1]), Path(sys.argv[2])
img = Image.open(src).convert('RGBA')
box = img.convert('RGB').point(lambda v: 255 if v > 12 else 0).convert('L').getbbox() or (0, 0, img.width, img.height)
logo = img.crop(box); logo.thumbnail((944, 944), Image.Resampling.LANCZOS)
icon = Image.new('RGB', (1024, 1024)); icon.paste(logo, ((1024 - logo.width) // 2, (1024 - logo.height) // 2), logo)
cat = out / 'AppIcon.appiconset'; cat.mkdir(parents=True, exist_ok=True)
icon.save(cat / 'AppIcon-1024.png')
(cat / 'Contents.json').write_text(json.dumps({'images': [{'filename': 'AppIcon-1024.png', 'idiom': 'universal', 'platform': 'ios',
                                    'size': '1024x1024'}], 'info': {'author': 'xcode', 'version': 1}}, indent=2))
(out / 'Contents.json').write_text(json.dumps({'info': {'author': 'xcode', 'version': 1}}, indent=2))
print(f'icon catalog written to {out}; build with ICON_CATALOG={out.resolve()}')
