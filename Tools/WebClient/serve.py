"""wow335modding: local server for the browser build.

- / and the build output (WowWeb.js, WowWeb.wasm) with the cross-origin isolation headers threads need.
- /data/... from your own game Data folder, with Range requests (System/File/Web/WebFile.cpp reads MPQs in chunks).
  Directories answer with an X-Wow-Directory header.
- /ws/<host>/<port>: a WebSocket bridged to a TCP port on this machine (authserver 3724, worldserver 8085), so other
  machines' clients reach the servers here even though the realmlist says 127.0.0.1.
- /register.html and POST /register: creates game accounts in the AzerothCore database (the login screen's Register
  button), at most 3 per client address an hour.

Usage: python serve.py [--port 8000] [--build ../../bin/Win] [--data ../../bin/Win/Data]
On your local network (HTTPS required there): python serve.py --host 0.0.0.0 --cert certs/wow.pem --key certs/wow.key
Only do that on a network you trust: it serves your game data to every machine that can reach it.
"""
import argparse
import base64
import hashlib
import os
import re
import socket
import ssl
import struct
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ALLOWED_PORTS = {3724, 8085}
WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
# The AzerothCore database, the way Mods/mods.sh reaches it.
MYSQL = ['docker', 'exec', '-i', '-e', 'MYSQL_PWD=acore', 'ac-mysql', 'mysql', '-uacore', '-N', 'acore_auth']
# SRP6 as the 3.3.5 client and AzerothCore use it.
SRP_N = int('894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7', 16)
SRP_G = 7
# ponytail: new accounts per client address, in memory; a restart forgets them.
REGISTERED = {}
REGISTER_LOCK = threading.Lock()


def srp_verifier(username, password, salt):
    """The verifier AzerothCore stores for this upper-case username and password, little-endian like the database."""
    x = int.from_bytes(hashlib.sha1(salt + hashlib.sha1(f'{username}:{password}'.encode()).digest()).digest(), 'little')
    return pow(SRP_G, x, SRP_N).to_bytes(32, 'little')


def create_account(username, password):
    """(HTTP status, message). username is checked to be [A-Z0-9], so it is safe inside the SQL; the rest is hex."""
    salt = os.urandom(32)
    sql = (f"INSERT INTO account (username, salt, verifier, expansion, joindate) "
           f"VALUES ('{username}', UNHEX('{salt.hex()}'), UNHEX('{srp_verifier(username, password, salt).hex()}'), 2, NOW());\n"
           # what AccountMgr::CreateAccount adds, so the realm list shows a character count
           "INSERT INTO realmcharacters (realmid, acctid, numchars) SELECT realmlist.id, account.id, 0 FROM realmlist, account "
           "LEFT JOIN realmcharacters ON acctid = account.id WHERE acctid IS NULL;\n")
    try:
        done = subprocess.run(MYSQL, input=sql, capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.TimeoutExpired):
        return 503, 'The account database is not reachable right now.'
    if done.returncode == 0:
        return 200, f'Account {username} created. Go back to the game and log in.'
    if 'Duplicate entry' in done.stderr:
        return 409, 'That username is taken.'
    print('register failed:', done.stderr.strip())
    return 500, 'Creating the account failed.'


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
        elif path == '/register.html':
            return os.path.join(HERE, 'register.html')
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

    def do_POST(self):
        if self.path != '/register':
            return self.reply(404, '')
        form = parse_qs(self.rfile.read(min(int(self.headers.get('Content-Length', 0)), 1024)).decode(errors='replace'))
        username = form.get('username', [''])[0].upper()
        password = form.get('password', [''])[0].upper()  # the client upper-cases passwords before hashing
        if not re.fullmatch(r'[A-Z0-9]{3,16}', username) or not re.fullmatch(r'[ -~]{4,16}', password):
            return self.reply(400, 'Use 3 to 16 letters and digits for the username and 4 to 16 plain characters for the password.')
        # Behind a reverse proxy every request comes from this machine; the proxy names the real client.
        client = self.client_address[0]
        if client in ('127.0.0.1', '::1') and self.headers.get('X-Forwarded-For'):
            client = self.headers['X-Forwarded-For'].split(',')[0].strip()
        with REGISTER_LOCK:
            now = time.time()
            recent = [t for t in REGISTERED.get(client, []) if now - t < 3600]
            if len(recent) >= 3:
                return self.reply(429, 'Too many new accounts from your address; try again in an hour.')
            status, message = create_account(username, password)
            if status == 200:
                REGISTERED[client] = recent + [now]
        self.reply(status, message)

    def reply(self, status, text):
        body = text.encode()
        self.send_response(status)
        self.send_header('Content-Type', 'text/plain; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

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
    parser.add_argument('--host', default='127.0.0.1', help='0.0.0.0 to serve your local network (needs --cert)')
    parser.add_argument('--cert', help='HTTPS certificate (PEM); see make-cert.ps1')
    parser.add_argument('--key', help='HTTPS private key (PEM)')
    args = parser.parse_args()

    # Threads need a cross-origin isolated page, which browsers only allow on https or on localhost.
    if args.host not in ('127.0.0.1', 'localhost') and not args.cert:
        parser.error('other machines can only run the client over HTTPS: pass --cert and --key (see make-cert.ps1)')

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.daemon_threads = True
    server.build, server.data = os.path.abspath(args.build), os.path.abspath(args.data)
    scheme = 'http'
    if args.cert:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(args.cert, args.key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        scheme = 'https'
    where = 'localhost' if args.host in ('127.0.0.1', 'localhost') else ('this machine\'s address' if args.host == '0.0.0.0' else args.host)
    print(f'{scheme}://{where}:{args.port}/  (data: {server.data})')
    server.serve_forever()


if __name__ == '__main__':
    main()
