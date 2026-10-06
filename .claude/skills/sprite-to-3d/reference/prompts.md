# Restyle prompts

`tools/aigen/restyle.py` builds one prompt per (provider, view):

1. A fixed opening: "Redraw the object in the first image as a clean, high-resolution reference image for 3D modelling. Keep it recognisably the same object…"
2. `Object: <manifest describe>.`
3. For any view other than front, "The second image is the approved front view…", with that provider's front restyle sent as a second image.
4. The view line (`restyle.VIEWS`): front, left, back, right, or above (three-quarter from 35°).
5. `restyle.extra` from the manifest, if set.
6. The style block from `tools/aigen/style/style.md` (between the `style-block` markers).

The images sent are `01_upscale/canvas_grey.png` (the 4x upscale centred on 1024² flat grey) plus, for other views, the front restyle.

The prompt text, the model id and the input images are all hashed. Changing any of them reruns only the restyles they affect; unchanged ones are never paid for twice.

## Models
- `vertex`: `gemini-3-pro-image` (Nano Banana Pro) on Vertex AI.
  - Endpoint `aiplatform.googleapis.com/v1/publishers/google/models/<id>:generateContent`, header `x-goog-api-key`, `responseModalities: ["IMAGE"]`, `imageConfig` 1:1 2K.
  - Paid from Google Cloud billing, so the Developer Program credits apply.
- `gemini`: the same models through AI Studio (`generativelanguage.googleapis.com`), which is prepaid; the credits don't apply.
- `openai`: `gpt-image-2.5-sunburst` (the editing-focused model) through `/v1/images/edits` at 1024², quality high. **Don't send `input_fidelity`:** the gpt-image-2 family rejects it.
- `fal@<model>`: any fal.ai edit model taking `prompt` + `image_url` (data URI) at `https://fal.run/<model>`, header `Authorization: Key …`. Tried: `fal-ai/flux-pro/kontext` (7 s, good), `fal-ai/qwen-image-edit` (slow, glossy, adds a shadow).
- `python tools/aigen/aigen.py models` lists the image models the Gemini and OpenAI keys can use.

## Writing `describe`
- Describe what the **sprite** shows, after looking at the 4x upscale: materials, parts, how many of each, what's attached where. Don't describe what you expect.
- Name what must *not* appear when the sprite has it in other frames (e.g. "the bowl is empty and unlit" for the brazier, whose flames are a separate flipbook).
- Keep it to one sentence of parts plus one of condition ("aged and worn").

## Per-kind lines that worked
| Kind | Line | Notes |
|---|---|---|
| (fill in) | | |
