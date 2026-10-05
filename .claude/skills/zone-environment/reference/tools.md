# Environment tools

Run commands from the repo root. Blender (5.x) is **not** on PATH: it's `H:\Steam\steamapps\common\Blender\blender.exe` (`/h/Steam/steamapps/common/Blender/blender.exe` in Git Bash); `blender` below means that. UE 5.8 is at `G:\Unreal Engine\UE_5.8`.
Every step is incremental, so re-running an unchanged step costs almost nothing.

## Source data
| Tool | Command | Output |
|---|---|---|
| Kod extract | `python tools/kod_extract/extract.py` | `data/zones.json` etc. for `DEMO_RIDS` |
| Textures | `python tools/bgf2png/bgf2png.py --textures-for-zones` | `build/textures/grdNNNNN.png` (+ `_fNN` frames for animated ones), `catalog.json` |
| Blockout | `python tools/roo2gltf/roo2gltf.py [--rid 301 ...] [--preview]` | `build/zones/<rid>_<Name>.glb` + `.png` map, `data/zone_layout.json` |

Run `bgf2png` before `roo2gltf`, which needs texture sizes for its UVs.

## Textures
**`python tools/textures/make_placeholders.py`**
- **Flags:**
  - `--force`: remake everything.
  - `--esrgan-model <name>`: one upscaler for every texture.
  - `--no-esrgan`: Lanczos.
  - `--relief rules`: skip Marigold.
  - `--jobs N`
- **Output:** `build/textures_placeholder/T_<grd>_{D,N,H,E,M}.png` and `placeholders.json`.
- **When a set is remade:** when its original, catalog entry, applicable rules (`facades.json`, name rules), options or code change (`cache.json`).
- **Upscales:** cached in `build/texai/upscaled/<model>/<grd>_<tag>.png`.
- **Relief:** it then calls `ai_maps.py --apply`, which keeps Marigold's raw output in `build/texai/relief_raw/<key>/`. Refinishing (flatten windows, hole normals) doesn't rerun the model.
- **Rules worth knowing:** `ESRGAN_MODEL` / `ESRGAN_FALLBACK`, `SURFACE_RULES` (roughness and normal strength by name), `HEIGHT_RULES` (rule height mode by name: `luma`, `stones`, `timber`), `flatten_windows`, `window_mask`, `clock_layout`.

**Other texture tools**
| Tool | Command | Notes |
|---|---|---|
| AI setup | `powershell -File tools/textures/setup_ai.ps1` | Once. Builds `build/texai/` (Real-ESRGAN, a uv venv with torch/diffusers/spandrel, the GTAV model). About 5 GB plus the Marigold weights |
| Upscale | `build/texai/.venv/Scripts/python.exe tools/textures/upscale.py --model <path.safetensors> IN_DIR OUT_DIR` | Any 4x openmodeldb model (spandrel), tiled for 8 GB VRAM. Normally called by `make_placeholders.py` |
| AI maps | `tools/textures/ai_maps.py` (in `build/texai/.venv`) | `--apply` is the pipeline step. `--methods`, `--sheet`, `--input`, `--all`, `--esrgan` are for comparisons |
| Facade check | `python tools/environment/facades.py` | `build/environment/facade_check/<grd>.png` overlays and `openings_sheet.png` |

## Zone art (headless Blender)
**`blender -b --factory-startup -P tools/blender/build_zone_art.py -- --rid <rid> [flags]`**
- **Flags:**
  - `--preview [Building,…]`: quick textured renders.
  - `--strict`: fail on unbuilt openings.
  - `--plain`: no rebuilding at all.
  - `--seed-override <Building>`: write `art_src/environment/zones/<rid>/<Building>.blend` to hand-edit.
  - `--force`: re-seed.
- **Output** in `build/environment/zone_<rid>/`: `SM_Z<rid>_<Building>.glb`, `SM_Z<rid>_Grime.glb`, `manifest.json`, `openings.json`, `zone_<rid>_art.blend` and the previews.
- **Check for z-fighting:** `blender -b build/environment/zone_<rid>/zone_<rid>_art.blend --factory-startup -P tools/blender/check_overlaps.py -- [SM_Z<rid>_<Building> …]` lists overlapping same-plane, same-facing triangle pairs per mesh (`MR_OVERLAP_DETAIL=20` prints the first 20 pairs).
- **Helpers:**
  - `zone_detail.py`: cut-out tracing (`cutout_solid`), optional timber, tiles and window boxes.
  - `zone_grime.py`: base and eave strips from the geometry, configured by `materials.json` `grime`.

**Kits**
| Kit | Command | Output |
|---|---|---|
| Props | `blender -b --factory-startup -P tools/blender/build_prop_kit.py` | Lamp post and brazier into `build/environment/kit/` |
| Grass | `blender -b --factory-startup -P tools/blender/build_grass_kit.py` | Grass tufts into `build/environment/kit/` |

