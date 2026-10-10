#!/usr/bin/env python3
"""Ablation study of the neural agent: retrain with one design choice removed, score on the 48 stress paths.
   python3 web/ablate.py   (about 10 min on 4+ cores)"""
import os, sys, random, statistics as st
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc
VARIANTS = {"full": {}, "no_mask": {"MASK": False}, "no_delay_penalty": {"DELAY_W": 0.0},
            "no_last_action": {"FEAT_ZERO": (4,)}, "no_throughput_feat": {"FEAT_ZERO": (3,)}}
SEEDS = (7, 11, 12, 13)

DEFAULTS = {"MASK": True, "DELAY_W": 0.5, "FEAT_ZERO": ()}
def apply(var):
    for k, v in DEFAULTS.items(): setattr(cc, k, v)
    for k, v in VARIANTS[var].items(): setattr(cc, k, v)

def job(a):
    var, seed = a
    apply(var)
    import train_deep, dqn
    net = train_deep.train(4000, seed=seed, log=False)
    return var, seed, train_deep.validate(net), net

def score(net, var):
    apply(var)
    from stress import sample_path
    from sim import run
    rng = random.Random(424242); paths = [sample_path(rng) for _ in range(48)]
    out = []
    for p in paths:
        ts = []
        for sd in range(3):
            c = cc.Controller("deep", None, net=net); c.learn = False
            ts.append(run(c, p["loss"], p["delay"], n_msgs=p["n"], rate=p["rate"], queue=p["queue"], seed=900 + sd, jitter=p["jitter"]))
        out.append(st.mean(ts))
    return out

def main_score(a):
    var, seed, v, net = a
    return var, seed, v, st.mean(score(net, var))

if __name__ == "__main__":
    with Pool(os.cpu_count()) as pool:
        trained = pool.map(job, [(v, s) for v in VARIANTS for s in SEEDS])
        scored = pool.map(main_score, trained)
    print("variant               best-of-seeds (by validation)   all seeds' stress means")
    for var in VARIANTS:
        rows = [r for r in scored if r[0] == var]
        best = min(rows, key=lambda r: r[2])
        print(f"{var:20s}  stress {best[3]:6.2f} s (seed {best[1]}, val {best[2]:.2f})   " + " ".join(f"{r[3]:.2f}" for r in rows))
