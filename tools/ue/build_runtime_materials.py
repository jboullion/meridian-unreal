"""
The runtime rooms' material and its collection values only (M_RuntimeRoom, MPC_Environment;
docs/adr/0012 M7), without a whole world build:

    powershell -File tools/ue/build_world.ps1 -Script build_runtime_materials.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_cache  # noqa: E402
from environment_materials import build_runtime_room_material, ensure_mpc  # noqa: E402

build_cache.begin()
ensure_mpc()
build_runtime_room_material()
build_cache.CACHE.save()
print("build_runtime_materials: done")
