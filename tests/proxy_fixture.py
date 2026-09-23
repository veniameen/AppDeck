#!/usr/bin/env python3
"""Offline loopback fixtures for tests/proxy_bridge_test.sh.

A target web server (127.0.0.1, and ::1 when available), an HTTP proxy with Basic auth (CONNECT and
absolute-form requests), a SOCKS5 proxy with an RFC 1929 login, a server that accepts and never answers,
a server that reads one message and hangs up, scripted servers that answer a SOCKS5 handshake with canned
bytes (an HTTP or TLS server on a SOCKS port, odd but valid SOCKS5 replies), and a closed port. The proxies only ever connect to loopback targets: anything else is refused with 403
(HTTP) or reply 2 (SOCKS), so nothing leaves the machine. Prints "<name> <port>" lines, then serves until
killed. Usage: proxy_fixture.py <work-dir>   (writes <work-dir>/big.bin, the download/upload payload)
"""
import base64
import hashlib
import http.server
import json
import os
import socket
import socketserver
import sys
import threading

USER = 'alice'.encode()
PASSWORD = 'pä55:w0rd'.encode()  # the same pair as in proxy_bridge_test.sh
BODY = b'appdeck-proxy-target\n'

stats = {}
lock = threading.Lock()


def count(key):
    with lock:
        stats[key] = stats.get(key, 0) + 1


def loopback(host):
    return host in ('localhost', '::1') or host.startswith('127.')


def recv_exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError('closed')
        data += chunk
    return data


def read_head(sock):
    data = b''
    while b'\r\n\r\n' not in data:
        chunk = sock.recv(65536)
        if not chunk or len(data) > 65536:
            raise ConnectionError('no head')
        data += chunk
    head, rest = data.split(b'\r\n\r\n', 1)
    lines = head.decode('latin-1').split('\r\n')
    return lines[0], lines[1:], rest


def split_host_port(target, default):
    if target.startswith('['):
        host, _, tail = target[1:].partition(']')
        return host, int(tail[1:]) if tail.startswith(':') else default
    host, _, port = target.partition(':')
    return host, int(port) if port else default


def pipe(client, upstream, first=b''):
    """Relay both ways; the end of one direction is passed on as a half-close."""
    if first:
        upstream.sendall(first)

    def copy(src, dst):
        try:
            while True:
                data = src.recv(65536)
                if not data:
                    break
                dst.sendall(data)
        except OSError:
            pass
        try:
            dst.shutdown(socket.SHUT_WR)
        except OSError:
            pass

    back = threading.Thread(target=copy, args=(upstream, client), daemon=True)
    back.start()
    copy(client, upstream)
    back.join()
    upstream.close()


def linger(sock):
    """Close as web servers do: stop sending, then read what the client still sends for a moment, so an
    unread request body does not turn the answer into a reset."""
    try:
        sock.shutdown(socket.SHUT_WR)
        sock.settimeout(2)
        while sock.recv(65536):
            pass
    except OSError:
        pass


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True
    request_queue_size = 128


