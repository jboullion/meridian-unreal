"""
Talk to the user's open Blender through the "MCP for Blender" add-on (blender_mcp.py, a JSON socket on
localhost:9876), to collect models the Tripo DCC Bridge sends into it from Tripo Studio.

    python tools/aigen/blender_link.py ping
    python tools/aigen/blender_link.py snapshot                  # names of the objects in the scene now
    python tools/aigen/blender_link.py grab OUT.glb [--wait 120] [--task ID]  # export what arrived since the snapshot

Flow (aigen.py <asset> bridge <variant> wraps it): snapshot, click "Send To Blender" in Studio's
Export panel, grab. grab waits for new mesh objects to appear and stop changing, exports them (with
their parent empties) as one GLB with embedded textures, then deletes them so the scene stays clean.
The bridge logs every model it imports with its Tripo task id (%TEMP%/tripo3d_blender_bridge.log);
grab reads the lines written since the snapshot and returns the task id, and with --task refuses a
model from a different task (a stale or unexpected send).

The bridge's browser connection drops when the Studio page reloads (and sometimes on its own): before
each send, open DCC Bridge in Studio and make sure Blender is toggled on ("Connected").
"""
import json
import os
import re
import socket
import sys
import tempfile
import time
from pathlib import Path

HOST, PORT = "127.0.0.1", 9876
STATE = Path(__file__).resolve().parents[2] / "build" / "aigen" / "blender_snapshot.json"
BRIDGE_LOG = Path(tempfile.gettempdir()) / "tripo3d_blender_bridge.log"


def call(command: dict, timeout: float = 120):
    s = socket.create_connection((HOST, PORT), timeout=timeout)
    try:
        s.sendall(json.dumps(command).encode())
        buf = b""
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
            try:
                return json.loads(buf)
            except ValueError:
                continue
        raise RuntimeError("Blender closed the connection")
    finally:
        s.close()


def run(code: str, timeout: float = 120):
    """Run Python in Blender; the code sets `out` to a JSON-able value, which is returned."""
    wrapped = "import bpy, json\nout = None\n" + code + "\nprint('@@OUT@@' + json.dumps(out))"
    r = call({"type": "execute_code", "params": {"code": wrapped}}, timeout)
    if r.get("status") != "success":
        raise RuntimeError("Blender: %s" % r.get("message", r))
    text = r["result"].get("result", "")
    marker = text.rfind("@@OUT@@")
    return json.loads(text[marker + 7:].strip().splitlines()[0]) if marker >= 0 else None


def names() -> list:
    return run("out = [o.name for o in bpy.data.objects]")


def snapshot() -> list:
    n = names()
    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps({"names": n, "log_offset": BRIDGE_LOG.stat().st_size if BRIDGE_LOG.exists() else 0}))
    return n


def bridge_imports(offset: int) -> list:
    """[(task id, root object name)] the bridge imported since `offset` bytes into its log. The name is
    the one the root actually got in Blender ("Renamed imported root: ... -> 'x.001'"): Tripo reuses
    model names ("alien creature 3d model"), so a second arrival with the same name gets a suffix."""
    if not BRIDGE_LOG.exists():
        return []
    with open(BRIDGE_LOG, "rb") as f:
        f.seek(offset)
        text = f.read().decode("utf-8", "replace")
    out, name = [], None
    for line in text.splitlines():
        m = re.search(r"Received complete model name: (.*)$", line)
        if m:
            name = m.group(1).strip()
        m = re.search(r"Renamed imported root: '.*?' -> '(.*?)' \(requested", line)
        if m:
            name = m.group(1)
        m = re.search(r"Successfully imported (\S+)", line)
        if m:
            out.append((m.group(1), name))
    return out


GRAB = r'''
before = set(%(before)s)
new = [o for o in bpy.data.objects if o.name not in before]
meshes = [o for o in new if o.type == "MESH"]
if meshes and %(export)s:
    bpy.ops.object.select_all(action="DESELECT")
    for o in new:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.export_scene.gltf(filepath=%(path)r, export_format="GLB", use_selection=True,
                              export_materials="EXPORT", export_image_format="AUTO", export_yup=True)
    stats = {"objects": [o.name for o in new], "tris": sum(len(p.vertices) - 2 for o in meshes for p in o.data.polygons)}
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.images):
        for d in list(block):
            if d.users == 0:
                block.remove(d)
    out = stats
else:
    out = {"pending": [o.name for o in new], "verts": sum(len(o.data.vertices) for o in meshes)}
'''


