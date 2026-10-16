"""PC Remote v0.1 Windows host. Python 3.10+, standard library only."""
import hmac
import json
import os
import secrets
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HOST = os.environ.get("PC_REMOTE_HOST", "127.0.0.1")
PORT = int(os.environ.get("PC_REMOTE_PORT", "8765"))
TOKEN = os.environ.get("PC_REMOTE_TOKEN", "")
CONFIG = Path(__file__).with_name("tasks.json")
MAX_BODY = 2048

def load_tasks():
    data = json.loads(CONFIG.read_text(encoding="utf-8"))
    tasks = data.get("tasks", {})
    if not isinstance(tasks, dict):
        raise ValueError("tasks must be an object")
    for name, spec in tasks.items():
        if not isinstance(spec, dict) or not isinstance(spec.get("argv"), list):
            raise ValueError(f"invalid task: {name}")
        if not spec["argv"] or not all(isinstance(x, str) and x for x in spec["argv"]):
            raise ValueError(f"invalid argv: {name}")
        if spec.get("cwd") is not None and not Path(spec["cwd"]).is_dir():
            raise ValueError(f"cwd does not exist: {name}")
    return tasks

TASKS = load_tasks()
JOBS = {}
LOCK = threading.Lock()

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print("%s - %s" % (self.address_string(), fmt % args))

    def send_json(self, status, data):
        payload = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def authorized(self):
        supplied = self.headers.get("X-PC-Remote-Token", "")
        return bool(TOKEN) and hmac.compare_digest(supplied, TOKEN)

    def do_GET(self):
        if not self.authorized():
            return self.send_json(401, {"ok": False, "error": "unauthorized"})
        if self.path == "/status":
            with LOCK:
                jobs = {key: {"running": proc.poll() is None, "returncode": proc.poll()}
                        for key, proc in JOBS.items()}
            return self.send_json(200, {"ok": True, "service": "pc-remote-v0.1", "jobs": jobs})
        if self.path == "/tasks":
            return self.send_json(200, {"ok": True, "tasks": list(TASKS)})
        self.send_json(404, {"ok": False, "error": "not_found"})

    def do_POST(self):
        if not self.authorized():
            return self.send_json(401, {"ok": False, "error": "unauthorized"})
        if self.path != "/run":
            return self.send_json(404, {"ok": False, "error": "not_found"})
        try:
            length = int(self.headers.get("Content-Length", "-1"))
            if not 0 < length <= MAX_BODY:
                return self.send_json(413, {"ok": False, "error": "invalid_size"})
            data = json.loads(self.rfile.read(length))
            name = data.get("task") if isinstance(data, dict) else None
            if not isinstance(name, str) or name not in TASKS:
                return self.send_json(400, {"ok": False, "error": "unknown_task"})
            spec = TASKS[name]
            with LOCK:
                old = JOBS.get(name)
                if old is not None and old.poll() is None:
                    return self.send_json(409, {"ok": False, "error": "already_running"})
                kwargs = {"cwd": spec.get("cwd"), "stdin": subprocess.DEVNULL,
                          "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL,
                          "shell": False}
                if os.name == "nt":
                    kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
                proc = subprocess.Popen(spec["argv"], **kwargs)
                JOBS[name] = proc
            self.send_json(200, {"ok": True, "task": name, "pid": proc.pid})
        except (ValueError, json.JSONDecodeError):
            self.send_json(400, {"ok": False, "error": "invalid_json"})
        except OSError as exc:
            self.send_json(500, {"ok": False, "error": type(exc).__name__})

if __name__ == "__main__":
    if not TOKEN or len(TOKEN) < 24:
        raise SystemExit("Set PC_REMOTE_TOKEN to a random string of at least 24 characters.")
    print(f"PC Remote listening on {HOST}:{PORT}; tasks: {', '.join(TASKS)}")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
