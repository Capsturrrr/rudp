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
import argparse, collections, heapq, json, math, os, random, select, socket, struct, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

SYN, ACK, FIN, DATA = 1, 2, 4, 8
WINDOW, TIMEOUT_MS, DUP = 8, 300, 3
RL_MULT = [0.5, 0.85, 1.0, 1.15, 1.5]
CWND_MAX = 32.0
INTERVAL = 0.5


def rl_state(rtt_ratio, loss, cwnd):
    rb = 0 if rtt_ratio < 1.15 else 1 if rtt_ratio < 1.5 else 2 if rtt_ratio < 2.2 else 3
    lb = 0 if loss <= 0 else 1 if loss < 0.03 else 2 if loss < 0.10 else 3
    cb = 0 if cwnd < 4 else 1 if cwnd < 12 else 2 if cwnd < 32 else 3 if cwnd < 64 else 4
    return (rb * 4 + lb) * 5 + cb


def load_q(path):
    try:
        rows = [list(map(float, l.split())) for l in open(path).read().strip().splitlines()[1:]]
        if len(rows) == 80 and all(len(r) == 5 for r in rows): return rows
    except Exception: pass
    return None

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
    def __init__(s, udp, peer, name, loss, delay=0):
        s.name, s.loss, s.lock = name, loss, threading.Lock()
        s.delay, s.dq, s.dseq = delay / 1000.0, [], 0
        s.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.sock.bind(("0.0.0.0", udp))
        ip, port = peer.rsplit(":", 1)
        s.peer = (ip, int(port)); s.heard = False
        s.sendq, s.sent_at = [], []
        s.base = s.next = s.expected = s.dups = 0
        s.msgs, s.log = [], []
        s.stats = dict(sent=0, dropped=0, retx=0)
        s.import_random = __import__("random")
        s.mode = "fixed"                     # fixed | aimd | smart
        s.cwnd, s.ssthresh = 8.0, 16.0
        s.first_sent, s.retxed = {}, set()
        s.srtt = s.min_rtt = None
        s.pending = collections.deque()
        s.bulk = None                        # dict(total, start, done_at, mode)
        s.q = load_q(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results", "q_lossy.txt"))
        s.agent_ok = s.q is not None
        s.last_sa = None
        s.iv_start = time.time(); s.iv_sent = s.iv_retx = s.iv_acked = s.iv_new = s.iv_lossev = 0; s.iv_rtts = []
        s.last_action = "-"


    def note(s, text, kind="info"):
        s.log.append(dict(t=time.strftime("%H:%M:%S"), kind=kind, text=text))
        s.log = s.log[-200:]

    def window(s):
        return WINDOW if s.mode == "fixed" else max(1, int(s.cwnd))

    def set_mode(s, m):
        if m not in ("fixed", "aimd", "smart"): return
        s.mode = m
        s.cwnd = 8.0 if m == "fixed" else max(1.0, min(s.cwnd, CWND_MAX))
        if m == "aimd": s.ssthresh = 16.0
        s.note(f"window controller: {m}", "ok")

    def tx(s, flags, seq, ack, payload=b"", retx=False):
        if flags & DATA:
            s.iv_sent += 1
            if retx: s.iv_retx += 1
            else: s.iv_new += 1
        s.stats["sent"] += 1
        if retx: s.stats["retx"] += 1
        if s.loss > 0 and s.import_random.random() * 100 < s.loss:
            s.stats["dropped"] += 1
            if flags & DATA:
                s.note(f"network lost message #{seq}" + (" (retransmission)" if retx else ""), "loss")
            return
        raw = pack(seq, ack, flags, payload)
        if s.delay > 0:
            s.dseq += 1
            heapq.heappush(s.dq, (time.time() + s.delay, s.dseq, raw, s.peer))
        else:
            s.sock.sendto(raw, s.peer)

    def resend(s, frm, retx=True):
        for q in range(frm, s.next):
            s.retxed.add(q)
            s.sent_at[q] = time.time()
            s.tx(DATA | ACK, q, s.expected, s.sendq[q], retx)

    def send(s, text):
        with s.lock:
            s.pending.append(f"{s.name}: {text}"[:400].encode())
            s.pump()
            return True

    def bulk_start(s, n=60):
        with s.lock:
            s.bulk = dict(total=n, start=time.time(), done_at=None, mode=s.mode, first=s.next + len(s.pending))
            for i in range(n):
                s.pending.append(f"{s.name}: test message {i + 1}/{n}".encode())
            s.note(f"stress test: {n} messages, controller = {s.mode}", "ok")
            s.pump()

    def pump(s):
        while s.pending and s.next - s.base < s.window():
            payload = s.pending.popleft()
            s.sendq.append(payload); s.sent_at.append(time.time())
            s.first_sent[s.next] = time.time()
            s.msgs.append(dict(seq=s.next, mine=True, text=payload.decode(errors="replace"), t=time.strftime("%H:%M")))
            s.tx(DATA | ACK, s.next, s.expected, payload)
            s.next += 1

    def on_acked(s, old_base, new_base):
        now = time.time()
        for q in range(old_base, new_base):
            s.iv_acked += 1
            t0 = s.first_sent.pop(q, None)
            if t0 is not None and q not in s.retxed:       # Karn: skip retransmitted samples
                r = now - t0
                s.iv_rtts.append(r)
                s.srtt = r if s.srtt is None else 0.875 * s.srtt + 0.125 * r
                s.min_rtt = r if s.min_rtt is None else min(s.min_rtt, r)
            s.retxed.discard(q)
            if s.mode == "aimd":
                s.cwnd = min(CWND_MAX, s.cwnd + (1.0 if s.cwnd < s.ssthresh else 1.0 / s.cwnd))
        if s.bulk and s.bulk["done_at"] is None and new_base >= s.bulk["first"] + s.bulk["total"]:
            s.bulk["done_at"] = time.time()
            s.note(f"stress test done: {s.bulk['total']} messages in {s.bulk['done_at'] - s.bulk['start']:.1f} s ({s.bulk['mode']})", "ok")

    def loss_event(s, kind):
        s.iv_lossev += 1
        if s.mode == "aimd":
            if kind == "timeout": s.ssthresh = max(2.0, s.cwnd / 2); s.cwnd = 1.0
            else: s.ssthresh = max(2.0, s.cwnd / 2); s.cwnd = s.ssthresh

    def rl_step(s):
        """Once per interval (about one RTT): observe, learn from the last action, choose the next."""
        now = time.time()
        interval = max(0.1, min(0.5, s.srtt or 0.5))
        if now - s.iv_start < interval: return
        dur = now - s.iv_start
        new, lossev, acked = s.iv_new, s.iv_lossev, s.iv_acked
        rtts = s.iv_rtts
        s.iv_start, s.iv_sent, s.iv_retx, s.iv_acked, s.iv_rtts = now, 0, 0, 0, []
        s.iv_new = s.iv_lossev = 0
        if s.mode != "smart" or not s.agent_ok or (new == 0 and acked == 0 and lossev == 0): return
        base_rtt = s.min_rtt or 0.05
        cur = (sum(rtts) / len(rtts)) if rtts else (s.srtt or base_rtt)
        ratio = max(1.0, cur / base_rtt)
        loss = min(1.0, lossev / max(1, new))        # loss events per new packet (Go-Back-N inflates raw resend counts)
        st = rl_state(ratio, loss, s.cwnd)
        if s.last_sa is not None:
            full = max(1.0, s.cwnd * dur / max(base_rtt, 0.02))   # acks a full window would produce
            thr = min(1.0, acked / full)
            r = thr - 0.5 * max(0.0, ratio - 1.0) - 4.0 * loss
            ps, pa = s.last_sa
            s.q[ps][pa] += 0.15 * (r + 0.9 * max(s.q[st]) - s.q[ps][pa])
        a = max(range(5), key=lambda k: s.q[st][k])
        if loss == 0 and ratio < 1.15 and s.pending: a = max(a, 3)   # backlog on a clean path: never shrink
        s.last_sa = (st, a)
        s.cwnd = max(2.0, min(CWND_MAX, s.cwnd * RL_MULT[a]))
        s.last_action = f"x{RL_MULT[a]}"
        s.note(f"smart: rtt x{ratio:.1f}, loss {loss*100:.0f}%, cwnd -> {s.cwnd:.1f} ({s.last_action})", "info")

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
                s.on_acked(s.base, ack)
                s.base, s.dups = ack, 0
            elif not (flags & DATA) and ack == s.base and s.base < s.next:
                s.dups += 1
                if s.dups == DUP:
                    s.note(f"3 duplicate ACKs: fast retransmit from #{s.base}", "retx")
                    s.loss_event("dup"); s.resend(s.base); s.dups = 0
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
                    s.loss_event("timeout"); s.resend(s.base)
                s.rl_step(); s.pump()
            with s.lock:
                while s.dq and s.dq[0][0] <= time.time():
                    _, _, raw_, dst_ = heapq.heappop(s.dq)
                    s.sock.sendto(raw_, dst_)
            r, _, _ = select.select([s.sock], [], [], 0.01)
            if r:
                raw, src = s.sock.recvfrom(2048)
                with s.lock: s.handle(raw, src)

    def snapshot(s):
        with s.lock:
            ms = [dict(m, delivered=(m["mine"] and m["seq"] < s.base)) for m in s.msgs]
            return dict(name=s.name, peer=f"{s.peer[0]}:{s.peer[1]}", connected=s.heard, loss=s.loss,
                        base=s.base, next=s.next, expected=s.expected, window=s.window(), mode=s.mode, cwnd=round(s.cwnd, 1), queued=len(s.pending), agent_ok=s.agent_ok, last_action=s.last_action, bulk=(dict(s.bulk, elapsed=round((s.bulk['done_at'] or time.time()) - s.bulk['start'], 1), done=s.bulk['done_at'] is not None) if s.bulk else None),
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
            elif path == "/mode":
                with chat.lock: chat.set_mode(str(d.get("mode")))
                self._send(200, "{}")
            elif path == "/bulk":
                chat.bulk_start(int(d.get("n", 60))); self._send(200, "{}")
            elif path == "/loss":
                chat.loss = max(0, min(90, int(d.get("loss", 0)))); self._send(200, "{}")
            else: self._send(404, "{}")
    return H

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--udp", type=int, default=9001); ap.add_argument("--peer", required=True)
    ap.add_argument("--name", default="me"); ap.add_argument("--http", type=int, default=8080)
    ap.add_argument("--loss", type=int, default=0); ap.add_argument("--delay", type=int, default=0, help="emulated one-way delay in ms")
    a = ap.parse_args()
    import os
    html = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "index.html"), encoding="utf-8").read()
    chat = Chat(a.udp, a.peer, a.name, a.loss, a.delay)
    threading.Thread(target=chat.loop, daemon=True).start()
    print(f"RUDP Web Chat: open http://localhost:{a.http}   (UDP {a.udp} -> {a.peer})")
    ThreadingHTTPServer(("0.0.0.0", a.http), make_handler(chat, html)).serve_forever()