def grab(path: Path, wait: float = 120, task: str = None) -> dict:
    """Export the objects that arrived since snapshot() to `path` once they've settled."""
    state = json.loads(STATE.read_text()) if STATE.exists() else {}
    before, offset = state.get("names", []), state.get("log_offset", 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    deadline, last = time.time() + wait, None
    while time.time() < deadline:
        cur = run(GRAB % {"before": before, "export": False, "path": str(path)})
        imports = bridge_imports(offset)
        if cur["verts"] and cur == last and (imports or not BRIDGE_LOG.exists()):  # arrived, unchanged for one poll
            if task and not any(t.startswith(task) for t, _ in imports):
                raise SystemExit("Blender received %s, not task %s; nothing exported" % (imports, task))
            out = run(GRAB % {"before": before, "export": True, "path": str(path)})
            out["tripo_task"], out["tripo_name"] = imports[-1] if imports else (None, None)
            return out
        last = cur
        time.sleep(2)
    raise SystemExit("nothing arrived in Blender within %d s: is DCC Bridge -> Blender toggled on in Studio?" % wait)


EXPORT_ROOT = r'''
name, path = %(name)r, %(path)r
roots = [o for o in bpy.data.objects if o.parent is None and o.name == name]
if roots:
    root = roots[0]
    objs = [root] + list(root.children_recursive)
    meshes = [o for o in objs if o.type == "MESH"]
    out = {"root": root.name, "tris": sum(len(p.vertices) - 2 for o in meshes for p in o.data.polygons)}
    if path:
        bpy.ops.object.select_all(action="DESELECT")
        for o in objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = root
        bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True,
                                  export_materials="EXPORT", export_image_format="AUTO", export_yup=True)
    for o in objs:
        bpy.data.objects.remove(o, do_unlink=True)
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.images):
        for d in list(block):
            if d.users == 0:
                block.remove(d)
'''


def collect(targets: dict, timeout: float = 3600, log=print) -> dict:
    """Export every model the bridge imports whose Tripo task id is a key of `targets` (task id ->
    GLB path), in whatever order they arrive: each import's root object is found by the exact name the
    bridge log says it got. A resent model that was already collected is deleted from the scene.
    Returns {task id: stats}; stops when all have arrived or at `timeout`."""
    offset = BRIDGE_LOG.stat().st_size if BRIDGE_LOG.exists() else 0
    got, seen, tries, deadline = {}, 0, 0, time.time() + timeout
    while len(got) < len(targets) and time.time() < deadline:
        imports = bridge_imports(offset)
        for task, name in imports[seen:]:
            if task not in targets:
                seen += 1
                continue
            path = "" if task in got else str(Path(targets[task]))
            if path:
                Path(path).parent.mkdir(parents=True, exist_ok=True)
            out = run(EXPORT_ROOT % {"name": name, "path": path})
            if not out:
                tries += 1
                if tries < 15:
                    break  # the root isn't in the scene yet: retry this entry next poll
                log("skipped %s: no root named %r in the scene" % (task[:8], name))
            seen, tries = seen + 1, 0
            if not out:
                continue
            if path:
                got[task] = out
                log("collected %s (%s, %d tris) -> %s  [%d/%d]" % (task[:8], name, out["tris"], path, len(got), len(targets)))
            else:
                log("dropped a resend of %s (%s)" % (task[:8], name))
        time.sleep(2)
    return got


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "ping"
    if cmd == "ping":
        print(call({"type": "ping"}))
    elif cmd == "snapshot":
        print(snapshot())
    elif cmd == "grab":
        w = float(sys.argv[sys.argv.index("--wait") + 1]) if "--wait" in sys.argv else 120
        t = sys.argv[sys.argv.index("--task") + 1] if "--task" in sys.argv else None
        print(json.dumps(grab(Path(sys.argv[2]).resolve(), w, t)))
