"""wow335modding: local server for the browser build.

- / and the build output (WowWeb.js, WowWeb.wasm) with the cross-origin isolation headers threads need.
- /data/... from your own game Data folder, with Range requests (System/File/Web/WebFile.cpp reads MPQs in chunks).
  Directories answer with an X-Wow-Directory header. The data is only served to this machine (127.0.0.1).
- /ws/<host>/<port>: a WebSocket bridged to a TCP port on this machine (authserver 3724, worldserver 8085).

Usage: python serve.py [--port 8000] [--build ../../bin/Win] [--data ../../bin/Win/Data]
"""
import argparse
import base64
import hashlib
import os
import re
import socket
import struct
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ALLOWED_PORTS = {3724, 8085}
WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()

    def log_message(self, format, *args):
        pass

    def resolve(self):
        path = self.path.split('?')[0]
        if path.startswith('/data/') or path == '/data':
            root, rest = self.server.data, path[len('/data'):]
        elif path in ('/', '/index.html'):
            return os.path.join(HERE, 'index.html')
        else:
            root, rest = self.server.build, path
        full = os.path.realpath(os.path.join(root, rest.lstrip('/')))
        if not (full == os.path.realpath(root) or full.startswith(os.path.realpath(root) + os.sep)):
            return None
        return full

    def do_HEAD(self):
        self.serve(False)

    def do_GET(self):
        if self.path.startswith('/ws/'):
            return self.websocket()
        self.serve(True)

    def serve(self, body):
        path = self.resolve()
        if not path or not os.path.exists(path):
            self.send_response(404)
            self.send_header('Content-Length', '0')
            return self.end_headers()
        if os.path.isdir(path):
            # One name per line, directories with a trailing '/' (WebFile::ListDirectory).
            listing = ''.join(e.name + ('/' if e.is_dir() else '') + '\n' for e in os.scandir(path)).encode()
            self.send_response(200)
            self.send_header('X-Wow-Directory', '1')
            self.send_header('Content-Type', 'text/plain; charset=utf-8')
            self.send_header('Content-Length', str(len(listing)))
            self.end_headers()
            if body:
                self.wfile.write(listing)
            return
        size = os.path.getsize(path)
        start, end = 0, size - 1
        match = re.match(r'bytes=(\d+)-(\d*)', self.headers.get('Range', ''))
        if match:
            start = int(match.group(1))
            end = min(int(match.group(2)) if match.group(2) else size - 1, size - 1)
            self.send_response(206)
            self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        else:
            self.send_response(200)
        types = {'.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm'}
        self.send_header('Content-Type', types.get(os.path.splitext(path)[1], 'application/octet-stream'))
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Length', str(end - start + 1))
        self.end_headers()
        if body:
            with open(path, 'rb') as f:
                f.seek(start)
                left = end - start + 1
                while left > 0:
                    chunk = f.read(min(left, 1 << 20))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    left -= len(chunk)

    def websocket(self):
        match = re.match(r'/ws/([^/]+)/(\d+)$', self.path)
        if not match or match.group(1) not in ('127.0.0.1', 'localhost') or int(match.group(2)) not in ALLOWED_PORTS:
            self.send_response(403)
            self.send_header('Content-Length', '0')
            return self.end_headers()
        try:
            upstream = socket.create_connection(('127.0.0.1', int(match.group(2))))
        except OSError:
            self.send_response(502)
            self.send_header('Content-Length', '0')
            return self.end_headers()
        upstream.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        accept = base64.b64encode(hashlib.sha1((self.headers['Sec-WebSocket-Key'] + WS_GUID).encode()).digest()).decode()
        self.send_response(101)
        self.send_header('Upgrade', 'websocket')
        self.send_header('Connection', 'Upgrade')
        self.send_header('Sec-WebSocket-Accept', accept)
        if 'binary' in self.headers.get('Sec-WebSocket-Protocol', ''):
            self.send_header('Sec-WebSocket-Protocol', 'binary')
        self.end_headers()
        self.close_connection = True
        client = self.connection
        lock = threading.Lock()

        def send_frame(opcode, payload):
            header = bytes([0x80 | opcode])
            n = len(payload)
            header += bytes([n]) if n < 126 else (bytes([126]) + struct.pack('>H', n) if n < 65536 else bytes([127]) + struct.pack('>Q', n))
            with lock:
                client.sendall(header + payload)

        def upstream_to_client():
            try:
                while data := upstream.recv(65536):
                    send_frame(2, data)
            except OSError:
                pass
            try:
                send_frame(8, b'')
            except OSError:
                pass

        threading.Thread(target=upstream_to_client, daemon=True).start()
        reader = self.rfile
        try:
            while True:
                head = reader.read(2)
                if len(head) < 2:
                    break
                opcode, n = head[0] & 0x0F, head[1] & 0x7F
                if n == 126:
                    n = struct.unpack('>H', reader.read(2))[0]
                elif n == 127:
                    n = struct.unpack('>Q', reader.read(8))[0]
                mask = reader.read(4) if head[1] & 0x80 else b'\0\0\0\0'
                payload = bytes(b ^ mask[i % 4] for i, b in enumerate(reader.read(n)))
                if opcode == 8:
                    break
                if opcode == 9:
                    send_frame(10, payload)
                elif opcode in (0, 2):
                    upstream.sendall(payload)
        except OSError:
            pass
        upstream.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--build', default=os.path.join(HERE, '..', '..', 'bin', 'Win'))
    parser.add_argument('--data', default=os.path.join(HERE, '..', '..', 'bin', 'Win', 'Data'))
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    server.daemon_threads = True
    server.build, server.data = os.path.abspath(args.build), os.path.abspath(args.data)
    print(f'http://localhost:{args.port}/  (data: {server.data})')
    server.serve_forever()


if __name__ == '__main__':
    main()
