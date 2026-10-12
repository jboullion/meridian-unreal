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


def prop_config(props: dict, obj: dict):
    """The props.json entry for a Kod object ({"class", "params"}, as in zone_layout.json): OrnamentalObjects
    by their type ("types", keyed by the OO number as a string), or one made with only its own icon by
    "icons" (the bgf, lower case: the memorial trees); every other class by "classes": "<Class>/<classtype>"
    first for an object made with a classtype (a FoodDispenser of Apple: the apple tree), then
    "<Class>/<param>=<value>" for a constructor argument (a Tree's bottom: 1 oak, 2 elm, 3 pine), then
    "<Class>"."""
    params = obj.get("params") or {}
    if obj["class"] == "OrnamentalObject":
        if params.get("type") is None and isinstance(params.get("rIcon"), str):
            return props.get("icons", {}).get(params["rIcon"].lower())
        return props.get("types", {}).get(str(params.get("type")))
    classes = props.get("classes", {})
    classtype = params.get("classtype")
    if isinstance(classtype, dict) and classtype.get("class"):
        cfg = classes.get("%s/%s" % (obj["class"], classtype["class"]))
        if cfg:
            return cfg
    for key, value in sorted(params.items()):
        if isinstance(value, (int, str)) and not isinstance(value, bool):
            cfg = classes.get("%s/%s=%s" % (obj["class"], key, value))
            if cfg:
                return cfg
    return classes.get(obj["class"])
