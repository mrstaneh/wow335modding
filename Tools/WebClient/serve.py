"""wow335modding: local server for the browser build.

- / and the build output (WowWeb.js, WowWeb.wasm) with the cross-origin isolation headers threads need.
- /data/... from your own game Data folder, with Range requests (System/File/Web/WebFile.cpp reads MPQs in chunks).
  Directories answer with an X-Wow-Directory header.
- /ws/<host>/<port>: a WebSocket bridged to a TCP port on this machine (authserver 3724, worldserver 8085), so other
  machines' clients reach the servers here even though the realmlist says 127.0.0.1.
- POST /guest: a new guest account in the AzerothCore database (random name and password), which the page keeps in the
  browser and logs in with; at most 10 per client address an hour.
- Discord, when discord.json holds a Discord application's {"client_id": ..., "client_secret": ..., "redirect_uri":
  "https://<your host>/auth/discord/callback"} (that address also in the application's OAuth2 redirects):
  POST /auth/discord/link saves the browser's guest account with a Discord account, GET /auth/discord signs another
  browser in to the account saved with yours. The links are in the web_discord table, with the guest's generated
  password so every browser signed in stays signed in.

Usage: python serve.py [--port 8000] [--build ../../bin/Win] [--data ../../bin/Win/Data]
On your local network (HTTPS required there): python serve.py --host 0.0.0.0 --cert certs/wow.pem --key certs/wow.key
Only do that on a network you trust: it serves your game data to every machine that can reach it.
"""
import argparse
import base64
import hashlib
import html
import json
import os
import re
import secrets
import socket
import ssl
import struct
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlencode
from urllib.request import Request, urlopen

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
# Guest accounts: GUEST and 11 letters and digits, a 16-character password of the same.
GUEST_NAME = re.compile(r'GUEST[A-Z0-9]{11}')
ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
# discord_name: the Discord username, refreshed at every Discord sign-in, for the character screen.
DISCORD_TABLE = ('CREATE TABLE IF NOT EXISTS web_discord (discord_id BIGINT UNSIGNED PRIMARY KEY, '
                 'account_id INT UNSIGNED NOT NULL UNIQUE, password VARCHAR(16) NOT NULL, '
                 "discord_name VARCHAR(64) NOT NULL DEFAULT '');\n")
# Discord sign-ins under way: state -> (expiry, redirect URI, (guest name, password) to save, or None to sign in).
# ponytail: in memory; a restart drops sign-ins in progress, which then just start over.
DISCORD_STATES = {}


def srp_verifier(username, password, salt):
    """The verifier AzerothCore stores for this upper-case username and password, little-endian like the database."""
    x = int.from_bytes(hashlib.sha1(salt + hashlib.sha1(f'{username}:{password}'.encode()).digest()).digest(), 'little')
    return pow(SRP_G, x, SRP_N).to_bytes(32, 'little')


def sql(query):
    """The rows of the last statement as lists of strings; raises OSError when the database is unreachable or fails.
    Callers only put [A-Z0-9], digits and hex inside queries."""
    try:
        done = subprocess.run(MYSQL, input=query, capture_output=True, text=True, timeout=15)
    except subprocess.TimeoutExpired as error:
        raise OSError('the database timed out') from error
    if done.returncode:
        raise OSError(done.stderr.strip())
    return [line.split('\t') for line in done.stdout.splitlines()]


def random_text(length):
    return ''.join(secrets.choice(ALPHABET) for _ in range(length))


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
        return 200, f'Account {username} created.'
    if 'Duplicate entry' in done.stderr:
        return 409, 'That username is taken.'
    print('register failed:', done.stderr.strip())
    return 500, 'Creating the account failed.'


def guest_password_ok(username, password):
    """Whether this is a guest account and its password."""
    if not GUEST_NAME.fullmatch(username) or not re.fullmatch(r'[A-Z0-9]{16}', password):
        return False
    rows = sql(f"SELECT HEX(salt), HEX(verifier) FROM account WHERE username = '{username}';")
    return bool(rows) and srp_verifier(username, password, bytes.fromhex(rows[0][0])).hex().upper() == rows[0][1]


def discord_account(discord_id):
    """(username, password) of the guest account saved with this Discord account, or None."""
    rows = sql(DISCORD_TABLE + f'SELECT account.username, web_discord.password FROM web_discord '
               f'JOIN account ON account.id = web_discord.account_id WHERE discord_id = {discord_id};')
    return tuple(rows[0]) if rows else None


