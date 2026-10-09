#!/usr/bin/env python3
"""
RUDP Web Chat: a browser front end for the RUDP chat.
Browsers cannot send raw UDP, so this small gateway speaks RUDP (same 13-byte
header, checksum, Go-Back-N, cumulative ACKs, fast retransmit as src/chat.c)
to the peer over UDP and serves a chat page over HTTP on localhost.
It interoperates with ./bin/chat on the other side.

  python3 web/rudp_web.py --udp 9001 --peer 100.88.28.25:9001 --name Harsh --http 8080
  then open http://localhost:8080
Standard library only.
"""
import argparse, json, select, socket, struct, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

SYN, ACK, FIN, DATA = 1, 2, 4, 8
WINDOW, TIMEOUT_MS, DUP = 8, 300, 3

def checksum(seq, ack, flags, payload):
    s = sum(struct.pack(">II", seq, ack)) + flags + sum(struct.pack(">H", len(payload))) + sum(payload)
    while s >> 16: s = (s & 0xFFFF) + (s >> 16)
    return ~s & 0xFFFF

def pack(seq, ack, flags, payload=b""):
    return struct.pack(">IIBHH", seq, ack, flags, checksum(seq, ack, flags, payload), len(payload)) + payload

def unpack(b):
    if len(b) < 13: return None
    seq, ack, flags, chk, n = struct.unpack(">IIBHH", b[:13])
    p = b[13:13 + n]
    if len(p) != n or chk != checksum(seq, ack, flags, p): return None
    return seq, ack, flags, p

class Chat:
    def __init__(s, udp, peer, name, loss):
        s.name, s.loss, s.lock = name, loss, threading.Lock()
        s.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.sock.bind(("0.0.0.0", udp))
        ip, port = peer.rsplit(":", 1)
        s.peer = (ip, int(port)); s.heard = False
        s.sendq, s.sent_at = [], []
        s.base = s.next = s.expected = s.dups = 0
        s.msgs, s.log = [], []
        s.stats = dict(sent=0, dropped=0, retx=0)
        s.import_random = __import__("random")

    def note(s, text, kind="info"):
        s.log.append(dict(t=time.strftime("%H:%M:%S"), kind=kind, text=text))
        s.log = s.log[-200:]

    def tx(s, flags, seq, ack, payload=b"", retx=False):
        s.stats["sent"] += 1
        if retx: s.stats["retx"] += 1
        if s.loss > 0 and s.import_random.random() * 100 < s.loss:
            s.stats["dropped"] += 1
            if flags & DATA:
                s.note(f"network lost message #{seq}" + (" (retransmission)" if retx else ""), "loss")
            return
        s.sock.sendto(pack(seq, ack, flags, payload), s.peer)

    def resend(s, frm, retx=True):
        for q in range(frm, s.next):
            s.sent_at[q] = time.time()
            s.tx(DATA | ACK, q, s.expected, s.sendq[q], retx)

    def send(s, text):
        with s.lock:
            if s.next - s.base >= WINDOW: return False
            payload = f"{s.name}: {text}"[:400].encode()
            s.sendq.append(payload); s.sent_at.append(time.time())
            s.msgs.append(dict(seq=s.next, mine=True, text=payload.decode(errors="replace"), t=time.strftime("%H:%M")))
            s.tx(DATA | ACK, s.next, s.expected, payload)
            s.next += 1
            return True

    def handle(s, raw, src):
        pk = unpack(raw)
        if pk is None:
            s.note("dropped corrupted packet", "loss"); return
        seq, ack, flags, p = pk
        if src != s.peer: s.peer = src
        if not s.heard:
            s.heard = True; s.note(f"connected to {src[0]}:{src[1]}", "ok")
        if flags & ACK:
            if s.base < ack <= s.next:
                s.base, s.dups = ack, 0
            elif not (flags & DATA) and ack == s.base and s.base < s.next:
                s.dups += 1
                if s.dups == DUP:
                    s.note(f"3 duplicate ACKs: fast retransmit from #{s.base}", "retx")
                    s.resend(s.base); s.dups = 0
        if flags & DATA:
            if seq == s.expected:
                s.msgs.append(dict(seq=seq, mine=False, text=p.decode(errors="replace"), t=time.strftime("%H:%M")))
                s.expected += 1
            elif seq < s.expected:
                s.note(f"duplicate #{seq} discarded", "info")
            else:
                s.note(f"out of order: got #{seq}, expected #{s.expected}, discarded", "info")
            s.tx(ACK, 0, s.expected)

    def loop(s):
        last_punch = 0
        while True:
            with s.lock:
                if not s.heard and time.time() - last_punch > 1:
                    last_punch = time.time(); s.tx(ACK, 0, s.expected)
                if s.base < s.next and (time.time() - s.sent_at[s.base]) * 1000 > TIMEOUT_MS:
                    s.note(f"timeout: resending #{s.base}..#{s.next - 1}", "retx")
                    s.resend(s.base)
            r, _, _ = select.select([s.sock], [], [], 0.05)
            if r:
                raw, src = s.sock.recvfrom(2048)
                with s.lock: s.handle(raw, src)

    def snapshot(s):
        with s.lock:
            ms = [dict(m, delivered=(m["mine"] and m["seq"] < s.base)) for m in s.msgs]
            return dict(name=s.name, peer=f"{s.peer[0]}:{s.peer[1]}", connected=s.heard, loss=s.loss,
                        base=s.base, next=s.next, expected=s.expected, window=WINDOW,
                        stats=dict(s.stats), msgs=ms, log=s.log[-60:])


def make_handler(chat, html):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a): pass
        def _send(self, code, body, ctype="application/json"):
            b = body if isinstance(body, bytes) else body.encode()
            self.send_response(code); self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
        def do_GET(self):
            if urlparse(self.path).path == "/state": self._send(200, json.dumps(chat.snapshot()))
            else: self._send(200, html, "text/html; charset=utf-8")
        def do_POST(self):
            n = int(self.headers.get("Content-Length", 0))
            d = json.loads(self.rfile.read(n) or b"{}")
            path = urlparse(self.path).path
            if path == "/send":
                ok = chat.send(str(d.get("text", ""))[:370]) if str(d.get("text", "")).strip() else False
                self._send(200, json.dumps(dict(ok=ok)))
            elif path == "/loss":
                chat.loss = max(0, min(90, int(d.get("loss", 0)))); self._send(200, "{}")
            else: self._send(404, "{}")
    return H

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--udp", type=int, default=9001); ap.add_argument("--peer", required=True)
    ap.add_argument("--name", default="me"); ap.add_argument("--http", type=int, default=8080)
    ap.add_argument("--loss", type=int, default=0)
    a = ap.parse_args()
    import os
    html = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "index.html"), encoding="utf-8").read()
    chat = Chat(a.udp, a.peer, a.name, a.loss)
    threading.Thread(target=chat.loop, daemon=True).start()
    print(f"RUDP Web Chat: open http://localhost:{a.http}   (UDP {a.udp} -> {a.peer})")
    ThreadingHTTPServer(("0.0.0.0", a.http), make_handler(chat, html)).serve_forever()
