"""
Which data/environment/props.json entry draws a Kod object: the world build's rule
(tools/ue/build_world.py prop_config calls it),
shared with tools/aigen/inventory.py so the prop survey knows what already has a mesh. Pure Python.
"""
import json
import os

PROPS = os.path.join(os.path.dirname(__file__), "..", "..", "data", "environment", "props.json")


def load(path: str = PROPS) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def prop_entry(props: dict, obj: dict):
    """-> (key, entry) of the props.json entry for a Kod object ({"class", "params"}, as in zone_layout.json), or
    (None, None): OrnamentalObjects by their type ("types", keyed by the OO number as a string; key "OO<type>"),
    or one made with only its own icon by "icons" (the bgf, lower case: the memorial trees); every other class
    by "classes": "<Class>/<classtype>" first for an object made with a classtype (a FoodDispenser of Apple: the
    apple tree), then "<Class>/<param>=<value>" for a constructor argument (a Tree's bottom: 1 oak, 2 elm,
    3 pine), then "<Class>"."""
    params = obj.get("params") or {}
    if obj["class"] == "OrnamentalObject":
        if params.get("type") is None and isinstance(params.get("rIcon"), str):
            key = params["rIcon"].lower()
            return key, props.get("icons", {}).get(key)
        return "OO%s" % params.get("type"), props.get("types", {}).get(str(params.get("type")))
    classes = props.get("classes", {})
    classtype = params.get("classtype")
    if isinstance(classtype, dict) and classtype.get("class"):
        key = "%s/%s" % (obj["class"], classtype["class"])
        if classes.get(key):
            return key, classes[key]
    for name, value in sorted(params.items()):
        if isinstance(value, (int, str)) and not isinstance(value, bool):
            key = "%s/%s=%s" % (obj["class"], name, value)
            if classes.get(key):
                return key, classes[key]
    return obj["class"], classes.get(obj["class"])


def prop_config(props: dict, obj: dict):
    """The props.json entry for a Kod object (prop_entry), or None."""
    return prop_entry(props, obj)[1]


def entry_meshes(cfg: dict):
    """Every mesh an entry may place: its mesh, custom mesh and options."""
    meshes = [cfg.get("mesh"), cfg.get("mesh_custom")]
    for option, names in (cfg.get("mesh_options") or {}).items():
        if not option.startswith("_"):
            meshes += names
    return [m for m in meshes if m]


def mesh_kept(props: dict, key, cfg: dict) -> bool:
    """Does the world build place this entry's mesh? props.json "mesh_keep": with "enabled", only the entries it
    keeps (by key) and, with "trees", the procedural trees (SM_Tree_* meshes); else every mesh."""
    rule = props.get("mesh_keep") or {}
    if not rule.get("enabled"):
        return True
    if key in rule.get("keep", []):
        return True
    meshes = entry_meshes(cfg or {})
    return bool(rule.get("trees") and meshes and all(m.startswith("SM_Tree_") for m in meshes))
