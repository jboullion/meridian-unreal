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
- **The wrong model arrives:** never rely on arrival order. The collector matches by task id.
- **The tab jumps to another model:** Studio follows new jobs on the account. Check the URL's task id.
- **A click selects the model in the viewport** (a gizmo appears): the Export panel had closed. Press Escape and reopen Export.
