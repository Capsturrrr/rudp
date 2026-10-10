import os, sys, random, statistics as st
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import v3_eval as v
from stress import sample_path
v.ARMS["orig"] = ("deep", "orig", 0)
if __name__ == "__main__":
    rng = random.Random(2468); paths = [dict(sample_path(rng), n=300) for _ in range(70)]
    arms = ("fixed", "aimd", "orig", "orig+cut")
    with Pool(2) as pool: out = {a: pool.map(v.one, [(p, a) for p in paths]) for a in arms}
    print("70 paths, 300 packets, sim v2 (calibrated to C), mean seconds by loss level")
    for lo, hi in ((0, .011), (.011, .08), (.08, .16), (.16, .31)):
        idx = [i for i, p in enumerate(paths) if lo <= p["loss"] < hi]
        print(f"  loss {lo:.3f}-{hi:.3f} n={len(idx):2d} " + " ".join(f"{a} {st.mean(out[a][i] for i in idx):6.1f}" for a in arms))
