# Objects with no prop

What the remastered maps place that has no 3D prop yet (2026-10-13, every built zone: 347 maps). A placed object needs a prop in `data/environment/props.json` (by class, by ornamental type or by icon) to be drawn offline; online, an object without one is drawn from its own bitmap (`UMRBgfSpriteComponent`), so nothing is invisible there. The props come from the sprite-to-3d pipeline (skill `.claude/skills/sprite-to-3d/`, ADR 0007).

Regenerate the raw list with `python tools/world/overnight.py --report` (`build/overnight/report.md`, "Objects with no prop"; it covers the batch passes) or `overnight.missing_props(...)` over every zone.

## Props to make (133 objects, 15 kinds)

| Object | Sprite | Count | Where |
|---|---|---|---|
| Smoke column (`SmokeColumn`, a `Flickerer`) | `smoke.bgf` | 31 | Cor Noth (7), Berg (5), Jasper, Jixa, the Valley of Death and Kardde's Pass, the jungle, two Barloque vaults. Probably an effect (rising smoke), not a mesh: like the chimneys' smoke (`tools/environment/chimneys.py`) |
| Pile of bones (`OrnamentalObject` 82, `OO_BONES`) | `bonepile.bgf` | 20 | Obfal's Sepulchre, in Ugol's Warren (all 20) |
| Smelting forge (`Forge`) | `forge.bgf` | 14 | The guild halls and Green's workshop, the Smoldering Forge |
| Ko'catan smelting forge (`KocatanForge`) | `forge.bgf` | 12 | The guild halls (the same halls as the forge; one prop can serve both) |
| Decorative weapons and armour (`DecoItem`) | per item | 14 | The Temple of Kraanan (11: scimitar, axe, swords, hammer...), the Wetland Fortress guild hall (3: knight's shield, longsword, plate armour). Each names an item, so the item's own mesh or icon could serve |
| Mana node (`ManaNode`) | `node.bgf` | 9 | One each in caves and castles: Castle Victoria, the Badlands, Seafarer's Peak, the Caves of Ice, the Maze of the Mimic, and others |
| Portal (`Portal`) | `portal.bgf` | 9 | The Underworld (6), the Underground Lake, Martyr's Battleground, Ukgoth, the Holy Land of Trolls |
| Brazier switch (`BrazierSwitch`) | `brazier.bgf` | 5 | The Underworld |
| Target globe and view-point globe (`TargetGlobe`, `ViewPointGlobe`) | `necglobe.bgf` | 5 + 5 | The Dungeon of Castle Brax (4 each), Jixa |
| Mushroom (`Mushroom`) | `mushroom.bgf` | 4 | The Forests of Meridian |
| Rip in space (`HellPortal`) | `teleport.bgf` | 2 | The Underworld, Meridian University |
| Gate to the Death Realm (`DeathRealmEntrance`) | `teleport.bgf` | 1 | The Laboratory of the Mad Brother |
| Miniature portal (`OrnamentalObject` 166, `OO_MUSEUM_TELEPORT`) | `modteleport.bgf` | 1 | The Grand Museum of Raza |
| Fountain jet (`FountainJet`) | `fountjet.bgf` | 1 | The Duke's Feast Hall (water spraying: maybe an effect) |

## Characters, not props (19 objects, 16 kinds)

NPCs the list catches because `tools/aigen/inventory.py` doesn't class them as NPCs. Characters are drawn as sprites (docs/sprites.md, ADR 0008), not modelled; check each has a converted sprite.

Captain Seaborn (`captain.bgf`; the Ports of Barloque, the plantation, Solitary Shoreline, Ko'catan), Fortuna, Zimmucas, the priestesses of Jala, Faren, Kraanan, Shal'ille and Qor, the Monk of Riija, the Quartermaster, and the six in the gods' meeting place (Parrin Aragone, Miriana, Maleval, Tendrath, Izzio, the Keeper of Souls).

## Nothing to make (33)

- **32 markers with no class**: object positions saved in the `.roo` by the map editor where no Kod object is created (the Arena of Kraanan 6, the Smuggler's Crossing 2, and one or two in about 20 other rooms). The server puts nothing there.
- **One `OO_NOTHING`** (ornamental type 0) in Marion: an ornament with no picture.
