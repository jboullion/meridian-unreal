# Pitfalls (symptom → cause → fix)

- **Cyan or dark halo around the upscaled sprite** → the palette's transparent key colour was blended into the edge. → `sprite.fill_transparent` fills transparent pixels from their opaque neighbours before the model runs. Never upscale the raw RGBA.
- **The upscaler drops alpha** → `tools/textures/upscale.py` works in RGB. → `sprite.upscale_rgba` rebuilds alpha from the 1-bit original (bilinear, blur, levels).
- **The procedural kit mesh isn't the original's shape** → `SM_Brazier` is a tripod, but the sprite shows a single stem on a round foot. Compare against the sprite, not the kit.
- **The flame or effects end up in the mesh** → the sprite frame had them. → Use the unlit frame (brazier: frame 0); flames, glows and smoke stay flipbooks or Niagara.
- **The model is the wrong size in game** → Tripo picks an arbitrary scale. → Always `normalize`; it scales to the sprite's `height_m`.
- **The model faces the wrong way** → its front isn't glTF +Z. → Set `yaw_deg` in the manifest and re-run `normalize`. Kod yaw placement is not wired up yet (`build_world.py` spawns props unrotated).
- **The Studio Max credits don't work with the API key** → there are two separate billing systems. → See reference/tripo.md.
- **Gemini `RESOURCE_EXHAUSTED`, "free_tier_requests, limit: 0"** → the key belongs to a free-tier project, and the free tier has no image-model quota (Pro or Flash). → Use a key from a Google AI Studio project with billing on. `.env` beats a `GEMINI_API_KEY` set machine-wide (`tools/aigen/env.py`).
- **A restyle drops a key feature** (the egg basket's eggs became dark twigs) → the sprite was tiny (29×35 px), so the model guessed. → Name the feature plainly and repeat it in `describe` ("heaped full of smooth white hen eggs, plainly visible"), then `restyle --force` just that asset.
- **A thin item is unreadable** (the wand's lying frame is 177×11 px) → generate from a frame where it's readable and size from another (`frame`, `size_frame`).
- **A hanging object sits on the floor** → its sprite has empty rows below; `lift_m` (from the visible bounds) raises the mesh in `normalize`.
- **Sizes look big** (a 1.13 m table, a 1.31 m museum breastplate) → those are the original's sizes. Keep them unless the user asks; override per manifest if needed.
- **A leafy tree or shrub comes out as a solid lump with leaves painted on** → image-to-3D makes one closed surface, and foliage needs alpha-cut leaf cards. → Don't send foliage through Tripo. Trees go through `tools/blender/build_tree_kit.py` or the Procedural Vegetation Editor (ADR 0007 "Trees"). Leafless trees, rocks and stumps are fine in Tripo.
