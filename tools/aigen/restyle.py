"""
Restyle step: the upscaled sprite (identity) + the style bible (tools/aigen/style/style.md) + the
asset's description -> a clean reference image per view. Plain REST through requests (in
build/texai/.venv); keys in the repo-root .env. Providers:

    vertex   Gemini image models (Nano Banana) on Vertex AI, VERTEX_API_KEY: postpaid Google Cloud,
             so the Developer Program credits pay for it (AI Studio's prepaid billing can't use them)
    gemini   the same models through Google AI Studio, GEMINI_API_KEY (prepaid)
    openai   GPT Image edits, OPENAI_API_KEY
    fal      any fal.ai image-edit model taking prompt + image_url (Flux Kontext, Qwen-Image-Edit, ...),
             FAL_AI_API_KEY

A provider entry in a manifest may name its model as "provider@model" (e.g.
"fal@fal-ai/qwen-image-edit"); otherwise DEFAULT_MODELS or the manifest's "<provider>_model".
`python tools/aigen/aigen.py models` lists the image models the Gemini and OpenAI keys can use.
"""
import base64
import io
import json
import re
import time
from pathlib import Path

import requests
from PIL import Image

import env

STYLE_FILE = Path(__file__).resolve().parent / "style" / "style.md"
DEFAULT_MODELS = {"vertex": "gemini-3-pro-image", "gemini": "gemini-3-pro-image", "openai": "gpt-image-2.5-sunburst",
                  "fal": "fal-ai/flux-pro/kontext"}
TIMEOUT = 600

VIEWS = {
    "front": "View: straight from the front at eye level, the same viewpoint as the reference image.",
    "left": "View: a true side profile. The object itself is rotated 90 degrees about its vertical axis so we see its "
            "left side, at eye level and the same scale; its front now points to the left of the image. This must look "
            "clearly different from the front view, not a copy of it. Infer the unseen side so it is consistent with the front.",
    "back": "View: the object itself is rotated 180 degrees about its vertical axis, seen from directly behind at eye level "
            "and the same scale: no face or front details are visible. Infer the unseen side so it is consistent with the front.",
    "right": "View: a true side profile. The object itself is rotated 90 degrees the other way about its vertical axis so "
             "we see its right side, at eye level and the same scale; its front now points to the right of the image.",
    "above": "View: a three-quarter view from about 35 degrees above, so the top and any opening are visible.",
}


def style_block() -> str:
    text = STYLE_FILE.read_text(encoding="utf-8")
    m = re.search(r"<!-- style-block -->(.*?)<!-- /style-block -->", text, re.S)
    if not m:
        raise SystemExit("%s has no style-block markers" % STYLE_FILE)
    return " ".join(line.strip() for line in m.group(1).strip().splitlines() if line.strip())


def prompt(describe: str, view: str, has_front_ref: bool, extra: str = "") -> str:
    parts = ["Redraw the object in the first image as a clean, high-resolution reference image for 3D modelling.",
             "Keep it recognisably the same object: same shape, silhouette, proportions, colours and details.",
             "Object: %s." % describe.rstrip(".")]
    if has_front_ref:
        parts.append("The second image is the approved front view of the redraw; match its design, colours and detail exactly.")
    parts.append(VIEWS[view])
    if extra:
        parts.append(extra)
    parts.append(style_block())
    return " ".join(parts)


def _png_bytes(img: Image.Image) -> bytes:
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return buf.getvalue()


def _raise(r: requests.Response, what: str):
    if r.status_code >= 400:
        # error bodies don't echo keys; keep them short
        raise RuntimeError("%s: HTTP %d %s" % (what, r.status_code, r.text[:800]))


def _gemini_call(url: str, key: str, images: list, text: str, model: str):
    parts = [{"text": text}] + [{"inline_data": {"mime_type": "image/png", "data": base64.b64encode(_png_bytes(im)).decode()}}
                                for im in images]
    body = {"contents": [{"role": "user", "parts": parts}],
            "generationConfig": {"responseModalities": ["IMAGE"], "imageConfig": {"aspectRatio": "1:1", "imageSize": "2K"}}}
    r = requests.post(url, json=body, headers={"x-goog-api-key": key}, timeout=TIMEOUT)
    if r.status_code == 400 and "image" in r.text.lower() and "config" in r.text.lower():
        body["generationConfig"].pop("imageConfig")  # older models take no imageConfig
        r = requests.post(url, json=body, headers={"x-goog-api-key": key}, timeout=TIMEOUT)
    _raise(r, "%s" % model)
    data = r.json()
    for cand in data.get("candidates", []):
        for part in cand.get("content", {}).get("parts", []):
            inline = part.get("inlineData") or part.get("inline_data")
            if inline and inline.get("data"):
                return Image.open(io.BytesIO(base64.b64decode(inline["data"]))), {"usage": data.get("usageMetadata")}
    raise RuntimeError("%s returned no image: %s" % (model, str(data)[:800]))


def gemini(images: list, text: str, model: str):
    url = "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent" % model
    return _gemini_call(url, env.require("GEMINI_API_KEY"), images, text, model)


def vertex(images: list, text: str, model: str):
    url = "https://aiplatform.googleapis.com/v1/publishers/google/models/%s:generateContent" % model
    return _gemini_call(url, env.require("VERTEX_API_KEY"), images, text, model)


