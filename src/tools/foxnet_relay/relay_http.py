"""Small HTTP adapter for the standalone relay; no third-party packages."""

import json
import logging
import re
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from types import SimpleNamespace

_current = threading.local()


class RequestProxy:
    def __getattr__(self, name):
        return getattr(_current.request, name)


request = RequestProxy()


def jsonify(**values):
    return values


class RelayApp:
    def __init__(self, name):
        self.config = {}
        self.logger = logging.getLogger(name)
        self.routes = []

    def route(self, pattern, methods=None):
        method_set = set(methods or ["GET"])
        expression = re.escape(pattern)
        expression = re.sub(r"<([a-zA-Z_][a-zA-Z_0-9]*)>",
                            r"(?P<\1>[^/]+)", expression)
        compiled = re.compile("^" + expression + "$")

        def decorate(handler):
            self.routes.append((compiled, method_set, handler))
            return handler
        return decorate

    def serve(self, host, port):
        app = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, format, *args):
                # BaseHTTPRequestHandler logs before sending response headers.
                # A paused Windows console can otherwise stall every request.
                pass

            def do_GET(self):
                self.handle_api()

            def do_POST(self):
                self.handle_api()

            def handle_api(self):
                self.connection.settimeout(5)
                length_text = self.headers.get("Content-Length", "0")
                try:
                    length = int(length_text)
                except ValueError:
                    length = -1
                if length < 0 or length > app.config.get("MAX_CONTENT_LENGTH", 262144):
                    self.send_json({"error": "invalid request size"}, 413)
                    return
                body = self.rfile.read(length)
                try:
                    parsed = json.loads(body) if body else None
                except (UnicodeError, ValueError):
                    parsed = None
                _current.request = SimpleNamespace(
                    headers=self.headers,
                    remote_addr=self.client_address[0],
                    get_json=lambda silent=True: parsed,
                )
                try:
                    path = self.path.split("?", 1)[0]
                    for expression, methods, handler in app.routes:
                        match = expression.fullmatch(path)
                        if match and self.command in methods:
                            result = handler(**match.groupdict())
                            if isinstance(result, tuple):
                                payload, status = result
                            else:
                                payload, status = result, 200
                            self.send_json(payload, status)
                            return
                    self.send_json({"error": "not found"}, 404)
                except Exception:
                    app.logger.exception("request failed")
                    self.send_json({"error": "internal error"}, 500)
                finally:
                    del _current.request

            def send_json(self, payload, status):
                data = json.dumps(payload, ensure_ascii=False,
                                  separators=(",", ":")).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        server_class = ThreadingHTTPServer
        if ":" in host:
            class IPv6Server(ThreadingHTTPServer):
                address_family = socket.AF_INET6
            server_class = IPv6Server
        server = server_class((host, port), Handler)
        server.daemon_threads = True
        self.logger.info("listening on %s:%d", host, port)
        server.serve_forever(poll_interval=0.2)

