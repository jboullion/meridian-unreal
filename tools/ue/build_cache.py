"""
Input hashes for incremental world builds (tools/ue/build_world.py). Runs inside the Unreal Editor.

Every generated asset is stored with a key: a hash of everything that went into it (source file
contents, settings from data/*.json, the source code of the function that builds it, and the keys of
the assets it was built from). A step whose key matches what was saved last time, and whose asset
still exists, is skipped; anything else is rebuilt in place (same asset, so whatever references it
stays valid). Nothing is deleted between runs.

The keys live in game/MeridianRemastered/Saved/MRBuild/world_cache.json. Deleting that file (or
build_world.ps1 -Clean) rebuilds everything.
"""
import hashlib
import inspect
import json
import linecache
import os
import time

import unreal

eal = unreal.EditorAssetLibrary
CACHE_FILE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()),
                          "MRBuild", "world_cache.json")

# a long-lived editor (remote execution) re-imports our modules each run; make inspect.getsource()
# read the current files rather than the lines it cached last time
linecache.checkcache()

_digests = {}


def file_digest(path):
    """sha1 of a file's contents (memoised per run by size + mtime)."""
    st = os.stat(path)
    memo = (path, st.st_size, st.st_mtime_ns)
    if memo not in _digests:
        h = hashlib.sha1()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        _digests[memo] = h.hexdigest()
    return _digests[memo]


def source(*functions):
    """The source code of these functions, so editing a builder rebuilds what it made."""
    return "\n".join(inspect.getsource(f) for f in functions)


class BuildCache:
    def __init__(self, clean=False):
        self.entries = {}
        if not clean and os.path.exists(CACHE_FILE):
            try:
                self.entries = json.load(open(CACHE_FILE, encoding="utf-8"))
            except ValueError:
                unreal.log_warning("[build_cache] unreadable %s; rebuilding everything" % CACHE_FILE)
        self.keys = {}  # asset path -> key, for this run (dependents hash their inputs' keys)
        self.built, self.reused = {}, {}
        self._last_save = time.time()

    def key(self, *parts, deps=True):
        """Hash of JSON-able parts. With `deps`, a string that is the path of an asset built this run
        brings that asset's key with it, so rebuilding an input rebuilds what depends on it."""
        def inputs(v):
            if isinstance(v, str):
                return [self.keys[v]] if v in self.keys else []
            if isinstance(v, dict):
                return [d for x in v.values() for d in inputs(x)]
            if isinstance(v, (list, tuple)):
                return [d for x in v for d in inputs(x)]
            return []
        blob = json.dumps([parts, inputs(parts) if deps else []], sort_keys=True, default=str)
        return hashlib.sha1(blob.encode("utf-8")).hexdigest()

    def fresh(self, path, key, exists=None):
        """True if `path` was built from `key` and is still there (`exists` overrides the check)."""
        entry = self.entries.get(path)
        if not entry or entry.get("key") != key:
            return False
        return exists() if exists else eal.does_asset_exist(path)

    def info(self, path):
        return self.entries.get(path, {})

    def done(self, path, key, kind, built, **extra):
        """Record `path` as built (or reused) from `key`, with optional extra data for next time."""
        self.keys[path] = key
        entry = dict(self.entries.get(path, {})) if not built else {}
        entry.update(extra, key=key)
        self.entries[path] = entry
        (self.built if built else self.reused).setdefault(kind, []).append(path)
        if built and time.time() - self._last_save > 30:
            self.save()  # an interrupted run keeps what it finished

    def get_or_build(self, path, key, kind, build):
        """-> path. Calls build() (which must save the asset at `path`) unless it is fresh."""
        if self.fresh(path, key):
            self.done(path, key, kind, False)
        else:
            build()
            self.done(path, key, kind, True)
        return path

    def forget(self, path):
        self.entries.pop(path, None)

    def save(self):
        os.makedirs(os.path.dirname(CACHE_FILE), exist_ok=True)
        tmp = CACHE_FILE + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.entries, f, indent=0, sort_keys=True)
        os.replace(tmp, CACHE_FILE)
        self._last_save = time.time()

    def summary(self):
        kinds = sorted(set(self.built) | set(self.reused))
        return ", ".join("%s %d/%d" % (k, len(self.built.get(k, [])), len(self.built.get(k, [])) + len(self.reused.get(k, [])))
                         for k in kinds)


CACHE = None  # set by build_world.py for the current run


def begin(clean=False):
    global CACHE
    CACHE = BuildCache(clean)
    return CACHE
