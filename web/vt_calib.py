#!/usr/bin/env python3
"""Calibration: does the virtual-time C sender reproduce the real-time results? Compares 40 virtual-time seeds per cell with
the 8-10 real-time runs of results/eval_perf4_*.csv (in-process emulator, real clock, real server process)."""
import csv, os, sys, statistics as st
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vt
R = os.path.join(vt.ROOT, "results")
print(f"{'path':6s} {'arm':9s} {'virtual (n=40)':>20s} {'real-time':>20s}  agree")
for path in vt.PATHS:
    real = {}
    f = os.path.join(R, f"eval_perf4_{path}.csv")
    for r in csv.reader(open(f)):
        if len(r) > 5: real.setdefault(r[2], []).append(float(r[3]) / 1000)
    for arm in ("aimd_32", "aimd_128", "deep"):
        v = [x["time"] for x in vt.sweep(arm, path, range(9801, 9841))]
        m, h = vt.mean_ci(v); rm, rh = vt.mean_ci(real[arm])
        ok = abs(m - rm) <= h + rh
        print(f"{path:6s} {arm:9s} {m:8.2f} +/- {h:5.2f}  {rm:8.2f} +/- {rh:5.2f}   {'yes' if ok else 'NO'}")