def openai(images: list, text: str, model: str):
    key = env.require("OPENAI_API_KEY")
    files = [("image[]", ("ref%d.png" % i, _png_bytes(im), "image/png")) for i, im in enumerate(images)]
    data = {"model": model, "prompt": text, "size": "1024x1024", "quality": "high", "output_format": "png", "n": "1"}
    r = requests.post("https://api.openai.com/v1/images/edits", headers={"Authorization": "Bearer " + key},
                      data=data, files=files, timeout=TIMEOUT)
    _raise(r, "openai %s" % model)
    out = r.json()
    return Image.open(io.BytesIO(base64.b64decode(out["data"][0]["b64_json"]))), {"usage": out.get("usage")}


def fal(images: list, text: str, model: str):
    key = env.require("FAL_AI_API_KEY")
    uris = ["data:image/png;base64," + base64.b64encode(_png_bytes(im)).decode() for im in images]
    body = {"prompt": text, "image_url": uris[0], "output_format": "png", "num_images": 1}
    if len(uris) > 1:
        body["image_urls"] = uris
    r = requests.post("https://fal.run/" + model, json=body, headers={"Authorization": "Key " + key}, timeout=TIMEOUT)
    _raise(r, "fal %s" % model)
    out = r.json()
    img = requests.get(out["images"][0]["url"], timeout=120)
    _raise(img, "fal download")
    return Image.open(io.BytesIO(img.content)), {"seed": out.get("seed")}


def openai_batch_line(custom_id: str, images: list, text: str, model: str) -> dict:
    """One /v1/images/edits request for the Batch API (JSON body, images as data URLs)."""
    return {"custom_id": custom_id, "method": "POST", "url": "/v1/images/edits",
            "body": {"model": model, "prompt": text, "size": "1024x1024", "quality": "high", "output_format": "png", "n": 1,
                     "images": [{"image_url": "data:image/png;base64," + base64.b64encode(_png_bytes(im)).decode()}
                                for im in images]}}


def _openai_headers():
    return {"Authorization": "Bearer " + env.require("OPENAI_API_KEY")}


def openai_batch_create(lines: list) -> dict:
    body = "\n".join(json.dumps(line) for line in lines).encode()
    r = requests.post("https://api.openai.com/v1/files", headers=_openai_headers(), data={"purpose": "batch"},
                      files={"file": ("aigen_batch.jsonl", body, "application/jsonl")}, timeout=TIMEOUT)
    _raise(r, "openai batch upload")
    r2 = requests.post("https://api.openai.com/v1/batches", headers=_openai_headers(), timeout=60,
                       json={"input_file_id": r.json()["id"], "endpoint": "/v1/images/edits", "completion_window": "24h"})
    _raise(r2, "openai batch create")
    return r2.json()


def openai_batch_get(batch_id: str) -> dict:
    r = requests.get("https://api.openai.com/v1/batches/" + batch_id, headers=_openai_headers(), timeout=60)
    _raise(r, "openai batch status")
    return r.json()


def openai_batch_results(batch: dict) -> dict:
    """custom_id -> {"image", "usage"} or {"error"} for a finished batch."""
    out = {}
    for fid in (batch.get("output_file_id"), batch.get("error_file_id")):
        if not fid:
            continue
        r = requests.get("https://api.openai.com/v1/files/%s/content" % fid, headers=_openai_headers(), timeout=TIMEOUT)
        _raise(r, "openai batch results")
        for line in r.text.splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            resp = row.get("response") or {}
            body = resp.get("body") or {}
            if row.get("error") or resp.get("status_code", 200) >= 400 or not body.get("data"):
                out[row["custom_id"]] = {"error": row.get("error") or body.get("error") or resp.get("status_code")}
            else:
                out[row["custom_id"]] = {"image": Image.open(io.BytesIO(base64.b64decode(body["data"][0]["b64_json"]))),
                                         "usage": body.get("usage")}
    return out


PROVIDERS = {"vertex": vertex, "gemini": gemini, "openai": openai, "fal": fal}


def split(entry: str, cfg: dict):
    """"provider" or "provider@model" -> (provider, model, file slug). fal outputs are always named by
    model (fal-ai/flux-pro/kontext -> fal-flux-pro-kontext), as are the others when a model is given."""
    provider, _, model = entry.partition("@")
    model = model or cfg.get("%s_model" % provider, DEFAULT_MODELS[provider])
    if provider != "fal" and "@" not in entry:
        return provider, model, provider
    name = "-".join(model.split("/")[1:]) if provider == "fal" else model
    return provider, model, "%s-%s" % (provider, name)


def run(provider: str, images: list, text: str, model: str):
    """-> (image, info {model, seconds, usage})."""
    t = time.time()
    img, info = PROVIDERS[provider](images, text, model)
    info.update(model=model, seconds=round(time.time() - t, 1))
    return img, info


def list_models() -> dict:
    """Image-capable model ids per provider whose key is set."""
    out = {}
    if env.get("GEMINI_API_KEY"):
        r = requests.get("https://generativelanguage.googleapis.com/v1beta/models", params={"pageSize": 1000},
                         headers={"x-goog-api-key": env.get("GEMINI_API_KEY")}, timeout=60)
        _raise(r, "gemini models")
        out["gemini"] = sorted(m["name"].removeprefix("models/") for m in r.json().get("models", []) if "image" in m["name"])
    if env.get("OPENAI_API_KEY"):
        r = requests.get("https://api.openai.com/v1/models", headers={"Authorization": "Bearer " + env.get("OPENAI_API_KEY")}, timeout=60)
        _raise(r, "openai models")
        out["openai"] = sorted(m["id"] for m in r.json().get("data", []) if "image" in m["id"])
    return out
