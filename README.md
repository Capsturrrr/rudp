# RUDP — Reliable Transport Protocol over UDP

A custom reliable transport-layer protocol implemented over raw UDP sockets in C,
replicating core TCP mechanisms: handshake, sequencing, retransmission, sliding
window flow control, and AIMD-based congestion control.

**Team:** Harshavardhan Mallela (24BAI0274), Rohit Arya (24BCE0819), Shloak Sinha (24BDS0378)

## Status
🚧 Phase 0 — project scaffold and environment setup

## Project Structure
```
rudp/
├── src/
│   ├── common.h      # shared packet struct, constants
│   ├── common.c      # packet pack/unpack, checksum
│   ├── client.c
│   └── server.c
├── tests/            # netem test scenarios
├── logs/             # runtime logs (gitignored)
├── scripts/
│   └── plot_results.py
├── docs/
│   ├── abstract.pdf
│   └── implementation-guide.md
├── Makefile
└── README.md
```

## Build
```bash
make
```

## Run
```bash
./bin/server
./bin/client
```

## Roadmap
- [x] Phase 0 — Setup & environment
- [ ] Phase 1 — Raw UDP echo
- [ ] Phase 2 — Packet format & sequencing
- [ ] Phase 3 — Connection establishment (handshake)
- [ ] Phase 4 — Reliable delivery (sliding window, retransmission)
- [ ] Phase 5 — Congestion control (AIMD + Cubic-inspired variant)
- [ ] Phase 6 — Testing & benchmarking (tc/netem, Wireshark)
- [ ] Phase 7 — Visualization & report
