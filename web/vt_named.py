#!/usr/bin/env python3
"""The five named paths with 100 paired seeds each in the virtual-time C sender (tight intervals). Paired differences against each baseline."""
import os, sys, statistics as st
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vt
SEEDS = range(20001, 20101); ARMS = ["aimd_32", "aimd_128", "cubic_128", "deep"]
print(f"{'path':6s} " + " ".join(f"{a:>18s}" for a in ARMS) + "    deep vs aimd_128      deep vs cubic_128")
for path in vt.PATHS:
    runs = {a: vt.sweep(a, path, SEEDS) for a in ARMS}
    cells = []
    for a in ARMS:
        m, h = vt.mean_ci([r["time"] for r in runs[a]]); cells.append(f"{m:8.2f} +/- {h:5.2f} s")
    out = []
    for ref in ("aimd_128", "cubic_128"):
        d = [x["time"] - y["time"] for x, y in zip(runs["deep"], runs[ref])]; m, h = vt.mean_ci(d)
        pct = 100 * m / st.mean(r["time"] for r in runs[ref])
        out.append(f"{pct:+5.0f}% ({'sig.' if abs(m) > h else 'n.s.'})")
    print(f"{path:6s} " + " ".join(f"{c:>18s}" for c in cells) + "    " + "   ".join(f"{o:>18s}" for o in out), flush=True)
