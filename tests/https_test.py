"""Exercise the real C++ HTTPS client against a local TLS server. No cloud calls."""
import base64
import http.server
import json
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
import time

probe, fixture_tool = sys.argv[1:]
requests = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert self.headers["Authorization"] == "Bearer PRIVATE-TEST-KEY"
        assert data["stream"] is False
        encoded = data["messages"][0]["content"][1]["image_url"]["url"]
        assert encoded.startswith("data:image/jpeg;base64,")
        jpeg = base64.b64decode(encoded.split(",", 1)[1], validate=True)
        assert jpeg[:2] == b"\xff\xd8"
        requests.append(self.path)
        code = {"/unauthorized": 401, "/limited": 429, "/redirect": 307}.get(self.path, 200)
        if self.path == "/slow":
            time.sleep(2.5)
        content = {"source_text": "Give up?\nNever!", "translation": "放弃？\n绝不！"}
        if self.path == "/no-text":
            content = {"source_text": None, "translation": None}
        body = json.dumps({"choices": [{"message": {"content": json.dumps(content)}}]}).encode()
        if self.path == "/malformed":
            body = b'{"choices": "incorrect", "secret": "PRIVATE-TEST-KEY"}'
        if self.path == "/oversized":
            body = b"x" * 200_000
        self.send_response(code)
        if self.path == "/redirect":
            self.send_header("Location", "/ok")
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass


with tempfile.TemporaryDirectory(prefix="gamelex-https-") as folder:
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
    subprocess.run([fixture_tool, "--fixture", str(root / "source.jpg")], check=True)

    def run(path, expected, *, trusted=True, timeout=5, cancel=None):
        config = {"schema_version": 1, "endpoint": f"https://localhost:{server.server_port}{path}",
                  "api_key": "PRIVATE-TEST-KEY", "model": "test-model", "timeout_seconds": timeout,
                  "ca_bundle": str(root / "ca.pem") if trusted else "",
                  "probe_crop": {"x": .625, "y": .25, "width": .25, "height": .5}}
        (root / "config.json").write_text(json.dumps(config))
        args = [probe, str(root / "config.json"), str(root / "source.jpg")]
        if cancel is not None:
            args.append(str(cancel))
        p = subprocess.run(args, capture_output=True, text=True, timeout=10)
        assert "PRIVATE-TEST-KEY" not in p.stdout + p.stderr
        result = json.loads(p.stdout)
        assert result["code"] == expected, result
        assert p.returncode == (0 if expected == "" else 1), (p.returncode, p.stderr)
        assert result["crop_width"] == 20 and result["crop_height"] == 30
        return result

    success = run("/ok", "")
    assert success["source_text"] == "Give up?\nNever!"
    assert success["translation"] == "放弃？\n绝不！"
    run("/no-text", "no_text")
    run("/unauthorized", "auth_failed")
    run("/limited", "rate_limited")
    run("/malformed", "invalid_response")
    run("/oversized", "response_too_large")
    before = len(requests)
    run("/redirect", "http_failed")
    assert len(requests) == before + 1  # No redirects, retries or duplicate charges.
    run("/slow", "timeout", timeout=1)
    run("/slow", "cancelled", cancel=100)
    before = len(requests)
    run("/ok", "tls_failed", trusted=False)
    assert len(requests) == before  # Credentials never reach an untrusted peer.
    server.shutdown()
print("PASS 10 real HTTPS scenarios")
