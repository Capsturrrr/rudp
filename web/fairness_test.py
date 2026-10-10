#!/usr/bin/env python3
"""Fairness: two flows share one bottleneck. Reports the share of packets each delivered while both were active
and Jain's fairness index (1.0 = equal, 0.5 = one flow takes everything).  python3 web/fairness_test.py"""
import os, statistics as st, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cc import Controller, load_q
from dqn import DQN
from sim_multi import run
HERE = os.path.dirname(os.path.abspath(__file__))
Q = load_q(os.path.join(HERE, "..", "results", "q_chat.txt")); NET = DQN(); NET.load(os.path.join(HERE, "..", "results", "dqn_chat.json"))
def mk(m): return Controller(m, [r[:] for r in Q] if m == "smart" else None, net=NET if m == "deep" else None)
PATHS = [("congestion only: 150 pkt/s, queue 30, no random loss", dict(loss=0.0, delay=0.04, rate=150, queue=30)),
         ("congestion + 2% random loss", dict(loss=0.02, delay=0.04, rate=150, queue=30)),
         ("long path: 150 pkt/s, 120 ms, queue 60", dict(loss=0.0, delay=0.12, rate=150, queue=60))]
PAIRS = [("aimd", "aimd"), ("deep", "deep"), ("deep", "aimd"), ("smart", "aimd"), ("fixed", "aimd")]
def jain(x): return sum(x) ** 2 / (len(x) * sum(v * v for v in x))
if __name__ == "__main__":
    seeds = 20
    for name, p in PATHS:
        print(f"\n{name}  (300 packets per flow, {seeds} seeds)")
        print(f"  {'pair':14s} {'share of flow A':>16s} {'Jain index':>11s} {'A time':>8s} {'B time':>8s}")
        for a, b in PAIRS:
            sh, jn, ta, tb = [], [], [], []
            for sd in range(seeds):
                r = run([mk(a), mk(b)], seed=sd, **p)
                x = [r[0][1], r[1][1]]; sh.append(x[0] / sum(x)); jn.append(jain(x)); ta.append(r[0][0]); tb.append(r[1][0])
            print(f"  {a+' vs '+b:14s} {100*st.mean(sh):14.0f} % {st.mean(jn):11.3f} {st.mean(ta):7.1f}s {st.mean(tb):7.1f}s")
