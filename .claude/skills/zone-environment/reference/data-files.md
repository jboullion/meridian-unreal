# Environment data files

Everything that shapes a zone's look is text in `data/environment/` (and `data/zone_layout.json`, generated).
Each file has `_doc` strings next to its keys. They are the authority when this summary and the file disagree.

## `zone_<rid>.json`: which parts of a zone are rebuilt as art
Read by `tools/environment/blockout.py` (`zone_art_config`, `building_triangles`), `build_zone_art.py` and `build_world.py`.
Without this file, the zone renders as the plain blockout with placeholder materials.

| Key | Meaning |
|---|---|
| `rid` | zone id |
| `rebuild` | what `build_zone_art.py` rebuilds: any of `facades`, `plain_facades`, `roofs`, `parapets`, `cutouts`. **Missing = all**, so always set it. Current default: `["plain_facades", "roofs", "parapets", "cutouts"]`. `plain_facades` = only the facades.json walls with bands/piers/plinth and no painted openings. Without `parapets`, crenels become cut-out solids |
| `buildings[]` | entries, in priority order (a face belongs to the first entry that takes it) |
| `buildings[].name` | becomes `SM_Z<rid>_<name>` |
| `buildings[].region_m` | `[x0, z0, x1, z1]` in blockout glTF metres (x east, z south). Ground-level floors in the region stay in the blockout |
| `buildings[].materials` | limit the entry to these texture ids (e.g. the town wall `grd20232` over a whole-town region) |
| `buildings[].kind` | `"water"` (sunken bed + water surface; `bed_depth_m`, `bed_texture`) or `"cutouts"` (catch-all for fences/gates/signs outside buildings; put it last, `"displacement": "none"`) |
| `buildings[].displacement` | `"runtime"` (default) / `"baked"` / `"none"`; ignored while `materials.json` relief displacement is off |
| `buildings[].detail` | opt-in Blender detail: `timber`, `tiles`, `window_boxes` (`zone_detail.py`); off by default |
| `buildings[].grid_m` | displacement grid size (default 0.25; only matters with displacement on) |
| `scatter[]` | ground decoration: `meshes`, `per_m2` per floor texture, `scale`, `cull_m`, `shadows`, `seed`, `keep_out_m` |

A per-building Blender override `art_src/environment/zones/<rid>/<Building>.blend` replaces the generated building (`--seed-override <Building>` starts one).

## `facades.json`: painted features, in texture pixels
Pixels of `build/textures/<grd>.png`: x right, y down from the top-left. Sizes are in metres.

