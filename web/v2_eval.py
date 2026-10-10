import os, sys, random, statistics as st
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc
from dqn import DQN
from sim import run
from stress import sample_path
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results")
NETS = {}
for k, f in (("orig", "dqn_chat.json"), ("rg", "dqn_chat_rg.json"), ("v2", "dqn_v2.json")):
    n = DQN(); n.load(os.path.join(R, f)); NETS[k] = n
def one(a):
    p, arm = a
    ts = []
    for sd in range(3):
        if arm in NETS: c = cc.Controller("deep", None, net=NETS[arm])
        else: c = cc.Controller(arm, None)
        c.learn = False
        ts.append(run(c, p["loss"], p["delay"], n_msgs=p["n"], rate=p["rate"], queue=p["queue"], seed=900 + sd, jitter=p["jitter"], max_t=120))
    return st.mean(ts)
if __name__ == "__main__":
    seed = int(sys.argv[1]); n_msgs = int(sys.argv[2]); npaths = int(sys.argv[3])
    rng = random.Random(seed); paths = [dict(sample_path(rng), n=n_msgs) for _ in range(npaths)]
    arms = ("fixed", "aimd", "orig", "rg", "v2")
    with Pool(2) as pool: out = {a: pool.map(one, [(p, a) for p in paths]) for a in arms}
    print(f"seed {seed}, {npaths} paths, {n_msgs} packets each (sim v2)")
    for a in arms:
        w = sum(1 for x, y in zip(out[a], out["aimd"]) if x < y * .95); l = sum(1 for x, y in zip(out[a], out["aimd"]) if x > y * 1.05)
        print(f"  {a:6s} mean {st.mean(out[a]):7.2f} median {st.median(out[a]):6.2f}  vs aimd faster {w} slower {l}")
