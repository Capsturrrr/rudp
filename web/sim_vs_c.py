import os, sys, statistics as st
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc
from dqn import DQN
from sim import run
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results")
nets = {}
for k, f in (("deep", "dqn_chat.json"), ("deeprg", "dqn_chat_rg.json")):
    n = DQN(); n.load(os.path.join(R, f)); nets[k] = n
PATHS = {"5g": dict(delay=.010, jitter=.003, loss=.002, rate=1500, queue=60), "lossy": dict(delay=.025, jitter=.010, loss=.02, rate=800, queue=50), "sat": dict(delay=.150, jitter=.010, loss=.003, rate=400, queue=100)}
for rg in (True,):
    print("realistic recovery" if rg else "permissive")
    for sc, p in PATHS.items():
        out = {}
        for m in ("aimd", "deep", "deeprg"):
            ts = []
            for sd in range(10):
                c = cc.Controller("aimd" if m == "aimd" else "deep", None, net=nets.get(m)); c.learn = False
                if m == "aimd": c.cwnd = 10.0
                ts.append(run(c, p["loss"], p["delay"], n_msgs=1200, rate=p["rate"], queue=p["queue"], seed=sd, jitter=p["jitter"], max_t=120, recover_guard=rg))
            out[m] = st.mean(ts)
        print(f"  {sc:6s} " + "  ".join(f"{k} {v:6.2f}" for k, v in out.items()))
