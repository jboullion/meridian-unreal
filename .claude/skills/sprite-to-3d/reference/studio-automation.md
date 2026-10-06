# Driving Tripo Studio (browser + DCC Bridge)

This is how Claude runs the Tripo step without the user: Studio in the user's Chrome (Claude in Chrome, already signed in), with models collected through the **Tripo DCC Bridge** into the user's open Blender. The user approved this on 2026-10-06. Generating spends the Max plan's web credits, so say what a batch costs before starting it.

## One-time setup (the user's machine)
- **Blender 5.2** stays open with two add-ons:
  - **Tripo3d_Blender_Bridge:** a websocket server on `127.0.0.1:60600`. Its log is `%TEMP%\tripo3d_blender_bridge.log`.
  - **"MCP for Blender"** (`blender_mcp.py`): a JSON socket on `127.0.0.1:9876`. `tools/aigen/blender_link.py` talks to it directly.
  - This session's `mcp__Blender__*` tools expect Blender's *official* MCP extension, which isn't installed, so they time out. Use `blender_link.py` instead.
- **Chrome** has a tab on `https://studio.tripo3d.ai/workspace/generate`.

## Generate (HD Model panel, left)
1. Set **Privacy → Private**. It defaulted to "Sharing Only".
2. **AI Model:** H3.1 – Best Quality (45 credits) or H3.0 – Fast & Balanced (30).
3. **Geometry & Texture** (panel; settings persist between sessions):
   - Ultra Mesh Quality on (H3.1) / AI Complete (H3.0)
   - Texture on, Texture Quality **2K**, Remove Lighting **on**, PBR on
   - Topology Triangle, Polycount **8000** (type it into the box, then Enter)
   - 8K Texture off; it's a one-time trial.
4. Upload without clicking: `find` "image file input in Generate Model upload area", then `file_upload` the PNG from `03_tripo_in/` to that ref.
5. Click **Generate**. Jobs run **in parallel**: remove the image (the bin icon on the thumbnail), upload the next one and Generate again.
6. Each job gets a page `…/workspace/generate/<task id>`. Note which task id belongs to which variant from the URL right after clicking Generate.
7. For many images, the third input tab is **Batch Images to 3D** (up to 30 images).

## Multi-view and Smart Mesh
- **Multi-view** is the second input tab (the cube icon), with Front, Left, Right and Back slots. `find` "file inputs for Front, Left, Right, Back image slots" and `file_upload` each one.
  - Smart Mesh refused a front image alone ("Please upload an image before proceeding"); fill all four.
  - The slots keep their images when you switch between HD Model and Smart Mesh.
  - To swap images, click each thumbnail's bin, Back first, then Right, Left and Front.
- **Smart Mesh** is the second toggle at the top of the panel.
  - **Privacy resets to "Sharing Only"** when you switch to it: set it to Private every time.
  - Topology panel: Triangle (500–50,000) or Quad (500–25,000). Turn off "Use Same Polygon Count" to give each of the 1, 2 or 4 generations its own count. Re-check Triangle when you reopen it.
  - A run costs 100 credits whatever the number of generations. Each generation is its own task id: read them from the Assets grid, and match each to its count by the "Faces" readout or by the triangles the collector reports.
- **Texturing a Smart Mesh model:** the left bar's Texture tool (page `/workspace/texture/<task id>`).
  - It reuses the model's input images. Set Remove Lighting on and pick 2K (10 credits), 4K, or 8K (30).
  - The bottom bar's **Texture** button starts at once, without the options.
  - The textured model keeps the same task id. Record it as a new variant (e.g. `SM5kT`) with the same `tripo_task`, so the collector files it next to the untextured one.
- **Export positions move** with the page. In the 1512x790 frame:

  | Page | Export | Send To | Blender item |
  |---|---|---|---|
  | HD model | (972,757) | (878,693) | (866,384) |
  | Smart Mesh untextured | (1016,757) | (918,693) | (907,384) |
  | Smart Mesh textured | (948,757) | (850,693) | (840,384) |
  | Texture page | (957,757) | (867,693) | (848,384) |

  Take a screenshot after the first Export on a new page type.

## Collect over the bridge (many models)
1. **Keep the Chrome window visible on screen.** While it's covered or minimised the page is "hidden" (`document.visibilityState`), and Studio's popovers (DCC Bridge panel, Send To menu) never open.
2. **Connect once, then never reload the page.** Any `navigate` drops the bridge, and it doesn't reconnect by itself. Open **DCC Bridge** (top bar), toggle Blender on, and check it shows "Connected".
3. Start the collector in the background: `python tools/aigen/aigen.py bridge-collect all`. It waits for every manifest's `tripo.runs.<variant>.tripo_task` that has no GLB yet, matches arrivals by the task id in the bridge log, and exports each one to the right `04_tripo_out/`.
4. **Switch models without reloading:** the Assets grid's thumbnails are router links (`a[href*="<task id>"]` inside the `flex-wrap` grid). The list loads lazily, so scroll its container until the link exists, then `click()` it from page JS. A helper `window.__open(id)` was defined for this; re-define it after any reload.
5. For each model, use **real clicks**, not scripted ones: Studio ignores a script `click()` on Send To.
   - bottom-bar **Export**, which reopens the panel (it closes after every send);
   - **Send To**;
   - **Send To Blender**, the top item of the menu.

   One browser batch per model: `__open` → wait about 6 s for the model to load → Export → Send To → Blender item.
   With waits of about 3 s, 10 of 60 sends were silently lost. Resend whatever the collector still lists as missing.
6. The bridge sends FBX, which Blender imports and the collector re-exports as GLB, with textures embedded.

## Pitfalls
- **Nothing arrives:** the bridge disconnected (a page reload), the window was hidden, or the click landed before the model loaded. Check the tail of `%TEMP%\tripo3d_blender_bridge.log`.
- **The wrong model arrives:** never rely on arrival order. The collector matches by task id, and finds the import by the exact name the bridge log says it got. Tripo reuses names, and Blender adds `.001` to the second copy.
- **A big model (20k) is missed:** give it about 10 s to load before Export.
- **The tab jumps to another model:** Studio follows new jobs on the account. Check the URL's task id.
- **A click selects the model in the viewport** (a gizmo appears): the Export panel had closed. Press Escape and reopen Export.
