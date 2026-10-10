import os, sys, random, statistics as st
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc
from dqn import DQN
from sim import run
from stress import sample_path
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results")
NETS = {}
for k, f in (("orig", "dqn_chat.json"), ("v2", "dqn_v2.json"), ("v3", "dqn_v3.json")):
    n = DQN(); n.load(os.path.join(R, f)); NETS[k] = n
ARMS = {"fixed": ("fixed", None, 0), "aimd": ("aimd", None, 0), "orig+cut": ("deep", "orig", .25), "v2": ("deep", "v2", 0), "v3+cut": ("deep", "v3", .25)}
def one(a):
    p, arm = a
    mode, net, cut = ARMS[arm]; cc.TIMEOUT_CUT = cut
    ts = []
    for sd in range(3):
        c = cc.Controller(mode, None, net=NETS.get(net)); c.learn = False
        ts.append(run(c, p["loss"], p["delay"], n_msgs=p["n"], rate=p["rate"], queue=p["queue"], seed=900 + sd, jitter=p["jitter"], max_t=120))
    return st.mean(ts)
if __name__ == "__main__":
    seed, n, npaths = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    rng = random.Random(seed); paths = [dict(sample_path(rng), n=n) for _ in range(npaths)]
    with Pool(2) as pool: out = {a: pool.map(one, [(p, a) for p in paths]) for a in ARMS}
    print(f"seed {seed}, {npaths} paths, {n} packets (sim v2)")
    for a in ARMS:
        w = sum(1 for x, y in zip(out[a], out["aimd"]) if x < y * .95); l = sum(1 for x, y in zip(out[a], out["aimd"]) if x > y * 1.05)
        print(f"  {a:9s} mean {st.mean(out[a]):7.2f} median {st.median(out[a]):6.2f}  vs aimd faster {w} slower {l}")
