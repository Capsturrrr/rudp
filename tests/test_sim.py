#!/usr/bin/env python3
"""Behaviour tests for the controllers and the simulated transport (no network needed)."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web"))
from cc import Controller, load_q, CWND_MAX, FIXED_WINDOW
from dqn import DQN
from sim import run
HERE = os.path.dirname(os.path.abspath(__file__))
q = load_q(os.path.join(HERE, "..", "results", "q_chat.txt")); net = DQN(); net.load(os.path.join(HERE, "..", "results", "dqn_chat.json"))
fails = 0
def check(c, msg):
    global fails
    print(("ok:   " if c else "FAIL: ") + msg); fails += 0 if c else 1
def ctrl(m): return Controller(m, [r[:] for r in q] if m == "smart" else None, net=net if m == "deep" else None)

check(q is not None and len(q) == 80, "Q-table loads with 80 states")
for m in ("fixed", "aimd", "smart", "deep"):
    for sack in (False, True):
        for loss in (0.0, 0.1, 0.3):
            c = ctrl(m); c.learn = False
            t = run(c, loss, 0.04, n_msgs=40, seed=3, sack=sack)
            check(t < 30.0, f"{m} delivers 40 messages at {int(loss*100)}% loss ({'SACK' if sack else 'Go-Back-N'}) in {t:.1f}s")
c = ctrl("smart"); c.learn = False; run(c, 0.05, 0.05, seed=1)
check(2.0 <= c.cwnd <= CWND_MAX, "window stays within [2, 32]")
check(ctrl("fixed").window() == FIXED_WINDOW, "fixed mode uses window 8")
c = ctrl("aimd"); c.on_loss("timeout")
check(c.cwnd == 1.0, "AIMD resets the window to 1 on a timeout")
c = ctrl("aimd"); w = c.cwnd; c.on_loss("dup")
check(abs(c.cwnd - w / 2) < 1e-9, "AIMD halves the window on duplicate ACKs")
t_clean = run(ctrl("aimd"), 0.0, 0.04, seed=1); t_lossy = run(ctrl("aimd"), 0.2, 0.04, seed=1)
check(t_lossy > t_clean, "loss makes delivery slower")
check(len(net.q([0, 0, 0.3, 0.5, 0.0])) == 5, "neural agent outputs 5 action values")
print("all simulator tests passed" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
