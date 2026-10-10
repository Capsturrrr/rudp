import os, sys, random, statistics as st
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc
from dqn import DQN
from sim import run
from stress import sample_path
NET = DQN(); NET.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results", "dqn_chat.json"))
RG = os.environ.get('RG') == '1'
def one(a):
    p, thr, mode = a
    cc.RANDOM_LOSS_RATIO = thr
    ts = []
    for sd in range(3):
        c = cc.Controller(mode, None, net=NET if mode == "deep" else None); c.learn = False
        ts.append(run(c, p["loss"], p["delay"], n_msgs=p["n"], rate=p["rate"], queue=p["queue"], seed=900 + sd, jitter=p["jitter"], recover_guard=RG))
    return st.mean(ts)
def go(seed, n, thrs):
    rng = random.Random(seed); paths = [sample_path(rng) for _ in range(n)]
    out = {}
    with Pool(2) as pool:
        out["aimd"] = pool.map(one, [(p, 0, "aimd") for p in paths])
        out["fixed"] = pool.map(one, [(p, 0, "fixed") for p in paths])
        for t in thrs: out[f"deep r<{t}"] = pool.map(one, [(p, t, "deep") for p in paths])
    for k, v in out.items():
        w = sum(1 for a, b in zip(v, out["aimd"]) if a < b * .95); l = sum(1 for a, b in zip(v, out["aimd"]) if a > b * 1.05)
        print(f"  {k:14s} mean {st.mean(v):6.2f} median {st.median(v):5.2f}  vs aimd faster {w} slower {l}")
    return out
if __name__ == "__main__":
    thrs = [0.0, 1.05, 1.1, 1.15, 1.25, 1.5]
    print("validation (seed 777, 60 paths)"); go(777, 60, thrs)
