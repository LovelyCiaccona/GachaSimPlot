#!/usr/bin/env python3
import csv
import shutil
import json
import mimetypes
import os
import subprocess
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse


ROOT = Path(__file__).resolve().parents[1]
FRONTEND_DIR = ROOT / "frontend"
RUNS_DIR = ROOT / "data" / "runs"
BIN_PATH = ROOT / "bin" / "gacha_sim.exe"
DELETED_INDEX = RUNS_DIR / ".deleted_runs.json"

RUNS_DIR.mkdir(parents=True, exist_ok=True)

active_lock = threading.Lock()
active_process = None


def now_iso():
    return datetime.now().isoformat(timespec="seconds")


def json_response(handler, status, payload):
    body = json.dumps(payload, ensure_ascii=False, indent=2).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def text_response(handler, status, text, content_type="text/plain; charset=utf-8"):
    body = text.encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", content_type)
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def read_json(path, default=None):
    try:
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return default


def write_json(path, data):
    with path.open("w", encoding="utf-8", newline="") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def load_deleted_runs():
    data = read_json(DELETED_INDEX, [])
    if not isinstance(data, list):
        return []
    return [str(x) for x in data]


def save_deleted_runs(run_ids):
    deduped = sorted(set(str(x) for x in run_ids))
    write_json(DELETED_INDEX, deduped)


def mark_deleted(run_id):
    ids = load_deleted_runs()
    if run_id not in ids:
        ids.append(run_id)
        save_deleted_runs(ids)


def run_dir(run_id):
    return RUNS_DIR / run_id


def status_path(run_id):
    return run_dir(run_id) / "status.json"


def update_status(run_id, **updates):
    status = read_json(status_path(run_id), {})
    status.update(updates)
    write_json(status_path(run_id), status)
    return status


def make_run_id():
    base = datetime.now().strftime("%Y%m%d-%H%M%S")
    candidate = base
    suffix = 1
    while run_dir(candidate).exists():
        suffix += 1
        candidate = f"{base}-{suffix}"
    return candidate


def parse_int(value, default, min_value=0):
    if value is None or value == "":
        return default
    number = int(value)
    if number < min_value:
        raise ValueError(f"value must be >= {min_value}")
    return number


def build_command(params, out_dir):
    simulator = params.get("simulator", "endfield-joint")
    samples = parse_int(params.get("samples"), 1000000, 1)
    threads = parse_int(params.get("threads"), 4, 1)
    target_char = parse_int(params.get("target_char"), 1, 0)
    target_weapon = parse_int(params.get("target_weapon"), 1, 0)
    initial_quota = parse_int(params.get("initial_arsenal_quota", params.get("initial_weapon_points")), 0, 0)
    initial_coral = parse_int(params.get("initial_coral"), 0, 0)
    exchange_enabled = params.get("exchange_enabled", True)
    if isinstance(exchange_enabled, str):
        exchange_enabled = exchange_enabled.lower() in ("1", "true", "on", "yes")
    seed = parse_int(params.get("seed"), 0, 0)

    if simulator not in ("endfield-joint", "wuwa"):
        raise ValueError("unsupported simulator")

    cmd = [
        str(BIN_PATH),
        "--sim",
        simulator,
        "--samples",
        str(samples),
        "--threads",
        str(threads),
        "--target-char",
        str(target_char),
        "--target-weapon",
        str(target_weapon),
        "--out",
        str(out_dir),
    ]
    if simulator == "endfield-joint":
        cmd.extend(["--initial-arsenal-quota", str(initial_quota)])
    else:
        cmd.extend(["--initial-coral", str(initial_coral), "--exchange", "on" if exchange_enabled else "off"])
    if seed:
        cmd.extend(["--seed", str(seed)])

    normalized = {
        "simulator": simulator,
        "samples": samples,
        "threads": threads,
        "target_char": target_char,
        "target_weapon": target_weapon,
        "initial_arsenal_quota": initial_quota,
        "initial_coral": initial_coral,
        "exchange_enabled": bool(exchange_enabled),
        "seed": seed,
    }
    return cmd, normalized


