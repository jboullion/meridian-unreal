"""
Texture files to import under names Unreal won't take for UDIM tiles. Runs inside the Unreal Editor.

Unreal reads a file name ending in four digits (shelter_1500.png, T_Map_1500.png) as one tile of a
UDIM set and gathers every sibling with the same stem into one virtual texture: the rooms numbered
1000 and up (the pass-3 maps) came out as one texture of many blocks, which the cook refuses
("VT has blocks with mismatched aspect ratios"). Such a file is imported from a copy whose name
doesn't end in digits; set the task's destination_name, as the asset is still named after the file.
"""
import os
import re
import shutil
import tempfile

import unreal

COPY_DIR = os.path.join(tempfile.gettempdir(), "mr_import")


def safe(path):
    """path, or a copy of it named so Unreal doesn't take it for a UDIM tile."""
    stem, ext = os.path.splitext(os.path.basename(path))
    if not re.search(r"\d{4}$", stem):
        return path
    os.makedirs(COPY_DIR, exist_ok=True)
    copy = os.path.join(COPY_DIR, stem + "_src" + ext)
    shutil.copyfile(path, copy)
    return copy


def task(path, destination_path, destination_name):
    """An automated, replacing import task for path, from a UDIM-safe name."""
    t = unreal.AssetImportTask()
    t.filename = safe(path)
    t.destination_path = destination_path
    t.destination_name = destination_name
    t.automated = True
    t.replace_existing = True
    t.save = False
    return t
