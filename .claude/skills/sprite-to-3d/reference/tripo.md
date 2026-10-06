# Tripo reference

Facts checked 2026-10-06 against the vendor docs. Re-check prices and model ids before relying on them.

## Two billing systems
- **Tripo Studio** (the web app at tripo3d.ai) runs on the subscription's credits. The user has the **Max** plan for October 2026: private models, commercial use, batch runs in the web UI.
- **The developer API** (platform.tripo3d.ai) has its **own prepaid wallet**. Studio credits can't be spent through the API ([FAQ](https://platform.tripo3d.ai/docs/faq)).
  - 1 API credit = $0.01; credits never expire.
- So: Studio by hand (handoff folder) for now. The API backend in `tools/aigen/tripo.py` comes once the recipe settles and the user tops up the wallet.

## Studio settings that worked
| Asset | Input | Model | Settings | Result |
|---|---|---|---|---|
| Props (Raza batch) | 1 image (OpenAI restyle) | HD H3.1 | Ultra Mesh + AI Complete, 2K PBR, Remove Lighting, tri 8000 | 45 credits; good for symmetric objects |
| Creatures (ant, fungus beast) | 4 views (OpenAI) | HD H3.1 | as above, multi-view tab | 45 credits; real depth from the side; the most painted detail |
| Creatures | 4 views or 1 image | Smart Mesh P2.0 | tri 2k/5k/10k/20k (4 generations), then Texture 2K + Remove Lighting | 100 a run + 10 per texture; clean topology; 10k keeps teeth and joints |

Record each run in the manifest too (`tripo.runs.<variant>.settings`).

## API V3 (for the batch backend)
- **V2 (`api.tripo3d.ai/v2/openapi`) shuts down 2026-10-31 16:00 UTC.** The `tripo3d` Python SDK is V2-first, so don't use it. Call V3 with `requests`.
  - Migration guide: https://developers.tripo3d.com/en/docs/migration-v2-to-v3.md
  - Docs index: https://developers.tripo3d.com/llms.txt (every page also exists as `.md`)
- **Base URL:** `https://openapi.tripo3d.com/v3`. Note it's `.com`.
- **Auth:** `Authorization: Bearer tsk_…` (env `TRIPO_API_KEY`). A `tcli_` client id gives a 401.
- **Upload:** `POST /v3/files` (multipart; PNG or JPEG up to 20 MB) → `data.file_token`.
- **Tasks:** `POST /v3/generation/image-to-model` and `/generation/multiview-to-model`.
  - Multiview takes named views front/left/back/right; front is required and at least 2 views are needed. "Left" is the object's own left. No Turbo.
  - Other operations: `/models/convert`, `/models/texture`, `/mesh/decimate`, `/mesh/segment`, `/animations/rig`.
- **Poll:** `GET /v3/tasks/{id}` every 1–2 s, or `POST /v3/tasks/list` for several. Statuses: queued, running, success, failed, banned, expired, cancelled.
  - Results are `output.model_url` (GLB) and `rendered_image_url`.
  - **Download at once:** the URLs expire in minutes. Re-query the task for a fresh one.
- **Model versions:** always set one explicitly.
  - H series: `v3.1-20260211` (newest), `v3.0-20250812`, `v2.5-20250123`, `Turbo-v1.0-20250506`.
  - P series (low-poly): `P1-20260311`, and `P2-20260801` with quads.
  - The Quick Start shows `tripo-v3.1`; test which ids are accepted.
- **image-to-model options:**
  - `texture`, `pbr`
  - `texture_quality` fast/standard/detailed/extreme
  - `geometry_quality` standard/detailed
  - `face_limit`: with `smart_low_poly` 1k–20k; quad 500–10k; P1 50–20k, P2 48–50k
  - `texture_alignment` original_image/geometry
  - `orientation` default/align_image, `auto_size`, `quad`, `smart_low_poly`
  - `generate_parts`: needs texture, pbr and quad all off
  - `model_seed`, `texture_seed`, `enable_image_autofix`
- **convert:** `pivot_to_center_bottom`, `flatten_bottom`, `face_limit`, `texture_size`, `export_orientation`, `scale_factor`. Formats GLTF/FBX/USDZ/OBJ/STL/3MF.
- **Prices (credits):**
  - image or multiview → 3D: 20 untextured, 30 with standard texture; P1 40/50
  - add-ons: detailed texture +10–20, detailed geometry +20, quad +5, smart low-poly +10, parts +20
  - convert 5–10, retopology 10–30, segmentation 40, rig 25
  - Check `credits_consumed` on real tasks; the two pricing pages disagree on texture surcharges.
- **Limits:** concurrency per account is H 10, P 5. Rate limits are per key (HTTP 429, code 1007).
- **Licence:** paid plans give private models with commercial use (Terms: no training competing models). The API-specific terms are not stated separately; ask support@tripo3d.ai before shipping API output publicly.

## Integrations
Official plugins exist for Unreal, Blender, Unity, Godot and ComfyUI (https://developers.tripo3d.com/en/docs/plugins.md), all API-key based. There's also an alpha MCP server that works through Blender (github.com/VAST-AI-Research/tripo-mcp). We use none of them yet: the handoff folder plus `prop_glb.py` keeps every step scripted and cached.

## Input images
One subject, centred, whole object visible, clean background, even lighting, at least 256 px (we send 1024²). Studio removes backgrounds itself. We send the raw upscale as a transparent PNG (variant A) and the restyles on flat grey.