def run_worker(run_id, cmd):
    global active_process
    out_dir = run_dir(run_id)
    stdout_path = out_dir / "stdout.log"
    stderr_path = out_dir / "stderr.log"

    update_status(run_id, status="running", started_at=now_iso(), command=cmd)
    return_code = None
    try:
        with stdout_path.open("w", encoding="utf-8", newline="") as stdout, stderr_path.open(
            "w", encoding="utf-8", newline=""
        ) as stderr:
            process = subprocess.Popen(
                cmd,
                cwd=str(ROOT),
                stdout=stdout,
                stderr=stderr,
                text=True,
            )
            with active_lock:
                active_process = process
            return_code = process.wait()

        if return_code == 0:
            update_status(run_id, status="completed", finished_at=now_iso(), return_code=return_code)
        else:
            update_status(run_id, status="failed", finished_at=now_iso(), return_code=return_code)
    except Exception as exc:
        update_status(run_id, status="failed", finished_at=now_iso(), error=str(exc), return_code=return_code)
    finally:
        with active_lock:
            active_process = None


def create_run(params):
    if not BIN_PATH.exists():
        raise RuntimeError(f"Simulator not built: {BIN_PATH}")

    with active_lock:
        if active_process is not None and active_process.poll() is None:
            raise RuntimeError("Another simulation is already running")

    run_id = make_run_id()
    out_dir = run_dir(run_id)
    out_dir.mkdir(parents=True, exist_ok=False)
    cmd, normalized = build_command(params, out_dir)

    request = {
        "id": run_id,
        "simulator": normalized["simulator"],
        "params": normalized,
        "created_at": now_iso(),
    }
    write_json(out_dir / "request.json", request)
    write_json(
        out_dir / "status.json",
        {
            "id": run_id,
            "status": "queued",
            "created_at": request["created_at"],
            "finished_at": None,
            "return_code": None,
        },
    )

    thread = threading.Thread(target=run_worker, args=(run_id, cmd), daemon=True)
    thread.start()
    return read_run(run_id)


def read_run(run_id):
    folder = run_dir(run_id)
    if not folder.exists():
        return None
    request = read_json(folder / "request.json", {})
    status = read_json(folder / "status.json", {})
    summary_exists = (folder / "summary.json").exists()
    return {
        "id": run_id,
        "request": request,
        "status": status,
        "has_summary": summary_exists,
        "files": sorted(p.name for p in folder.iterdir() if p.is_file()),
    }


def list_runs():
    deleted = set(load_deleted_runs())
    runs = []
    for folder in sorted(RUNS_DIR.iterdir(), reverse=True):
        if folder.name in deleted:
            continue
        if folder.is_dir() and (folder / "status.json").exists():
            runs.append(read_run(folder.name))
    return runs


def read_csv_records(path):
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        records = []
        for row in reader:
            converted = {}
            for key, value in row.items():
                if value is None:
                    converted[key] = value
                    continue
                try:
                    if "." in value:
                        converted[key] = float(value)
                    else:
                        converted[key] = int(value)
                except ValueError:
                    converted[key] = value
            records.append(converted)
        return records


