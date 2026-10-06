# Style bible: AI restyles of the original sprites

`tools/aigen/restyle.py` sends the text between the `style-block` markers with every restyle request,
after the per-asset line from the manifest (`describe`) and the view instruction. Change it here, not in code;
the restyle cache is keyed on it, so a change reruns every restyle that uses it.

The goal: the same object as the original sprite (identity, silhouette, colours, motifs), redrawn as a clean
reference image an image-to-3D generator can read. We are not after a new art style. Depth and
detail should come from the 3D model and the game's lighting, not from painted shading.

<!-- style-block -->
Style: a faithful high-resolution remaster of a 1990s pre-rendered fantasy game sprite.
Keep the original's colours, materials, proportions and ornament; add only the fine detail the low resolution hid.
Matte, slightly worn, hand-made materials with muted earthy colours; no glossy or plastic look.
Lighting: soft, even, neutral studio light from the front, no strong highlights, no rim light, no cast or ground shadow.
Background: plain flat light grey (#C8C8C8), nothing else in the image.
The whole object is visible, centred, upright, with a margin around it; no cropping.
No text, no labels, no watermark, no border, no people, no added props.
<!-- /style-block -->

## Palette notes per family
Fill these in as the families come up. Each note keeps a family in one colour range while staying close to the original's colours.

- **Iron and bronze fittings** (brazier, lamp post, chandelier): near-black iron with brown rust in the recesses; bronze is dull brown, not gold.
- **Wood** (boxes, tables, stools): weathered mid-brown, visible grain, no varnish shine.

## Golden references
None approved yet. After 3–5 props are approved, list them here (paths under `build/aigen/`) and send them
as extra style references with each request (docs/adr/0007).
