#!/usr/bin/env python3
"""
Mock HTTP server for TinyMUX httpclient module tests.

Endpoints:
  GET  /health               → 200 {"status":"ok"}
  GET  /json/simple          → 200 {"name":"Hero","hp":100,"alive":true}
  GET  /json/nested          → 200 {"vitals":{"hp":42,"mp":7},"items":[{"id":1,"name":"sword"}]}
  GET  /json/array           → 200 [10, 20, 30]
  GET  /echo?x=y             → 200 {"query":"x=y","method":"GET"}
  POST /echo                 → 200 mirrors request body as {"body":"...","ct":"..."}
  GET  /status/404           → 404 {"error":"not found"}
  GET  /status/500           → 500 {"error":"server error"}
  GET  /slow                 → 200 after 35-second delay (exceeds httpclient timeout)
  GET  /large                → 200 32 KB of 'x' (tests output cap)
  POST /auth                 → 200 if Authorization header present, else 401
"""

import argparse
import json
import time
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

# ---------------------------------------------------------------------------

class MockHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        # Suppress default access log; uncomment below for debugging:
        # print(f"[mock_httpd] {fmt % args}")
        pass

    # ---- helpers -----------------------------------------------------------

    def send_json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def read_body(self):
        length = int(self.headers.get("Content-Length", 0))
        return self.rfile.read(length).decode("utf-8", errors="replace") if length else ""

    # ---- routing -----------------------------------------------------------

    def do_GET(self):
        parsed = urlparse(self.path)
        path   = parsed.path

        if path == "/health":
            self.send_json(200, {"status": "ok"})

        elif path == "/json/simple":
            self.send_json(200, {"name": "Hero", "hp": 100, "alive": True})

        elif path == "/json/nested":
            self.send_json(200, {
                "vitals": {"hp": 42, "mp": 7},
                "items": [{"id": 1, "name": "sword"}, {"id": 2, "name": "shield"}]
            })

        elif path == "/json/array":
            self.send_json(200, [10, 20, 30])

        elif path == "/json/unicode":
            self.send_json(200, {"greeting": "Hello \u4e16\u754c"})  # "Hello 世界"

        elif path == "/echo":
            qs = parsed.query
            self.send_json(200, {"query": qs, "method": "GET"})

        elif path.startswith("/status/"):
            code = int(path.split("/")[-1])
            self.send_json(code, {"error": f"status {code}"})

        elif path == "/slow":
            # Sleep longer than httpclient's 30-second timeout
            time.sleep(35)
            self.send_json(200, {"delayed": True})

        elif path == "/large":
            # 32 KB response — exceeds httpclient output cap (8 KB)
            body = ("x" * 32768).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        elif path == "/auth":
            if "Authorization" in self.headers:
                self.send_json(200, {"authenticated": True})
            else:
                self.send_json(401, {"error": "unauthorized"})

        else:
            self.send_json(404, {"error": "not found", "path": path})

    def do_POST(self):
        parsed = urlparse(self.path)
        path   = parsed.path
        body   = self.read_body()
        ct     = self.headers.get("Content-Type", "")

        if path == "/echo":
            self.send_json(200, {"body": body, "ct": ct, "method": "POST"})

        elif path == "/auth":
            if "Authorization" in self.headers:
                self.send_json(200, {"authenticated": True, "body": body})
            else:
                self.send_json(401, {"error": "unauthorized"})

        elif path == "/status/201":
            self.send_json(201, {"created": True, "body": body})

        else:
            self.send_json(404, {"error": "not found", "path": path})


# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Mock HTTP server for MUX tests")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    server = HTTPServer((args.host, args.port), MockHandler)
    print(f"[mock_httpd] Listening on {args.host}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
