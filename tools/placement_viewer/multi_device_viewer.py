"""Serve registered placement runs with isolated caches and device-bound geometry."""
import argparse
import json
import re
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse
from device_geometry import identity
from placement_viewer import make_handler


def catalog_handler(catalog_path):
    catalog = json.loads(catalog_path.read_text())
    if catalog.get("schema") != 1:
        raise ValueError("Unknown catalog schema")
    entries = {}
    public = []
    for entry in catalog["runs"]:
        key = entry["id"]
        if not re.fullmatch(r"[a-z0-9-]+", key) or key in entries:
            raise ValueError("Invalid/duplicate run ID")
        config = Path(entry["config"])
        traces = Path(entry["traces"])
        if not config.is_absolute(): config = catalog_path.parent / config
        if not traces.is_absolute(): traces = catalog_path.parent / traces
        ident = identity(json.loads(config.read_text()))
        if entry["device"] != ident["device"]:
            raise ValueError("Catalog device differs from actual input")
        entries[key] = dict(config=config, traces=traces, handler=None, lock=threading.Lock())
        public.append(dict(id=key, label=entry["label"], scope=entry["scope"], **ident))

    class Handler(BaseHTTPRequestHandler):
        def log_request(self, code="-", size="-"):
            if str(code).startswith(("4", "5")): super().log_request(code, size)

        def send_data(self, body, kind):
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def delegate(self, method):
            # Reuse a per-run handler's APIs and closures, but keep the request,
            # socket and response lifecycle on this one HTTP server.
            match = re.match(r"^/view/([a-z0-9-]+)(/.*)$", self.path)
            if not match or match[1] not in entries:
                self.send_error(404); return
            entry = entries[match[1]]
            try:
                with entry["lock"]:
                    if entry["handler"] is None:
                        entry["handler"] = make_handler(entry["traces"], entry["config"])
                request = object.__new__(entry["handler"])
                request.__dict__.update(self.__dict__)
                request.path = match[2]
                getattr(request, method)()
                self.close_connection = request.close_connection
            except (OSError, ValueError, KeyError) as error:
                print("RUN_LOAD_ERROR", match[1], str(error), flush=True)
                self.send_error(409, "Run data or device geometry is unavailable")

        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/":
                self.send_data(Path(__file__).with_name("catalog.html").read_bytes(), "text/html; charset=utf-8")
            elif path == "/api/catalog":
                self.send_data(json.dumps({"runs": public}, ensure_ascii=False).encode(), "application/json; charset=utf-8")
            else:
                self.delegate("do_GET")

        def do_POST(self):
            self.delegate("do_POST")

    return Handler


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--port", type=int, default=8772)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), catalog_handler(args.catalog.resolve()))
    print(f"AMF multi-device viewer http://127.0.0.1:{args.port}/", flush=True)
    server.serve_forever()
