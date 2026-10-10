#!/usr/bin/env python3
"""Export results/dqn_chat.json to results/dqn_chat.txt for the C agent (src/deep_cc.h)."""
import json, os, sys
h = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else "dqn_chat"   # e.g. dqn_chat_rg
d = json.load(open(os.path.join(h, "..", "results", src + ".json")))
with open(os.path.join(h, "..", "results", src + ".txt"), "w") as f:
    for k in ("W1", "b1", "W2", "b2", "W3", "b3"):
        flat = [x for row in d[k] for x in (row if isinstance(row, list) else [row])]
        f.write(" ".join(f"{x:.9g}" for x in flat) + "\n")
print("wrote results/dqn_chat.txt")
