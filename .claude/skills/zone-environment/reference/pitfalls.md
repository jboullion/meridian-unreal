# Known pitfalls

Each entry gives the symptom you'll see, then the cause and fix. The ADR 0003 section named in brackets has the full story.

## Geometry
- **A window or door painted across a wall seam wasn't rebuilt.** The original map split the wall into pieces.
  `merge_wall_runs` joins them, mirrored (`WF_BACKWARDS`) seams included. Check `openings.json`, and run with `--strict`. ["Openings left flat"]
- **A fence has solid boxes between its bars, or half a gate shows only its edges.** Clipping holes before triangulating broke `tessellate_polygon`.
  `cutout_solid` now triangulates the outline first and clips the triangles. A `[zone_detail] WARNING: … triangulated to …` line means a new case. ["Upscale and relief artefacts"]
- **An opening outline misses its painting.** It's a data error in `facades.json`. Check `facade_check/openings_sheet.png`.
  Automatic edge fitting was tried and doesn't work on these textures. ["Opening outlines that miss their painting"]
- **The parapet corner shows a slit or half-merlons.** Each parapet keeps solid end blocks (`PARAPET_CORNER_M`), textured from the nearest painted merlon (`_merlon_shift`).
  A faint texture seam at the top of the corner merlon is still open. ["Selective rebuild"]
- **Z-fighting (flickering stripes) at corners, roofs or trims.** Two pieces of art filling the same space with faces in the same plane.
  `parapet_corner_trims` handles parapets (trim one at right-angle corners, mitre oblique ones, no end face against a wall), `roof_neighbour` stops overhangs on edges shared between roof pieces, and `wall_continues` stops bands and plinths from wrapping where a wall continues or the other wall of a corner already wraps.
  Find new cases with `tools/blender/check_overlaps.py`. Harmless reports: trim back faces on the wall plane, box bottoms on the ground, the pond bed's skirts (underwater). Duplicated faces in some cut-out signs are a known follow-up. ["Plain facades back"]
- **Textures sit wrong on walls** (half clocks, seams). `roo2gltf` must follow the original client's UV rules (`wall_uvs`, `docs/findings.md`). Fix the blockout, not the art. ["Texture alignment fix"]
- **A thin line along the top of a fence, gate or tree line.** Two causes:
  - A wall exactly one texture tall has the next repeat start on its top edge. `cutout_solid` skips the zero-height pieces that repeat clips to; they used to become flat strips.
  - Wrap sampling blended the bottom row into the top edge. Masked textures clamp V (`environment_materials.vertically_tiled` keeps tiling ones on wrap). ["Lines along the tops of cut-outs"]
- **A new `zone_<rid>.json` suddenly has facade relief.** `rebuild` was missing, and missing means all of them.

## Textures and relief
- **Window glass wobbles.** Inferred height and normals on painted glazing.
  `flatten_windows` fills the window from its surroundings, the `pane` variant has `NormalStrength` 0, and `relief.flat_openings` drops the normals entirely. Every painted window needs a `facades.json` opening. ["Upscale and relief artefacts"]
- **Signs and small pictures look smeared or repainted.** That's the upscaler. Real-ESRGAN repaints; the GTAV dither model stays faithful.
  Compare models with `upscaler_test.ps1` before changing anything. ["Upscaler models"]
- **Roofs look flat.** Marigold returns nearly flat normals on roof textures. `relief.rules_for: "roof"` keeps the rule-based `luma` maps.
  The `stones` mode makes blobs on slate. Barn shingles still change little. ["Selective rebuild"]
- **Mortar is raised instead of sunk.** Luminance-as-height on light mortar. The `stones` mode uses an Otsu minority split.
  `grd20232` (the town wall) still splits its mottling rather than its mortar.
- **Normals look inverted** (lit from below). UE wants DirectX (green-down) normals.
  Pillow's `ImageFilter.Kernel` flips kernels vertically, so test any new filter with a synthetic raised square. ["Proof of concept"]
- **Big stones on the ground show small stones' bumps.** `M_Ground` blends the texture at two scales. Any map added to it (normal, roughness, height) must be sampled at both scales and blended with the same weight as the colour. ["Ground normals"]
- **Displacement makes melted stone, cracks at corners, or doubled window depth.** That's why displacement is off.
  If it's ever revisited, the grid mask (vertex colour R) and `displacement_facade_scale` exist for this. ["Plain walls", "Textures-only test"]

## Unreal build
- **A material change didn't apply.** `_master`'s cache key hashes the source of the builder and the helpers listed in `environment_materials._master`.
  A new helper function called by a master must be added to that list, or the master never rebuilds.
- **A new material property didn't reset on rebuild.** Add it to `RESET_PROPERTIES` (`material_domain` is there for the grime decal).
- **The decal material warns while compiling.** Set the blend mode before the domain (see `build_grime_master`).
- **Window glow is blown out to flat white.** Use a multiply (base × warm × mask × glow), not a lerp toward white.
  Night is 0.35 and dusk 0.12. ["Moods and lit windows"]
- **The clock or atlas shows the neighbouring cell's edge.** UVs must be clamped inside the cell (`_atlas_uv` clamps to 0.0005–0.9995).
- **Textures are stuck at low mips, or Nanite fallback meshes show (blur, dark gaps in parapets) in a capture right after an in-place mesh reimport.**
  Remove those meshes' entries from `Saved/MRBuild/world_cache.json` (keys starting `/Game/Generated/Zones/Z<rid>/Art/SM_Z<rid>_<Building>`) and build again. It has happened twice, so expect it after big Blender changes and judge look-dev only after the forced reimport. ["Known flake"]
- **A GPU crash (D3D12 page fault) when a headless editor opens `L_World`.** `build_world.ps1` starts on the Entry map for this reason; don't change that.
  Look-dev runs `-unattended` and retries once. ["Look-dev survives a GPU crash"]
- **New C++ isn't picked up.** Compile, then restart the editor. The build runs inside the open editor, which still has the old DLL.

## Look-dev and process
- **Two labels differ everywhere.** Captures made with the old fixed wait (before 2026-10-04: `raza4`, `incr1`, …) aren't comparable with settled, frozen-time captures.
  Clock captures need the same `-GameHour`, and mood comparisons need the same mood.
- **A change looks fine on one camera and breaks elsewhere.** The user's bar is "everywhere all the time".
  Capture all cameras before calling anything done, and add a camera for every new problem spot.
- **Stopping a long upscale also killed its own watcher and left an orphan python worker** (exit 255).
  Run long jobs in the background, and after stopping one, check for leftover `python.exe` from `build/texai/.venv`.
- **Inline heredocs to `cat` can hang in this shell.** Write patch scripts to the scratchpad and run them.
