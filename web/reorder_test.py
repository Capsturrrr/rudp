#!/usr/bin/env python3
"""Packet reordering test: jitter larger than the packet spacing makes packets arrive out of order.
Go-Back-N drops out-of-order packets and sees duplicate ACKs, so reordering looks like loss. Path: 2% loss, 50 ms delay, 60 messages."""
import os, statistics as st, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cc import Controller, load_q
from dqn import DQN
from sim import run
HERE = os.path.dirname(os.path.abspath(__file__))
q = load_q(os.path.join(HERE, "..", "results", "q_chat.txt")); net = DQN(); net.load(os.path.join(HERE, "..", "results", "dqn_chat.json"))
print("jitter   transport     fixed   AIMD  table   deep   (mean s, 40 seeds)")
for j in (0.0, 0.02, 0.05, 0.1, 0.2):
    for sack in (False, True):
        r = []
        for m in ("fixed", "aimd", "smart", "deep"):
            ts = []
            for sd in range(40):
                c = Controller(m, [x[:] for x in q] if m == "smart" else None, net=net if m == "deep" else None); c.learn = False
                ts.append(run(c, 0.02, 0.05, n_msgs=60, seed=sd, jitter=j, sack=sack))
            r.append(st.mean(ts))
        print(f"{int(j*1000):4d} ms  {'SACK       ' if sack else 'Go-Back-N '} " + " ".join(f"{x:6.2f}" for x in r))
