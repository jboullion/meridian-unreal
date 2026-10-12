"""
The Tripo developer API (V3) backend for the image -> 3D step: the same 03_tripo_in/ images and
04_tripo_out/<variant>.glb as the Studio route, without a browser (docs/adr/0007 "Overnight batch").

    python tools/aigen/aigen.py <assets> tripo-api [variant]     # every variant without a GLB, or one
    python tools/aigen/aigen.py tripo-balance                     # the API wallet

Billed from the separate API wallet (platform.tripo3d.ai), key TRIPO_API_KEY in the repo-root .env;
the Max plan's Studio credits can't be spent here. V2 and the tripo3d SDK stop working 2026-10-31, so
this calls V3 with requests. Facts: .claude/skills/sprite-to-3d/reference/tripo.md.

Settings, matched to Studio's HD Model (H3.1, Ultra Mesh + AI Complete, 2K PBR, Remove Lighting,
triangles): model v3.1, geometry_quality "detailed" (Ultra Mesh), pbr, delight (Remove Lighting), enable_image_autofix (AI Complete;
single image only), face_limit = the manifest's polycount; texture "detailed" with texture version
v3.5 (the settings test of 2026-10-10: "standard" came out softer than Studio's).
"""
import json
import struct
import time
from pathlib import Path

import requests

import env

API = "https://openapi.tripo3d.ai/v3"  # international (platform.tripo3d.ai keys); openapi.tripo3d.com is the Chinese platform
MODEL = "v3.1-20260211"
# 2026-10-10 settings test (stump and chest beside their Studio models): texture_quality "standard" came out
# soft and smooth, "detailed" with the newest texture version matched or beat Studio (60 credits a model)
TEXTURE_QUALITY = "detailed"
TEXTURE_VERSION = "v3.5-20260815"
VIEWS = ("front", "left", "back", "right")
POLL_S = 5
MAX_PARALLEL = 8  # the account runs 10 H-series tasks at once; leave room for a retry
TASK_TIMEOUT_S = 30 * 60


def options(polycount: int, multiview: bool) -> dict:
    o = {"model": MODEL, "texture": True, "pbr": True, "texture_quality": TEXTURE_QUALITY, "texture_version": TEXTURE_VERSION,
         "geometry_quality": "detailed", "delight": True}
    if int(polycount) > 0:
        o["face_limit"] = int(polycount)
    # polycount 0: Tripo's own adaptive mesh, reduced afterwards by normalize (manifest "max_tris"). For
    # clusters of small round things (a basket of corn, a heap of grapes) a face_limit shatters them into
    # shards (2026-10-11); Blender's collapse keeps their shapes
    if not multiview:
        o["enable_image_autofix"] = True
    return o


def settings_text(o: dict, views: int) -> str:
    return "API v3 %s, geometry %s, texture %s %s, PBR, delight, %s%d faces, %s" % (
        o["model"], o["geometry_quality"], o["texture_quality"], o.get("texture_version", ""), "autofix, " if o.get("enable_image_autofix") else "",
        o.get("face_limit", 0), "single image" if views == 1 else "multi-view (%d views)" % views)


def face_front(glb: Path):
    """The API exports models facing glTF +X (export_orientation "+x"); Studio and this pipeline face
    them along +Z (prop_glb.py, normalize). Wrap the scene's root nodes in a node turned -90 degrees
    about +Y, which takes +X to +Z (2026-10-10: the smoke test's chest and stump came out a quarter
    turn off). Rewrites the GLB's JSON chunk only; the binary chunk is kept as it is."""
    data = glb.read_bytes()
    magic, version, _ = struct.unpack_from("<4sII", data, 0)
    jlen, jtype = struct.unpack_from("<I4s", data, 12)
    if magic != b"glTF" or jtype != b"JSON":
        raise TripoError("%s is not a GLB" % glb)
    gltf = json.loads(data[20:20 + jlen])
    rest = data[20 + jlen:]
    scene = gltf["scenes"][gltf.get("scene", 0)]
    gltf.setdefault("nodes", []).append({"name": "FaceFront", "rotation": [0.0, -0.70710678, 0.0, 0.70710678],
                                         "children": scene["nodes"]})
    scene["nodes"] = [len(gltf["nodes"]) - 1]
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    body = struct.pack("<I4s", len(js), b"JSON") + js + rest
    glb.write_bytes(struct.pack("<4sII", b"glTF", version, 12 + len(body)) + body)


class TripoError(RuntimeError):
    pass


