"""
Install MakeHuman asset packs (zip) into MPFB, headless:

    blender -b -P tools/blender/install_mpfb_packs.py -- UnrealAssets/makehuman/*.zip

Uses MPFB's own "Load pack from zip file" operator, so packs end up exactly where the MPFB UI
would put them (the extension's user data folder). Packs we use (all CC0, from
https://static.makehumancommunity.org/assets/assetpacks/index.html):
makehuman_system_assets, eyebrows01, eyelashes01, hair01, skins01, skins02, cheek01, nose01,
ears01, shirts01, pants01, shoes01.
"""
import glob
import os
import sys

import addon_utils
import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
zips = []
for pattern in argv:
    zips.extend(sorted(glob.glob(pattern)) or [pattern])

pkg = None
for mod in addon_utils.modules():
    if mod.__name__.endswith(".mpfb"):
        addon_utils.enable(mod.__name__, default_set=True)
        pkg = mod.__name__
if not pkg:
    print("[install_mpfb_packs] MPFB extension not found")
    sys.exit(1)

failed = 0
for z in zips:
    z = os.path.abspath(z)
    if not os.path.exists(z):
        print("[install_mpfb_packs] missing: " + z)
        failed += 1
        continue
    result = bpy.ops.mpfb.load_pack(filepath=z)
    print("[install_mpfb_packs] %s -> %s" % (os.path.basename(z), result))

location = sys.modules[pkg + ".services.locationservice"].LocationService
print("[install_mpfb_packs] user data: " + location.get_user_data())
sys.exit(1 if failed else 0)
