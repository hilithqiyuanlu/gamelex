"""Same streaming transport as Switch; test repeated uploads without cloud calls."""
import base64
import http.server
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import parse_qs

requests = []
errors = []
image = bytes((i * 73 + 255) % 256 for i in range(512 * 1024))
query = "PRIVATE-TEST-KEY 中文\nA+B & C=/~"


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        try:
            assert self.headers.get("Transfer-Encoding") is None
            assert self.headers["Content-Type"] == "application/x-www-form-urlencoded"
            body = self.rfile.read(int(self.headers["Content-Length"]))
            fields = parse_qs(body.decode("ascii"), strict_parsing=True)
            assert set(fields) == {"image", "q", "language_type"}
            assert base64.b64decode(fields["image"][0], validate=True) == image
            assert fields["q"] == [query]
            assert fields["language_type"] == ["ENG"]
            requests.append(self.path)
            if self.path == "/slow":
                time.sleep(2.5)
            code = {"/unauthorized": 401, "/server-error": 500, "/redirect": 307}.get(self.path, 200)
            body = b"x" * 300_000 if self.path == "/oversized" else b"OK"
            self.send_response(code)
            if code == 307:
                self.send_header("Location", "/ok")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass
        except Exception as error:
            errors.append(repr(error))


with tempfile.TemporaryDirectory(prefix="gamelex-form-") as folder:
    root = Path(folder)
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                    "-keyout", str(root / "key.pem"), "-out", str(root / "ca.pem"),
                    "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(root / "ca.pem", root / "key.pem")
    server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    result = subprocess.run([sys.argv[1], f"https://localhost:{server.server_port}", str(root / "ca.pem")],
                            text=True, capture_output=True, timeout=35)
    server.shutdown()
    assert query not in result.stdout + result.stderr
    assert result.returncode == 0, result.stdout + result.stderr
    assert not errors, errors
    assert requests[:50] == ["/ok"] * 50
    assert len(requests) == 64, (len(requests), requests[50:])
    assert requests.count("/redirect") == 1
    print(result.stdout.strip())
