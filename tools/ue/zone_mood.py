"""
Apply a lighting mood from data/environment/moods.json to the lighting actors in L_World
(docs/adr/0003 pass 4). Runs inside the Unreal Editor.

build_world.py calls apply_level_mood() after spawning the lights. To iterate on a mood without
rebuilding the world, run this file on its own; it loads L_World, applies the mood and saves:

    UnrealEditor-Cmd.exe <project>.uproject -ExecutePythonScript="<repo>/tools/ue/zone_mood.py" -unattended -nosplash -RenderOffscreen

then re-render the look-dev cameras (tools/ue/run_lookdev.ps1). Set MR_MOOD=<name> to try another mood
from moods.json without changing its "levels" entry. A mood only sets what it lists; values from an
earlier mood stay until build_world.py rebuilds L_World.
"""
import json
import os

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MOODS = os.path.join(REPO, "data", "environment", "moods.json")
WORLD_PATH = "/Game/Generated/Maps/L_World"


def log(msg):
    unreal.log("[zone_mood] " + msg)


def _convert(current, value):
    """JSON value -> the type of the property's current value."""
    if isinstance(current, unreal.LinearColor):
        return unreal.LinearColor(*value)
    if isinstance(current, unreal.Color):
        return unreal.Color(*value)
    if isinstance(current, unreal.Vector4):
        return unreal.Vector4(*value)
    if isinstance(current, unreal.Vector):
        return unreal.Vector(*value)
    if isinstance(current, unreal.EnumBase):
        return getattr(type(current), value.upper())
    if isinstance(current, bool):
        return bool(value)
    if isinstance(current, int):
        return int(value)
    if isinstance(current, float):
        return float(value)
    return value


def _set(obj, name, value):
    current = obj.get_editor_property(name)
    obj.set_editor_property(name, _convert(current, value))


def _apply_post_process(actor, settings):
    pp = actor.get_editor_property("settings")
    for name, value in settings.items():
        _set(pp, name, value)
        override = "override_" + name
        try:
            pp.set_editor_property(override, True)
        except Exception:
            log("WARNING: %s has no %s flag" % (name, override))
    actor.set_editor_property("settings", pp)


def resolve_mood(moods, name):
    """A mood with "inherit": <other> starts from that mood; its own actor blocks and settings win."""
    mood = moods[name]
    base = mood.get("inherit")
    if not base:
        return mood
    merged = json.loads(json.dumps(resolve_mood(moods, base)))
    for label, block in mood.items():
        if label == "inherit" or not isinstance(block, dict):
            continue
        target = merged.setdefault(label, {})
        for key, value in block.items():
            if key == "settings":
                target.setdefault("settings", {}).update(value)
            else:
                target[key] = value
    return merged


def apply_mood(world_actors, mood_name):
    """Apply one mood to the actors (by label) of the current level. Returns the number of actors changed."""
    mood = resolve_mood(json.load(open(MOODS, encoding="utf-8"))["moods"], mood_name)
    by_label = {a.get_actor_label(): a for a in world_actors}
    changed = 0
    for label, block in mood.items():
        if label.startswith("_"):
            continue
        actor = by_label.get(label)
        if not actor:
            log("WARNING: mood %s names %s, which is not in the level" % (mood_name, label))
            continue
        if label == "GlobalPostProcess" or "settings" in block:
            _apply_post_process(actor, block.get("settings", {}))
        else:
            comp = actor.root_component  # the light / fog component for these actor types
            for name, value in block.items():
                if name == "rotation":
                    actor.set_actor_rotation(unreal.Rotator(roll=0, pitch=value[0], yaw=value[1]), False)
                elif not name.startswith("_"):
                    _set(comp, name, value)
        changed += 1
    log("applied mood %s to %d actors" % (mood_name, changed))
    return changed


def level_mood(level_name):
    """The mood for a level; the MR_MOOD environment variable overrides it (for look-dev variants)."""
    return os.environ.get("MR_MOOD") or json.load(open(MOODS, encoding="utf-8")).get("levels", {}).get(level_name)


def apply_level_mood(level_name="L_World"):
    """Apply the mood moods.json assigns to this level to the currently open level (unsaved)."""
    name = level_mood(level_name)
    if not name:
        log("no mood for %s" % level_name)
        return 0
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    return apply_mood(actors, name)


def main():
    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_sub.load_level(WORLD_PATH):
        raise RuntimeError("could not load " + WORLD_PATH)
    level_sub.set_current_level_by_name("PersistentLevel")
    if apply_level_mood("L_World") and not level_sub.save_current_level():
        raise RuntimeError("could not save " + WORLD_PATH)


if __name__ == "__main__":
    try:
        main()
    finally:
        if "-keep-open" not in os.environ.get("MR_BUILD_WORLD_ARGS", ""):
            unreal.SystemLibrary.quit_editor()