class Client:
    def __init__(self):
        self.s = requests.Session()
        self.s.headers["Authorization"] = "Bearer " + env.require("TRIPO_API_KEY")

    def _call(self, method: str, path: str, **kw) -> dict:
        """One API call with retries: 429 (rate or concurrency limit) waits for Retry-After or backs off,
        5xx and network errors back off; any other error raises TripoError."""
        wait = 2.0
        for attempt in range(12):
            try:
                r = self.s.request(method, API + path, timeout=120, **kw)
            except requests.RequestException as e:
                err = "network: %s" % type(e).__name__
            else:
                if r.status_code == 429 or r.status_code >= 500:
                    err = "HTTP %d" % r.status_code
                    wait = float(r.headers.get("Retry-After") or wait)
                else:
                    try:
                        body = r.json()
                    except ValueError:
                        raise TripoError("HTTP %d, not JSON" % r.status_code)
                    if r.status_code >= 400 or body.get("code", 0) != 0:
                        raise TripoError("HTTP %d code %s: %s" % (r.status_code, body.get("code"), body.get("message")))
                    return body.get("data", body)
            time.sleep(wait)
            wait = min(wait * 2, 120)
        raise TripoError("gave up after retries (%s)" % err)

    def balance(self) -> dict:
        return self._call("GET", "/account/balance")

    def upload(self, path: Path) -> str:
        with open(path, "rb") as f:
            data = self._call("POST", "/files", files={"file": (path.name, f, "image/png")})
        return data["file_token"]

    def create(self, views: dict, opts: dict) -> str:
        """views {view: png path}: one image -> image-to-model, several -> multiview-to-model."""
        if len(views) == 1:
            body = dict(opts, input=self.upload(next(iter(views.values()))))
            data = self._call("POST", "/generation/image-to-model", json=body)
        else:
            if "front" not in views:
                raise TripoError("multi-view needs a front image")
            inputs = [{v: self.upload(views[v])} for v in VIEWS if v in views]
            data = self._call("POST", "/generation/multiview-to-model", json=dict(opts, inputs=inputs))
        return data["task_id"]

    def task(self, task_id: str) -> dict:
        return self._call("GET", "/tasks/%s" % task_id)

    def download(self, task_id: str, out: Path) -> dict:
        """Fetch the finished model to `out` (a fresh URL each try: they expire within minutes)."""
        for attempt in range(4):
            data = self.task(task_id)
            o = data.get("output") or {}
            url = o.get("pbr_model_url") or o.get("model_url")
            if not url:
                raise TripoError("task %s has no model url" % task_id)
            r = self.s.get(url, timeout=300) if url.startswith(API) else requests.get(url, timeout=300)
            if r.ok and r.content[:4] == b"glTF":
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(r.content)
                face_front(out)
                return data
            time.sleep(5)
        raise TripoError("task %s: could not download a GLB" % task_id)


def run(jobs: list, log=print, max_parallel: int = MAX_PARALLEL) -> dict:
    """Run jobs [{"name", "views", "opts", "out", "task" (an earlier task id to resume, or None),
    "submitted"(task_id), "finished"(data) or "failed"(message)}] at most max_parallel at a time.
    The task id is handed to submitted() before any polling, so an interrupted run resumes it rather
    than paying again. Returns {"done": n, "failed": n, "credits": total}."""
    client = Client()
    queue = list(jobs)
    running = {}  # task id -> job
    done = failed = 0
    credits = 0.0
    while queue or running:
        while queue and len(running) < max_parallel:
            job = queue.pop(0)
            try:
                tid = job.get("task") or client.create(job["views"], job["opts"])
            except TripoError as e:
                log("tripo-api: %s: submit FAILED %s" % (job["name"], e))
                job["failed"]("submit: %s" % e)
                failed += 1
                continue
            if not job.get("task"):
                job["submitted"](tid)
                log("tripo-api: %s submitted %s" % (job["name"], tid))
            else:
                log("tripo-api: %s resuming %s" % (job["name"], tid))
            job["since"] = time.time()
            running[tid] = job
        time.sleep(POLL_S)
        for tid, job in list(running.items()):
            try:
                data = client.task(tid)
            except TripoError as e:
                log("tripo-api: %s poll error %s" % (job["name"], e))
                continue
            status = data.get("status")
            if status == "success":
                del running[tid]
                try:
                    data = client.download(tid, job["out"])
                except TripoError as e:
                    log("tripo-api: %s download FAILED %s" % (job["name"], e))
                    job["failed"]("download: %s" % e)
                    failed += 1
                    continue
                credits += float(data.get("credits_consumed") or 0)
                job["finished"](data)
                done += 1
                log("tripo-api: %s done (%s credits) -> %s  [%d done, %d running, %d queued]" % (
                    job["name"], data.get("credits_consumed"), job["out"], done, len(running), len(queue)))
            elif status in ("failed", "banned", "expired", "cancelled"):
                del running[tid]
                msg = "%s %s %s" % (status, data.get("error_code", ""), data.get("error_message", ""))
                log("tripo-api: %s FAILED %s" % (job["name"], msg))
                job["failed"](msg.strip())
                failed += 1
            elif time.time() - job["since"] > TASK_TIMEOUT_S:
                del running[tid]
                log("tripo-api: %s timed out (%s); its task id is kept, a rerun resumes it" % (job["name"], status))
                failed += 1
    return {"done": done, "failed": failed, "credits": credits}
