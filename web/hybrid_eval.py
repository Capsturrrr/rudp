import os, sys, random, statistics as st
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc
from dqn import DQN
from sim import run
from stress import sample_path
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results")
NET = DQN(); NET.load(os.path.join(R, "dqn_chat.json"))
ARMS = {"fixed": ("fixed", 0), "aimd": ("aimd", 0), "deep": ("deep", 0), "hybrid 0.05": ("deep", .05), "hybrid 0.10": ("deep", .10)}
def one(a):
    p, arm = a; mode, h = ARMS[arm]; cc.HYBRID = h
    ts = []
    for sd in range(3):
        c = cc.Controller(mode, None, net=NET if mode == "deep" else None); c.learn = False
        ts.append(run(c, p["loss"], p["delay"], n_msgs=p["n"], rate=p["rate"], queue=p["queue"], seed=900 + sd, jitter=p["jitter"], max_t=120))
    return st.mean(ts)
if __name__ == "__main__":
    n = int(sys.argv[1]); rng = random.Random(2468); paths = [dict(sample_path(rng), n=n) for _ in range(70)]
    with Pool(2) as pool: out = {a: pool.map(one, [(p, a) for p in paths]) for a in ARMS}
    print(f"70 paths, {n} packets, simulator v2 (in-order jitter, realistic recovery, adaptive RTO)")
    for a in ARMS:
        w = sum(1 for x, y in zip(out[a], out["aimd"]) if x < y * .95); l = sum(1 for x, y in zip(out[a], out["aimd"]) if x > y * 1.05)
        print(f"  {a:12s} mean {st.mean(out[a]):7.2f} median {st.median(out[a]):6.2f} vs aimd faster {w} slower {l}")
    for lo, hi in ((0, .011), (.011, .08), (.08, .16), (.16, .31)):
        idx = [i for i, p in enumerate(paths) if lo <= p["loss"] < hi]
        print(f"  loss {lo:.3f}-{hi:.3f} n={len(idx):2d} " + " ".join(f"{a} {st.mean(out[a][i] for i in idx):6.1f}" for a in ARMS))
