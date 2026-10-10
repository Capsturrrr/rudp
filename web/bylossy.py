import os, sys, random, statistics as st
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import guard_eval as g, cc
from stress import sample_path
rng = random.Random(555); paths = [sample_path(rng) for _ in range(120)]
with Pool(2) as pool:
    R = {m: pool.map(g.one, [(p, 0, m) for p in paths]) for m in ("aimd", "fixed", "deep")}
for lo, hi in ((0, .01), (.01, .08), (.08, .16), (.16, .31)):
    idx = [i for i, p in enumerate(paths) if lo <= p["loss"] < hi]
    print(f"loss {lo:.2f}-{hi:.2f} n={len(idx):3d} " + " ".join(f"{m} {st.mean(R[m][i] for i in idx):6.2f}" for m in R))
