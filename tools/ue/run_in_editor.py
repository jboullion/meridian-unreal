"""
Run a tools/ue script inside the Unreal Editor that is already open on this project, through the
Python plugin's remote execution (enabled in Config/DefaultEngine.ini; local machine only). No
editor startup, no fight over locked files, and the open editor shows the result straight away.

    python tools/ue/run_in_editor.py build_world.py [script args...]
    python tools/ue/run_in_editor.py --list            # editors that answer

build_world.ps1 calls this first and starts a headless editor only when nothing answers.
Exit codes: 0 done, 1 the script failed, 3 no editor open on this project.

Environment variables the scripts read (MR_MOOD, MR_DISPLACEMENT_RANGE_CM) are passed through.
"""
import argparse
import json
import os
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PROJECT_DIR = os.path.join(REPO, "game", "MeridianRemastered")
DEFAULT_ENGINE = r"G:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
PASS_ENV = ("MR_MOOD", "MR_DISPLACEMENT_RANGE_CM")
# lines worth echoing from the editor's output (everything goes to its log as usual)
ECHO = ("[build_world]", "[build_cache]", "[environment_materials]", "[zone_mood]", "Error", "Traceback")
NO_EDITOR = 3


def load_remote_execution(engine_exe):
    engine_dir = os.path.abspath(os.path.join(os.path.dirname(engine_exe), "..", ".."))
    sys.path.insert(0, os.path.join(engine_dir, "Plugins", "Experimental", "PythonScriptPlugin", "Content", "Python"))
    import remote_execution

    def receive(self, expected_type):
        # the engine's version stops at the first short read, which cuts long output in half
        data = b""
        while True:
            part = self._command_channel_socket.recv(1 << 16)
            if not part:
                break
            data += part
            try:
                json.loads(data.decode("utf-8"))
                break
            except ValueError:
                continue
        message = remote_execution._RemoteExecutionMessage(None, None)
        if data and message.from_json_bytes(data) and message.passes_receive_filter(self._node_id) \
                and message.type_ == expected_type:
            return message
        raise RuntimeError("the editor sent an unreadable response")

    remote_execution._RemoteExecutionCommandConnection._receive_message = receive
    return remote_execution


def same_dir(a, b):
    return os.path.normcase(os.path.abspath(a or "")).rstrip("\\/") == os.path.normcase(os.path.abspath(b)).rstrip("\\/")


def find_editors(rex, wait_s):
    remote = rex.RemoteExecution()
    remote.start()
    deadline = time.time() + wait_s
    nodes = []
    while time.time() < deadline:
        nodes = [n for n in remote.remote_nodes if same_dir(n.get("project_root"), PROJECT_DIR)]
        if nodes:
            time.sleep(0.3)  # let a second editor answer too
            nodes = [n for n in remote.remote_nodes if same_dir(n.get("project_root"), PROJECT_DIR)]
            break
        time.sleep(0.2)
    return remote, nodes


def interactive_editor(rex, remote, nodes):
    """The first node that is a normal editor session (not -game, a server, or a headless build)."""
    for node in nodes:
        remote.open_command_connection(node["node_id"])
        r = remote.run_command("__import__('unreal').SystemLibrary.get_command_line()",
                               exec_mode=rex.MODE_EVAL_STATEMENT)
        cmdline = (r.get("result") or "").lower()
        if r.get("success") and not any(f in cmdline for f in ("-game", "-server", "-executepythonscript", "-run=")):
            return node
        remote.close_command_connection()
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script", nargs="?", help="script in tools/ue (or a path)")
    ap.add_argument("args", nargs=argparse.REMAINDER)
    ap.add_argument("--engine", default=DEFAULT_ENGINE, help="any exe under the engine's Binaries/Win64")
    ap.add_argument("--wait", type=float, default=2.0, help="seconds to wait for editors to answer")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()

    rex = load_remote_execution(a.engine)
    remote, nodes = find_editors(rex, a.wait)
    try:
        if a.list:
            for n in nodes:
                print("%s  %s  %s" % (n["node_id"], n.get("machine"), n.get("project_root")))
            return 0 if nodes else NO_EDITOR
        node = interactive_editor(rex, remote, nodes) if nodes else None
        if not node:
            print("no editor open on %s (or remote execution is off in it)" % PROJECT_DIR)
            return NO_EDITOR

        script = a.script if os.path.isabs(a.script) else os.path.join(REPO, "tools", "ue", a.script)
        env = {k: os.environ.get(k) for k in PASS_ENV}
        command = "\n".join([
            "import os, sys",
            "for _k, _v in %r.items():" % env,
            "    os.environ.pop(_k, None) if _v is None else os.environ.__setitem__(_k, _v)",
            "sys.argv = %r" % ([script] + a.args),
            "exec(compile(open(%r, encoding='utf-8').read(), %r, 'exec'), {'__name__': '__main__', '__file__': %r})"
            % (script, script, script),
        ])
        print("running %s in the open editor (%s)..." % (os.path.basename(script), node.get("machine")))
        start = time.time()
        r = remote.run_command(command, unattended=True, exec_mode=rex.MODE_EXEC_FILE)
        for entry in r.get("output") or []:
            text = entry.get("output", "").rstrip()
            if entry.get("type") == "Error" or any(e in text for e in ECHO):
                print(text)
        if not r.get("success"):
            print(r.get("result") or "")
            print("FAIL after %.0f s (full output in the editor's Output Log)" % (time.time() - start))
            return 1
        print("OK in %.0f s" % (time.time() - start))
        return 0
    finally:
        remote.stop()


if __name__ == "__main__":
    sys.exit(main())
