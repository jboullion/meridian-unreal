"""
Write data/environment/materials.json for one ground-normals variant (tools/lookdev/ground_normals_test.ps1),
starting from a saved copy of the real file:

    python tools/lookdev/ground_normals_variant.py <variant> <original materials.json copy>

  current   unchanged: Marigold normals, M_Ground's default strength (0.8)
  strong    Marigold normals on every "ground" floor at normal_strength STRONG
  stones    the path, stone and rock floors switch to make_placeholders.py's rule-based "stones" maps
            (relief rules_for) at normal_strength STONES; grass keeps Marigold's
"""
import json
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MATERIALS = os.path.join(REPO, "data", "environment", "materials.json")
CATALOG = os.path.join(REPO, "build", "textures", "catalog.json")
STRONG = 2.0
STONES = 1.0
STONE_FLOORS = r"path|stone|rock|cobble"  # floor names (catalog) that get the rule-based maps


def main():
    variant, original = sys.argv[1], sys.argv[2]
    data = json.load(open(original, encoding="utf-8"))
    names = {k: v["name"] for k, v in json.load(open(CATALOG, encoding="utf-8"))["textures"].items()}
    floors = [k for k in data["ground"] if not k.startswith("_")]
    if variant == "strong":
        for k in floors:
            data["ground"][k]["normal_strength"] = STRONG
    elif variant == "stones":
        stony = [k for k in floors if re.search(STONE_FLOORS, names.get(k, ""), re.I)]
        exact = "|".join(re.escape(names[k]) for k in stony)
        relief = data["relief"]
        relief["rules_for"] = "|".join(x for x in (relief.get("rules_for"), exact) if x)
        for k in stony:
            data["ground"][k]["normal_strength"] = STONES
        print("stones: rule-based maps for %s" % ", ".join("%s (%s)" % (k, names[k]) for k in stony))
    elif variant != "current":
        raise SystemExit("unknown variant %s" % variant)
    with open(MATERIALS, "w", encoding="utf-8", newline="") as f:
        f.write(json.dumps(data, indent=2, ensure_ascii=False) + "\n")


main()
