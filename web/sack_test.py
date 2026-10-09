#!/usr/bin/env python3
"""Does selective acknowledgment help? Go-Back-N vs a basic SACK variant of the same simulated transport, on the 48 stress paths."""
import os, random, statistics as st, sys
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cc import Controller
from sim import run
from stress import sample_path

def one(p):
    out = {}
    for sack in (False, True):
        for m in ("fixed", "aimd"):
            out[(m, sack)] = st.mean(run(Controller(m), p["loss"], p["delay"], n_msgs=p["n"], rate=p["rate"], queue=p["queue"], seed=900 + sd, jitter=p["jitter"], sack=sack) for sd in range(3))
    return p, out

if __name__ == "__main__":
    rng = random.Random(424242); paths = [sample_path(rng) for _ in range(48)]
    with Pool(os.cpu_count()) as pool: res = pool.map(one, paths)
    for m in ("fixed", "aimd"):
        g = [o[(m, False)] for _, o in res]; s = [o[(m, True)] for _, o in res]
        w = sum(1 for a, b in zip(s, g) if a < b * 0.95); l = sum(1 for a, b in zip(s, g) if a > b * 1.05)
        print(f"{m:5s}: Go-Back-N mean {st.mean(g):5.2f} s median {st.median(g):4.2f} | SACK mean {st.mean(s):5.2f} s median {st.median(s):4.2f} | SACK faster on {w}, slower on {l}, within 5% on {48-w-l}")