class Target(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def reply(self, body, kind='text/plain'):
        self.send_response(200)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == '/big':
            self.reply(BIG, 'application/octet-stream')
        elif self.path == '/stats':
            with lock:
                self.reply(json.dumps(stats).encode(), 'application/json')
        elif self.path == '/headers':
            self.reply(json.dumps([self.requestline] + [k + ': ' + v for k, v in self.headers.items()]).encode())
        else:
            self.reply(BODY)

    def do_POST(self):
        size = int(self.headers.get('Content-Length', '0'))
        digest = hashlib.sha256()
        while size:
            chunk = self.rfile.read(min(size, 65536))
            if not chunk:
                break
            digest.update(chunk)
            size -= len(chunk)
        self.reply(digest.hexdigest().encode() + b'\n')


class Web(http.server.ThreadingHTTPServer):
    request_queue_size = 128

    def server_bind(self):
        # HTTPServer.server_bind asks socket.getfqdn(), a reverse DNS lookup that can stall for a long
        # time on CI machines; the fixture only needs the loopback address.
        socketserver.TCPServer.server_bind(self)
        host, port = self.server_address[:2]
        self.server_name, self.server_port = host, port


class Web6(Web):
    address_family = socket.AF_INET6


class HttpProxy(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            line, headers, rest = read_head(self.request)
        except (OSError, ConnectionError):
            return
        auth = [h.split(':', 1)[1].strip() for h in headers if h.lower().startswith('proxy-authorization:')]
        if auth != ['Basic ' + base64.b64encode(USER + b':' + PASSWORD).decode()]:
            count('http_auth_bad')
            self.request.sendall(b'HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm="fixture"\r\n'
                                 b'Content-Length: 0\r\nConnection: close\r\n\r\n')
            linger(self.request)
            return
        count('http_auth_ok')
        method, target, version = line.split(' ')
        if method == 'CONNECT':
            host, port = split_host_port(target, 443)
            if not loopback(host):
                count('http_refused_target')
                self.request.sendall(b'HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n')
                return
            try:
                upstream = socket.create_connection((host, port), timeout=5)
            except OSError:
                self.request.sendall(b'HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n')
                return
            upstream.settimeout(None)
            count('http_connect')
            self.request.sendall(b'HTTP/1.1 200 Connection established\r\n\r\n')
            pipe(self.request, upstream, rest)
            return
        if not target.startswith('http://'):
            self.request.sendall(b'HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n')
            return
        authority, slash, path = target[7:].partition('/')
        host, port = split_host_port(authority, 80)
        if not loopback(host):
            count('http_refused_target')
            self.request.sendall(b'HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n')
            return
        count('http_absolute')
        if any(h.lower().replace(' ', '') == 'connection:close' for h in headers):
            count('http_absolute_close')
        kept = [h for h in headers if not h.lower().startswith('proxy-')]
        upstream = socket.create_connection((host, port), timeout=5)
        upstream.settimeout(None)
        forward = ' '.join((method, '/' + path, version)) + '\r\n' + ''.join(h + '\r\n' for h in kept) + '\r\n'
        pipe(self.request, upstream, forward.encode('latin-1') + rest)


class SocksProxy(socketserver.BaseRequestHandler):
    def handle(self):
        s = self.request
        try:
            version, n = recv_exact(s, 2)
            methods = recv_exact(s, n)
            if version != 5 or 2 not in methods:
                count('socks_no_login_offered')
                s.sendall(b'\x05\xff')
                return
            s.sendall(b'\x05\x02')
            if recv_exact(s, 1) != b'\x01':
                return
            user = recv_exact(s, recv_exact(s, 1)[0])
            password = recv_exact(s, recv_exact(s, 1)[0])
            if (user, password) != (USER, PASSWORD):
                count('socks_auth_bad')
                s.sendall(b'\x01\x01')
                return
            count('socks_auth_ok')
            s.sendall(b'\x01\x00')
            _, command, _, kind = recv_exact(s, 4)
            if kind == 1:
                host = socket.inet_ntop(socket.AF_INET, recv_exact(s, 4))
            elif kind == 4:
                host = socket.inet_ntop(socket.AF_INET6, recv_exact(s, 16))
            else:
                host = recv_exact(s, recv_exact(s, 1)[0]).decode()
            port = int.from_bytes(recv_exact(s, 2), 'big')
        except (OSError, ConnectionError, ValueError):
            return
        count('socks_atyp%d' % kind)

        def answer(code):
            s.sendall(bytes([5, code, 0, 1, 0, 0, 0, 0, 0, 0]))

        if command != 1:
            return answer(7)
        if not loopback(host):
            count('socks_refused_target')
            return answer(2)
        try:
            upstream = socket.create_connection((host, port), timeout=5)
        except OSError:
            return answer(5)
        upstream.settimeout(None)
        answer(0)
        pipe(s, upstream)


class Silent(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            while self.request.recv(4096):
                pass
        except OSError:
            pass


def scripted(*replies):
    """A server that answers each message it receives with the next canned reply, then hangs up."""
    class Scripted(socketserver.BaseRequestHandler):
        def handle(self):
            try:
                for reply in replies:
                    if not self.request.recv(4096):
                        return
                    self.request.sendall(reply)
            except OSError:
                pass
    return Scripted


SOCKS_OK = bytes([5, 0, 0, 1, 127, 0, 0, 1, 0, 80])
SCRIPTS = {
    'http400': scripted(b'HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'),
    'tlsalert': scripted(bytes([0x15, 3, 1, 0, 2, 2, 40])),  # a TLS server's handshake_failure alert
    'socksgss': scripted(b'\x05\x01'),                     # picks GSSAPI, which the bridge never offers
    'socksinsist': scripted(b'\x05\x02'),                  # wants a login although none was offered
    'socksv5auth': scripted(b'\x05\x02', b'\x05\x00', SOCKS_OK),  # login answered with version 5, as some servers do
    'socksauthodd': scripted(b'\x05\x02', b'HT'),          # a refusal that is not a login reply
    'socksrep4': scripted(b'\x05\x00', b'\x05\x04\x00\x09'),  # host unreachable, with an unknown address type
    'hangup': scripted(b''),                               # reads the request, closes without a byte
}


def start(server):
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server.server_address[1]


def main():
    global BIG
    work = sys.argv[1]
    BIG = os.urandom(8 << 20)
    with open(os.path.join(work, 'big.bin'), 'wb') as f:
        f.write(BIG)
    ports = {
        'target': start(Web(('127.0.0.1', 0), Target)),
        'http': start(Server(('127.0.0.1', 0), HttpProxy)),
        'socks': start(Server(('127.0.0.1', 0), SocksProxy)),
        'silent': start(Server(('127.0.0.1', 0), Silent)),
    }
    for name, handler in SCRIPTS.items():
        ports[name] = start(Server(('127.0.0.1', 0), handler))
    try:
        ports['target6'] = start(Web6(('::1', 0), Target))
    except OSError:
        ports['target6'] = 0
    closed = socket.socket()
    closed.bind(('127.0.0.1', 0))
    ports['dead'] = closed.getsockname()[1]
    closed.close()  # nothing listens there: connections are refused
    for name, port in ports.items():
        print(name, port)
    sys.stdout.flush()
    threading.Event().wait(900)  # the test kills the fixture; this is only a safety net


if __name__ == '__main__':
    main()
