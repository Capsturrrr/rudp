import os, sys, random, statistics as st
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc
from dqn import DQN
from sim import run
from stress import sample_path
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results")
NET = DQN(); NET.load(os.path.join(R, os.environ.get("NETF", "dqn_chat.json")))
def one(a):
    p, cut = a
    cc.TIMEOUT_CUT = cut if cut != "aimd" else 0
    ts = []
    for sd in range(3):
        c = cc.Controller("aimd" if cut == "aimd" else "deep", None, net=NET); c.learn = False
        ts.append(run(c, p["loss"], p["delay"], n_msgs=p["n"], rate=p["rate"], queue=p["queue"], seed=900 + sd, jitter=p["jitter"], max_t=120))
    return st.mean(ts)
if __name__ == "__main__":
    seed, n, npaths = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    rng = random.Random(seed); paths = [dict(sample_path(rng), n=n) for _ in range(npaths)]
    arms = ["aimd", 0, 0.5, 0.25, 0.1]
    with Pool(2) as pool: out = {a: pool.map(one, [(p, a) for p in paths]) for a in arms}
    print(f"seed {seed}, {npaths} paths, {n} packets")
    for a in arms:
        w = sum(1 for x, y in zip(out[a], out["aimd"]) if x < y * .95); l = sum(1 for x, y in zip(out[a], out["aimd"]) if x > y * 1.05)
        print(f"  {'aimd' if a=='aimd' else 'deep cut='+str(a):14s} mean {st.mean(out[a]):7.2f} median {st.median(out[a]):6.2f} vs aimd faster {w} slower {l}")