def discord_config():
    try:
        with open(os.path.join(HERE, 'discord.json')) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def discord_user(config, code, redirect_uri):
    """(id, username) of the Discord account that signed in with this code: the unique name, now that Discord has
    dropped the #1234 numbers (the display name is not unique)."""
    agent = {'User-Agent': 'wow335modding-webclient (serve.py)'}  # Discord refuses Python's default agent
    form = urlencode({'client_id': config['client_id'], 'client_secret': config['client_secret'],
                      'grant_type': 'authorization_code', 'code': code, 'redirect_uri': redirect_uri}).encode()
    request = Request('https://discord.com/api/oauth2/token', form, {**agent, 'Content-Type': 'application/x-www-form-urlencoded'})
    with urlopen(request, timeout=10) as response:
        token = json.load(response)['access_token']
    with urlopen(Request('https://discord.com/api/users/@me', headers={**agent, 'Authorization': f'Bearer {token}'}), timeout=10) as response:
        user = json.load(response)
    return str(user['id']), user['username']


# Where the Discord pages send the browser: the account to keep (JSON), and whether to ask first. The page asks anyway
# when keeping it would drop a guest account that is not saved with Discord.
ACCOUNT_PAGE = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>WoW Web Client</title>
<style>
  html, body { margin: 0; height: 100%; background: #000; color: #e8dcc0; font: 16px system-ui, sans-serif; }
  main { display: grid; place-content: center; gap: 16px; height: 100%; max-width: 520px; margin: 0 auto; padding: 0 16px; text-align: center; }
  main[hidden] { display: none; }
  div { display: flex; gap: 12px; justify-content: center; flex-wrap: wrap; }
  button { padding: 10px 22px; border: 0; border-radius: 6px; background: #8a1c12; color: #fff; font: inherit; font-weight: 600; cursor: pointer; }
  button.quiet { background: #3a3328; }
</style></head>
<body><main hidden><p>{{MESSAGE}}</p><div></div></main>
<script>
  const account = {{ACCOUNT}};
  const ask = {{ASK}};
  const main = document.querySelector('main');
  const back = () => location.replace('/');
  const button = (label, quiet, onclick) => {
    const element = document.createElement('button');
    element.textContent = label;
    element.className = quiet ? 'quiet' : '';
    element.onclick = onclick;
    main.querySelector('div').append(element);
  };
  const use = () => {
    try { localStorage.setItem('wowAccount', JSON.stringify(account)); } catch (error) {}
    back();
  };
  let current = null;
  try { current = JSON.parse(localStorage.getItem('wowAccount')); } catch (error) {}
  const dropsGuest = account && current && !current.discord && current.username !== account.username;
  if (account && !ask && !dropsGuest) {
    use();
  } else {
    if (dropsGuest && !ask) main.querySelector('p').textContent = 'This browser plays a guest account that is not saved with Discord. Signing in replaces it, and it will be lost.';
    if (account) button('Play that account', false, use);
    button(account ? "Keep this browser's account" : 'Back to the game', !!account, back);
    main.hidden = false;
  }
</script></body></html>
'''


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
        path = self.path.split('?')[0]
        if path == '/auth/discord':
            return self.discord_start(None)
        if path == '/auth/discord/callback':
            return self.discord_callback()
        if path == '/auth/discord/saved':
            return self.discord_saved()
        self.serve(True)

    def do_POST(self):
        form = parse_qs(self.rfile.read(min(int(self.headers.get('Content-Length', 0)), 1024)).decode(errors='replace'))
        if self.path == '/guest':
            return self.guest()
        if self.path == '/auth/discord/link':
            guest = (form.get('username', [''])[0], form.get('password', [''])[0])
            try:
                if not guest_password_ok(*guest):
                    return self.reply(403, 'Only the guest account this browser plays can be saved with Discord.')
            except OSError as error:
                print('discord save failed:', error)
                return self.reply(503, 'The account database is not reachable right now.')
            return self.discord_start(guest)
        self.reply(404, '')

    def client(self):
        """The client's address. Behind a reverse proxy every request comes from this machine; the proxy names it."""
        client = self.client_address[0]
        if client in ('127.0.0.1', '::1') and self.headers.get('X-Forwarded-For'):
            client = self.headers['X-Forwarded-For'].split(',')[0].strip()
        return client

    def guest(self):
        """A new guest account, as 'USERNAME PASSWORD'."""
        client = self.client()
        with REGISTER_LOCK:
            now = time.time()
            recent = [t for t in REGISTERED.get(client, []) if now - t < 3600]
            if len(recent) >= 10:
                return self.reply(429, 'Too many new accounts from your address; try again in an hour.')
            username, password = 'GUEST' + random_text(11), random_text(16)
            status, message = create_account(username, password)
            if status == 200:
                REGISTERED[client] = recent + [now]
                message = f'{username} {password}'
        self.reply(status, message)

    def discord_start(self, guest):
        """Off to Discord: a redirect when the page navigates here to sign in, the address as text to save a guest."""
        config = discord_config()
        if not config:
            message = 'Discord sign-in is not set up on this server.'
            return self.reply(503, message) if guest else self.account_page(message)
        # From discord.json: behind a reverse proxy this server cannot tell its public address.
        redirect_uri = config['redirect_uri']
        state = secrets.token_urlsafe(24)
        now = time.time()
        for old in [key for key, value in list(DISCORD_STATES.items()) if value[0] < now]:
            DISCORD_STATES.pop(old, None)
        DISCORD_STATES[state] = (now + 600, redirect_uri, guest)
        url = 'https://discord.com/oauth2/authorize?' + urlencode({
            'client_id': config['client_id'], 'response_type': 'code', 'redirect_uri': redirect_uri,
            'scope': 'identify', 'state': state, 'prompt': 'none'})
        if guest:
            return self.reply(200, url)
        self.send_response(302)
        self.send_header('Location', url)
        self.send_header('Content-Length', '0')
        self.end_headers()

    def discord_saved(self):
        """The Discord username the guest account ?username= is saved with (empty if not known yet), or 404 when it is
        not saved. The character screen asks this rather than trusting the browser, whose copy misses saves made from
        another address of this server."""
        username = parse_qs(self.path.partition('?')[2]).get('username', [''])[0]
        if not GUEST_NAME.fullmatch(username):
            return self.reply(404, '')
        try:
            # hex, because the name comes from Discord
            rows = sql(DISCORD_TABLE + f"SELECT HEX(discord_name) FROM web_discord JOIN account ON account.id = web_discord.account_id "
                       f"WHERE account.username = '{username}';")
        except OSError as error:
            print('discord check failed:', error)
            return self.reply(503, 'The account database is not reachable right now.')
        if not rows:
            return self.reply(404, '')
        self.reply(200, bytes.fromhex(rows[0][0]).decode(errors='replace'))

    def discord_callback(self):
        query = parse_qs(self.path.partition('?')[2])
        entry = DISCORD_STATES.pop(query.get('state', [''])[0], None)
        if not entry or entry[0] < time.time():
            return self.account_page('That Discord sign-in expired. Try again from the game.')
        if 'code' not in query:
            return self.account_page('Discord sign-in was cancelled.')
        _, redirect_uri, guest = entry
        try:
            discord_id, name = discord_user(discord_config(), query['code'][0], redirect_uri)
            if not discord_id.isdigit():
                raise ValueError(discord_id)
            saved = discord_account(discord_id)
            name_sql = f"UNHEX('{name.encode()[:64].hex()}')"  # hex, because the name comes from Discord
            if saved:
                sql(f'UPDATE web_discord SET discord_name = {name_sql} WHERE discord_id = {discord_id};')
            if guest is None:
                if not saved:
                    return self.account_page('No game account is saved with this Discord account yet. Play, then use '
                                             '"Save with Discord" on the character screen.')
                return self.account_page(None, (*saved, name))
            if saved and saved[0] != guest[0]:
                return self.account_page('This Discord account already has a game account saved with it. Play that one '
                                         'instead? The guest account on this browser is not saved anywhere, so it '
                                         'would be lost.', (*saved, name))
            if not saved:
                rows = sql(DISCORD_TABLE + f"INSERT IGNORE INTO web_discord (discord_id, account_id, password, discord_name) "
                           f"SELECT {discord_id}, id, '{guest[1]}', {name_sql} FROM account WHERE username = '{guest[0]}';\n"
                           "SELECT ROW_COUNT();")
                if rows != [['1']]:
                    return self.account_page('This game account is already saved with a different Discord account.')
            return self.account_page(None, (*guest, name))
        except (OSError, ValueError, KeyError) as error:
            print('discord sign-in failed:', error)
            return self.account_page('Signing in with Discord failed. Try again in a moment.')

    def account_page(self, message, account=None):
        """The page that keeps account (username, password, Discord name) in this browser and goes back to the game;
        with a message it shows that first."""
        data = json.dumps({'username': account[0], 'password': account[1], 'discord': account[2]} if account else None)
        # the account last: the Discord name is the one value from outside
        body = (ACCOUNT_PAGE.replace('{{MESSAGE}}', html.escape(message or ''))
                .replace('{{ASK}}', 'true' if message else 'false')
                .replace('{{ACCOUNT}}', data.replace('<', '\\u003c'))).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

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
