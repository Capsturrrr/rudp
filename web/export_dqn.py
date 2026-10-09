#!/usr/bin/env python3
"""Export results/dqn_chat.json to results/dqn_chat.txt for the C agent (src/deep_cc.h)."""
import json, os
h = os.path.dirname(os.path.abspath(__file__))
d = json.load(open(os.path.join(h, "..", "results", "dqn_chat.json")))
with open(os.path.join(h, "..", "results", "dqn_chat.txt"), "w") as f:
    for k in ("W1", "b1", "W2", "b2", "W3", "b3"):
        flat = [x for row in d[k] for x in (row if isinstance(row, list) else [row])]
        f.write(" ".join(f"{x:.9g}" for x in flat) + "\n")
print("wrote results/dqn_chat.txt")