## Unreal
**World build: `powershell -File tools/ue/build_world.ps1 [-Script zone_mood.py] [-Clean] [-Headless]`**
- **Where it runs:** in the open editor through Python remote execution (`run_in_editor.py`). With no editor open it starts a headless editor on the Entry map. Stop PIE first.
- **What it builds:** `build_world.py` imports textures, materials (`environment_materials.py`), zone meshes (blockout as hidden collision, the rendered blockout minus rebuilt faces, the art meshes, grime as decals), props and scatter. It then builds `L_Zone_<rid>` sublevels and `L_World`.
- **Incremental:** every asset is keyed by its inputs in `game/MeridianRemastered/Saved/MRBuild/world_cache.json` (`build_cache.py`). Changed assets are rebuilt in place.
- **`-Script zone_mood.py`:** applies the mood only. `$env:MR_MOOD` picks a mood without editing `levels`.

**`environment_materials.py` masters**
- `M_Placeholder`, `M_PlaceholderMasked`, `M_PlaceholderArtFlat*` and the displaced variants.
- `M_PlaceholderClock4x3` and `M_PlaceholderArtFlatClock4x3`.
- `M_GrimeDecal`, `M_Ground`, `M_Grass`, `M_Water`, `M_PropSurface`.
- `MPC_Environment`, with the scalars `WindowGlow` and `GameHour`.
- Window glow = base × warm (1, 0.62, 0.3) × `T_<grd>_E` × `WindowGlow`.

**Look-dev captures: `powershell -File tools/ue/run_lookdev.ps1 -Label <name> [flags]`**
- **Flags:**
  - `-Compare <label>`
  - `-Only cam1,cam2`
  - `-GameHour <h>` (default 17; -1 = real time): drives the day/night cycle, the sun and moon and the clock
  - `-Mood <name>`: pin one mood (no cycle; the mood's own sun rotation)
  - `-Profile -ResX 1920 -ResY 1080`
  - `-Settle 3` (the old fixed wait)
- **Output:** images go to `build/lookdev/<label>/`. With `-Compare`, a side-by-side sheet is written as well.
- **How it captures:** each camera is saved once its image settles, with world time frozen. A GPU crash gets one retry.

**Comparisons and profiling**
- `python tools/lookdev/compare.py <labelA> <labelB> [...] [--only cams]` builds sheets for any labels.
- `powershell -File tools/lookdev/mood_test.ps1 -Moods a,b,c [-Cameras …]` captures `mood_<name>` per mood, each pinned with `-Mood` (nothing baked).
- `powershell -File tools/lookdev/cycle_test.ps1 [-Hours 0,6,9,14,19,22] [-Cameras …]` captures `cycle_<hh>` per hour and one sheet with the hours side by side.
- `powershell -File tools/lookdev/ground_normals_test.ps1 [-Variants current,strong,stones] [-Moods …] [-Cameras …]` captures `gn_<variant>_<mood>` per ground-normal variant (`ground_normals_variant.py`), then restores `materials.json`, the textures and the mood.
- `powershell -File tools/lookdev/upscaler_test.ps1 -Models default,<name>,… [-Cameras …]` captures `up_<model>` per model, then restores the default textures.
- `python tools/lookdev/profile_report.py <label> [--top N]` summarises the GPU passes from a `-Profile` run.

**Environment director (C++)**
- `UMREnvironmentSubsystem` (`Source/MeridianRemastered/Environment/`): client and PIE only (not the dedicated server; editor worlds with `mr.Env.Editor 1`).
- Reads `moods.json` at runtime; blends the cycle's keys by hour; moves the directional light along the sun or moon path (`"sky"`); switches `NightLamp` lights and `MPC_Environment.LampsOn`; sets `WindowGlow` and `Stars`.
- It re-evaluates every `mr.Env.UpdateSeconds` (5) and applies only changes. Lighting actors are found by tag (`build_world.py` tags them with their label).
- `-MRMood=` / `mr.Env.Mood` pin a mood; `MREnvReload` re-reads the file.
- Tests: `UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Meridian.Environment;Quit" -unattended -nullrhi`.

**Game time (C++)**
- `UMRGameTimeSubsystem` (`Source/MeridianRemastered/Environment/`) writes `MPC_Environment.GameHour` every tick, in game, PIE and the editor.
- The formula is the original server's: `((UTC − 5 h) mod 2 h) / 5 min`, so a game day is two real hours.
- To pin the hour: `-MRGameHour=<h>` on the command line, or the console variable `mr.GameHour <h>` (≥ 0).

## Compile
```
"G:/Unreal Engine/UE_5.8/Engine/Build/BatchFiles/Build.bat" MeridianRemasteredEditor Win64 Development -Project="E:/2026_Experiments/meridian-unreal/game/MeridianRemastered/MeridianRemastered.uproject" -WaitMutex
```
New C++ needs an editor restart before `build_world.ps1` runs inside the editor.