| Key | Meaning |
|---|---|
| `defaults` | `cut_openings` (false: windows/doors aren't cut), `trim` texture, reveal/frame/band/pier depths, `plinth`, `parapet_thickness_m`, `roof` (`thickness_m` 0.14, `eave_m` 0.35, `verge_m` 0.22) |
| `roofs` | sloped roof textures that get a slab and overhangs (`{}` = defaults, or per-texture overrides). Used when `rebuild` has `roofs` |
| `cutouts` | cut-out solids: `thickness_m` rules `[[name regex, metres], …]` (first match), `default_m`, `exclude` (foliage stays flat) |
| `textures.<grd>.openings[]` | `kind` `window`/`door`, `shape` (`rect`, `round`, `pointed`, `circle`), `x`/`y` pixel ranges, `spring`, `frame_px`, `sill` |
| `textures.<grd>.bands[]`, `piers[]`, `plinth`, `crenels` | trims: built with `facades` in `rebuild`, or with `plain_facades` when the texture has no `openings`. Bands and plinths wrap round outside corners (one wall of each corner) and stop where the wall continues in the same plane |
| `textures.<grd>.clock` | a time-driven animated texture: dial `shape`/`x`/`y`. The frames are packed into a 4×3 atlas and the material picks `floor(GameHour) mod 12` |

Even with facade relief off, `openings` matter:
- `window` openings give the night-glow mask `T_<grd>_E` and flattened height and normals in the glass.
- Any opening makes the texture flat under `relief.flat_openings`.

Other animated textures (`catalog.json` `frames` > 1) show frame 0.

## `materials.json`: material policy per texture
| Key | Meaning |
|---|---|
| `materials` | `grd` → real material asset override (empty today) |
| `relief` | `normals`, `displacement`, `flat_openings`, `flat` (regex on the catalog **name**, case-insensitive), `rules_for` (names that keep the rule-based maps instead of Marigold's: roofs and the four path/stone/rock floors; changing it remakes those texture sets). `true`/`false` switches both |
| `grime` | `enabled`, `exclude` (texture-name regex), `base` and `eave` blocks: `height_m`, linear `color`, `opacity`, `falloff` |
| `variants` | parameters for `<grd>__<variant>` slots: `pane`, `glass`, `panel`, `solid` |
| `displacement`, `displacement_range_cm`, `displacement_facade_scale` | per-texture Nanite displacement strengths; dormant while `relief.displacement` is false |
| `ground` | floors drawn with the world-aligned ground master (`tile_m` 2.2, optional `normal_strength`, default 0.8, and `large_normal_scale` for the 2.73x layer, default 1.5) |
| `grass` | tuft colours and wind (`wind`, `wind_speed`, `calm`, `gust_cm`) |
| `water` | `M_Water` colour, scattering, absorption, ripples |

## `moods.json`: lighting
| Key | Meaning |
|---|---|
| `levels` | level → mood baked by `zone_mood.py` (the editor's view of `L_World`). `MR_MOOD=<name>` overrides it for a run |
| `sky` | the directional light's paths in game hours: `sun_rise`/`sun_set`/`sun_max_elevation` (6/22/50: highest at 14), `moon_rise`/`moon_set`/`moon_max_elevation` (18/34/40: highest at 2), `moon_intensity` (lux), `moon_temperature` |
| `cycles.<name>.keys` | `[game hour, mood]` keys; the director blends the two around the hour (numbers and colours interpolate, the rest switches halfway) |
| `zones` | per original room id: `cycle`, `kind` (outdoor / interior / underground), `sun` (false: no sun or moon in the zone), `daylight` (share of window daylight, outside factor / 10), `base_light` / `outside` (from `zones.json`: the original's room light, which scales `SectorAmbient` by the hour), `lamps` (`always`), `ambient` (`{kind: weight}`: `motes`, `pollen`, `fireflies`, `leaves`), `weather_zone` (kod `WEATHER_ZONE_*`; 0 none) and `weather_mask` (kod `WEATHER_MASK_*` without the prefix); `default` covers the rest (Raza: 13, `DEFAULT_NS`) |
| `weather` | storm timing (`build_seconds`, `clear_seconds`), ground (`wet_seconds`, `dry_seconds`, `snow_seconds`, `melt_seconds`), wind (`wind_clear`, `wind_storm`), the overlay mood per kind in `storm` (`rain`, `snow`, `sand`, `interior`), `lightning` (`min_seconds`, `max_seconds`, `lux`, `window_flash`, `bolt_distance_m`, `bolt_height_m`), and `sounds` (the original's `.ogg` names for `rain`, `wind`, `thunder`; `follow_mask`: only where the mask has the sound bit, as the original) |
| `atmosphere` | phase 5: `night_deg` [full night below, full day above] (the sun's elevation → `MPC_Environment.Night`), `seasons` (`tint` per season, `winter_snow_cover` lying outdoors all winter), `smoke` (`base`, `morning` over `morning_hours`, `night`, `winter`, `storm` takes away), `ambient.<kind>` (`seasons` [spring..winter], `when`: day / night / always, `storm`: share a full storm takes away, negative adds) |
| overlay moods (`storm_*`) | name only what a storm changes: numbers and arrays replace, `{"mul": x}` multiplies, `{"add": x}` adds; the base mood must have the field so it blends back; `CloudMaterial` sets the clouds' material parameters |
| `moods.<name>.inherit` | start from another mood; set only what differs |
| `moods.<name>.<ActorLabel>` | `Sun` (`rotation` [pitch, yaw], intensity in lux, temperature), `SkyLight`, `HeightFog`, `GlobalPostProcess.settings` (PostProcessSettings fields; exposure in EV100) |
| `moods.<name>.Collection` | `MPC_Environment` values: `WindowGlow` (0 by day, 0.12 dusk, 0.35 night), `Stars` (0 by day, 0.2 dusk, 1 night), `WindowDaylight` (inside: 1 by day), `SectorAmbient` (inside: 1, scaled by the room light) and the vector `AmbientTint` ([r, g, b]). `GameHour` and `LampsOn` are written by C++, not by moods |

Moods: `raza_afternoon` (the level's baked mood, and "day" in the cycle), `raza_morning` (with the morning mist), `raza_dusk`, `raza_night` (with a faint sky-light fill). Cycle `raza_outdoor`: night until 4:30, morning by 6:30, day 9:30–17, dusk by 19:30, night from 21:30. A mood's `Sun.rotation` only counts when it's pinned; in the cycle the sun path decides.

`materials.json` `"precip"`: the rain and snow look (fall speeds, drift, streak and flake sizes, opacity), read by `M_Precip`'s instances; `splash` (`Period`, `Area`, `Density`, `Size`, `Opacity`) for `M_Splash`, `bolt` (`Brightness`) for `M_Bolt`.

`materials.json` `"seasons"`: `foliage`, a catalog-name regex for textures that get the season's tint (Seasonal masters; `M_Ground` and `M_Grass` always do). `"atmosphere"`: the looks of `M_Ambient` (`ambient`: per-kind share of the quads, sizes, colours, the fireflies' glow), `M_Smoke` (`smoke`: `Life`, `Rise`, `Bend`, `Size0`/`Size1`, `Opacity`, `Color`) and `M_Moth` (`moths`: `Share`, `Radius`, `Size`, `Glow`).

## `props.json`: Kod-placed objects
- `classes.<KodClass>`: `mesh` (`build/environment/kit/<mesh>.glb` from `build_prop_kit.py`), an optional `light` (`offset_m`, `candela`, `radius_m`, `temperature`, `source_radius_cm`, `night_only`: off 11–17; or `kod_intensity` / `kod_color`, the Kod light; `shadows`; `flicker`: `true`, or `"sector"` only in the original's flickering sectors) and an optional `fire` (`preset`, `base_m`: the flame's bottom above the object).
- `fires.<preset>`: a flame flipbook from the original frames. `texture` (a wall texture's frames) or `bgf` + `frames` (an object's, `tools/bgf2png/bgf2png.py <name>` first); `crop` [x0, y0, x1, y1] in the original's pixels; `keep` (`flame`: only flame-coloured pixels, or `all`); `fps`; `brightness`. `walls.<grd>`: wall textures with this flame painted on (`at` the flame centre px, `out` the texture direction away from the wall, `erase` the rect whose flame pixels are removed) and `light` for each torch found.
- `smoke.textures`: chimney wall textures; every chimney the blockout draws with one gets a smoke plume (`tools/environment/chimneys.py`). A class's `moths` (`offset_m`): moths circle its light at night.
- `materials`: the kit's slots (linear colours, emissive; `night_only`: the emissive follows the lamps).
- `build_world.py` spawns one per object in `data/zone_layout.json`.

## `lookdev_cameras.json`
`cameras[]`: `name`, `location_cm` (world UE cm, X east, Y south, Z up), `rotation` [pitch, yaw] (yaw 0 = east, 90 = south), `fov`.

Interiors sit at their zone's `world_origin_cm` (`data/zone_layout.json`). `MRBookmark <name>` in game logs a ready-made entry.

## Generated files you read but don't edit
| File | What |
|---|---|
| `data/zone_layout.json` | per zone: `world_origin_cm`, objects, exits, `roo_security` (the .roo's security value, checked against the server's room online) (from `roo2gltf`) |
| `build/textures/catalog.json` | per texture: `name`, `w`, `h`, `shrink`, `frames`, `groups`, `has_transparency` |
| `build/textures_placeholder/placeholders.json` | per texture: maps, `masked`, roughness, normal strength, `atlas` |
| `build/environment/zone_<rid>/manifest.json`, `openings.json` | what `build_world.py` imports; every painted opening built or not |
