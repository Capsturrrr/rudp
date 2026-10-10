#!/usr/bin/env python3
"""Generalisation test on randomly drawn paths (virtual-time C sender). Each path draws delay, jitter, loss, bottleneck rate and queue
from wide ranges, so the result does not depend on the five hand-picked scenarios. Usage: vt_random.py [n_paths] [seed0] [dfile]"""
import math, random, sys, os, statistics as st
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vt

def draw(rng):
    d = rng.uniform(5, 150); j = rng.uniform(0, 0.25 * d)
    loss = math.exp(rng.uniform(math.log(0.05), math.log(8)))
    rate = math.exp(rng.uniform(math.log(300), math.log(4000)))
    q = rng.randint(20, 300); n = rng.choice([600, 1200, 2000])
    return f"--delay {d:.0f} --jitter {j:.0f} --loss {loss:.2f} --rate {rate:.0f} --queue {q}", n

def evaluate(arms, paths, seed0=1, extra=""):
    """returns {arm: [time per path]} (mean of 2 seeds) and retx"""
    res = {a: [] for a in arms}
    for i, (pa, n) in enumerate(paths):
        for a in arms:
            r = [x for x in (vt.run(a, pa, seed0 + i * 7 + k, npk=n) for k in range(2)) if x]
            res[a].append((st.mean(x["time"] for x in r), st.mean(x["retx"] for x in r) / n) if r else (float("nan"), float("nan")))
    return res

if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    seed0 = int(sys.argv[2]) if len(sys.argv) > 2 else 424242
    rng = random.Random(seed0); paths = [draw(rng) for _ in range(n)]
    arms = ["aimd_128", "cubic_128", "deep"]
    if len(sys.argv) > 3:
        vt.ARMS["deep2"] = "--mode deep --init 10 --dfile " + sys.argv[3]; arms.append("deep2")
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(os.cpu_count() or 2) as ex:
        chunks = [paths[i::8] for i in range(8)]
        parts = list(ex.map(lambda c: evaluate(arms, c), chunks))
    res = {a: [] for a in arms}
    order = [i for k in range(8) for i in range(k, n, 8)]
    flat = {a: [None] * n for a in arms}
    for k, part in enumerate(parts):
        for a in arms:
            for j, v in enumerate(part[a]): flat[a][k + 8 * j] = v
    base = flat["aimd_128"]
    print(f"{n} random paths, 2 seeds each; ratio = completion time / tuned AIMD (lower is better)")
    print(f"{'policy':10s} {'geo-mean ratio':>15s} {'median':>8s} {'worst':>7s} {'faster on':>10s} {'>20% slower on':>15s} {'retx/pkt':>9s}")
    for a in arms:
        rat = [flat[a][i][0] / base[i][0] for i in range(n)]
        g = math.exp(st.mean(math.log(r) for r in rat))
        print(f"{a:10s} {g:15.3f} {st.median(rat):8.3f} {max(rat):7.2f} {sum(r < 0.95 for r in rat):7d}/{n} {sum(r > 1.2 for r in rat):10d}/{n} {st.mean(flat[a][i][1] for i in range(n)):9.3f}")