class Handler(BaseHTTPRequestHandler):
    server_version = "GachaSimHTTP/0.1"

    def log_message(self, fmt, *args):
        print("%s - %s" % (self.address_string(), fmt % args))

    def do_GET(self):
        parsed = urlparse(self.path)
        path = unquote(parsed.path)

        if path == "/api/simulators":
            return json_response(
                self,
                200,
                [
                    {
                        "id": "endfield-joint",
                        "name": "终末地 角色+武器联合模拟",
                        "fields": [
                            {"id": "samples", "label": "样本数", "type": "number", "default": 1000000, "min": 1},
                            {"id": "target_char", "label": "目标限定角色", "type": "number", "default": 1, "min": 0},
                            {"id": "target_weapon", "label": "目标限定武器", "type": "number", "default": 1, "min": 0},
                            {
                                "id": "initial_arsenal_quota",
                                "label": "初始武库配额",
                                "type": "number",
                                "default": 0,
                                "min": 0,
                            },
                            {"id": "threads", "label": "线程数", "type": "number", "default": 4, "min": 1},
                        ],
                    },
                    {
                        "id": "wuwa",
                        "name": "鸣潮 角色+武器联合模拟",
                        "fields": [
                            {"id": "samples", "label": "样本数", "type": "number", "default": 1000000, "min": 1},
                            {"id": "target_char", "label": "目标限定角色", "type": "number", "default": 1, "min": 0},
                            {"id": "target_weapon", "label": "目标五星武器", "type": "number", "default": 1, "min": 0},
                            {"id": "initial_coral", "label": "初始珊瑚", "type": "number", "default": 0, "min": 0},
                            {"id": "exchange_enabled", "label": "允许珊瑚兑换", "type": "boolean", "default": True},
                            {"id": "threads", "label": "线程数", "type": "number", "default": 4, "min": 1},
                        ],
                    }
                ],
            )

        if path == "/api/runs":
            return json_response(self, 200, list_runs())

        if path.startswith("/api/runs/"):
            parts = [p for p in path.split("/") if p]
            if len(parts) < 3:
                return json_response(self, 404, {"error": "not found"})
            run_id = parts[2]
            folder = run_dir(run_id)
            if not folder.exists():
                return json_response(self, 404, {"error": "run not found"})

            if len(parts) == 3:
                return json_response(self, 200, read_run(run_id))

            endpoint = parts[3]
            if endpoint == "summary":
                summary = read_json(folder / "summary.json")
                if summary is None:
                    return json_response(self, 404, {"error": "summary not found"})
                return json_response(self, 200, summary)
            if endpoint == "distribution":
                target = folder / "distribution.csv"
                if not target.exists():
                    return json_response(self, 404, {"error": "distribution not found"})
                return json_response(self, 200, read_csv_records(target))
            if endpoint == "percentiles":
                target = folder / "percentiles.csv"
                if not target.exists():
                    return json_response(self, 404, {"error": "percentiles not found"})
                return json_response(self, 200, read_csv_records(target))
            if endpoint == "logs":
                stdout = (folder / "stdout.log").read_text(encoding="utf-8", errors="replace") if (folder / "stdout.log").exists() else ""
                stderr = (folder / "stderr.log").read_text(encoding="utf-8", errors="replace") if (folder / "stderr.log").exists() else ""
                return json_response(self, 200, {"stdout": stdout, "stderr": stderr})
            if endpoint == "files" and len(parts) >= 5:
                filename = parts[4]
                if "/" in filename or "\\" in filename or filename.startswith("."):
                    return json_response(self, 400, {"error": "invalid filename"})
                target = folder / filename
                if not target.exists() or not target.is_file():
                    return json_response(self, 404, {"error": "file not found"})
                content_type = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
                body = target.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
                self.end_headers()
                self.wfile.write(body)
                return

            return json_response(self, 404, {"error": "not found"})

        return self.serve_static(path)

    def do_POST(self):
        parsed = urlparse(self.path)
        path = unquote(parsed.path)
        if path != "/api/runs":
            return json_response(self, 404, {"error": "not found"})

        length = int(self.headers.get("Content-Length", "0"))
        try:
            payload = json.loads(self.rfile.read(length).decode("utf-8") or "{}")
            run = create_run(payload)
            return json_response(self, 201, run)
        except Exception as exc:
            return json_response(self, 400, {"error": str(exc)})

    def do_DELETE(self):
        parsed = urlparse(self.path)
        path = unquote(parsed.path)
        if not path.startswith("/api/runs/"):
            return json_response(self, 404, {"error": "not found"})

        parts = [p for p in path.split("/") if p]
        if len(parts) != 3:
            return json_response(self, 404, {"error": "not found"})

        run_id = parts[2]
        folder = run_dir(run_id)
        if not folder.exists():
            return json_response(self, 404, {"error": "run not found"})

        status = read_json(folder / "status.json", {}) or {}
        if status.get("status") in ("queued", "running"):
            return json_response(self, 409, {"error": "run is active, cannot delete now"})

        with active_lock:
            if active_process is not None and active_process.poll() is None:
                cmd = status.get("command") or []
                if isinstance(cmd, list) and any(run_id in str(p) for p in cmd):
                    return json_response(self, 409, {"error": "run is active, cannot delete now"})

        mark_deleted(run_id)

        last_exc = None
        for _ in range(5):
            try:
                shutil.rmtree(folder)
                return json_response(self, 200, {"ok": True, "deleted": run_id})
            except Exception as exc:
                last_exc = exc
                time.sleep(0.2)
        # Logical delete succeeded even if physical cleanup is blocked by transient file lock.
        return json_response(self, 200, {"ok": True, "deleted": run_id, "warning": str(last_exc)})

    def serve_static(self, path):
        if path == "/":
            target = FRONTEND_DIR / "index.html"
        else:
            rel = path.lstrip("/")
            target = (FRONTEND_DIR / rel).resolve()
            if FRONTEND_DIR.resolve() not in target.parents and target != FRONTEND_DIR.resolve():
                return json_response(self, 403, {"error": "forbidden"})

        if not target.exists() or not target.is_file():
            return json_response(self, 404, {"error": "not found"})

        content_type = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
        body = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    host = os.environ.get("GACHA_HOST", "127.0.0.1")
    port = int(os.environ.get("GACHA_PORT", "8765"))
    httpd = ThreadingHTTPServer((host, port), Handler)
    print(f"Serving on http://{host}:{port}")
    print(f"Project root: {ROOT}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("Stopping server")


if __name__ == "__main__":
    main()
