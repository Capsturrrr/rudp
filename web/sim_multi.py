"""Several flows share one bottleneck link (drop-tail queue). Each flow has its own window controller.
Used to test fairness: does a learned controller starve, or get starved by, an AIMD flow?"""
import heapq, random
from cc import Controller

TIMEOUT = 0.3
TICK = 0.01

class Flow:
    def __init__(s, ctrl, n):
        s.c, s.n = ctrl, n
        s.base = s.nxt = 0; s.pending = n
        s.sent_at, s.first_sent, s.retxed = {}, {}, set()
        s.expected = 0; s.dups = 0; s.done = None; s.acked_at_first_done = None
        ctrl.reset()

def run(ctrls, n_msgs=300, loss=0.0, delay=0.04, rate=150, queue=30, seed=0, max_t=60.0):
    """Returns per-flow (completion_time, packets_delivered_when_first_flow_finished)."""
    rng = random.Random(seed)
    flows = [Flow(c, n_msgs) for c in ctrls]
    ev, cnt = [], 0
    def push(t, kind, data=None):
        nonlocal cnt; cnt += 1; heapq.heappush(ev, (t, cnt, kind, data))
    t = 0.0; link_free = 0.0
    def tx_data(f, seq, retx):
        nonlocal link_free
        if not retx: f.c.on_new_packet()
        if rng.random() < loss: return
        start = max(t, link_free)
        if (start - t) * rate > queue: return
        link_free = start + 1.0 / rate
        push(link_free + delay, "data", (flows.index(f), seq))
    def tx_ack(i, ackn):
        if rng.random() < loss: return
        push(t + delay, "ack", (i, ackn))
    def pump(f):
        while f.pending and f.nxt - f.base < f.c.window():
            f.first_sent[f.nxt] = t; f.sent_at[f.nxt] = t
            tx_data(f, f.nxt, False); f.nxt += 1; f.pending -= 1
    def resend(f):
        for q in range(f.base, f.nxt):
            f.retxed.add(q); f.sent_at[q] = t; tx_data(f, q, True)
    push(0.0, "tick")
    for f in flows: pump(f)
    first_done = None
    while ev:
        t, _, kind, data = heapq.heappop(ev)
        if t > max_t: break
        if kind == "data":
            i, seq = data; f = flows[i]
            if seq == f.expected: f.expected += 1
            tx_ack(i, f.expected)
        elif kind == "ack":
            i, a = data; f = flows[i]
            if f.base < a <= f.nxt:
                samples = []
                for q in range(f.base, a):
                    t0 = f.first_sent.pop(q, None)
                    if t0 is not None and q not in f.retxed: samples.append(t - t0)
                    f.retxed.discard(q)
                f.c.on_ack(a - f.base, samples); f.base, f.dups = a, 0
                if f.base >= f.n and f.done is None:
                    f.done = t
                    if first_done is None:
                        first_done = t
                        for g in flows: g.acked_at_first_done = g.base
            elif a == f.base and f.base < f.nxt:
                f.dups += 1
                if f.dups == 3: f.c.on_loss("dup"); resend(f); f.dups = 0
        elif kind == "tick":
            for f in flows:
                if f.done is None:
                    if f.base < f.nxt and t - f.sent_at[f.base] > TIMEOUT: f.c.on_loss("timeout"); resend(f)
                    f.c.step(t, f.pending); pump(f)
            if all(f.done is not None for f in flows): break
            push(t + TICK, "tick")
    return [(f.done if f.done is not None else max_t, f.acked_at_first_done or f.base) for f in flows]
