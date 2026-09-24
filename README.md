# RUDP — Reliable Transport Protocol over UDP

A custom reliable transport-layer protocol implemented over raw UDP sockets in C,
replicating core TCP mechanisms: three-way handshake, sequencing, retransmission,
sliding window flow control, and AIMD-based congestion control.

**Team:** Harshavardhan Mallela (24BAI0274), Rohit Arya (24BCE0819), Shloak Sinha (24BDS0378)
**Course:** Computer Networks — lab project (Cisco/Wireshark experiment + implementation)

## Project Status: Phases 0–7 complete

- [x] Phase 0 — Environment, toolchain, repo setup
- [x] Phase 1 — Raw UDP echo (client/server communicating)
- [x] Phase 2 — Custom packet format, pack/unpack, checksum
- [x] Phase 3 — Three-way handshake, connection state machine
- [x] Phase 4 — Reliable delivery: sliding window, Go-Back-N, retransmission timeout, fast retransmit
- [x] Phase 5 — AIMD congestion control: slow start, congestion avoidance, timeout/fast-retransmit response
- [x] Phase 6 — Real network emulation (tc/netem) + Wireshark packet capture
- [x] Phase 7 — Benchmarking + visualization (cwnd graphs, loss-vs-duration graphs)

Still to do: Cisco Packet Tracer topology (separate deliverable, see `docs/`), case study +
video submission (due 21st Oct), final written report.

## Setup (one-time, per machine)

This project requires a Linux environment. On Windows, use **WSL2**:

```bash
# In an Administrator PowerShell:
wsl --install -d Ubuntu
# Restart if prompted, then set up your Ubuntu username/password on first launch.
```

Inside Ubuntu/WSL:
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential gcc gdb valgrind iproute2 git python3-pip
pip install matplotlib --break-system-packages
```

**Wireshark** should be installed natively on Windows (not inside WSL) — download from
wireshark.org. Packet capture on WSL's loopback interface is done via `tshark` (installed
below), with the resulting `.pcapng` file copied to Windows for viewing in the Wireshark GUI.

```bash
sudo apt install -y tshark   # say Yes to the "non-superusers capture packets" prompt
```

## Clone and build

```bash
git clone https://github.com/Capsturrrr/rudp.git ~/projects/rudp
cd ~/projects/rudp
make
```

## Running it

**Terminal 1 (server):**
```bash
./bin/server
```

**Terminal 2 (client):**
```bash
./bin/client [loss_percent]
```
`loss_percent` is optional (default 0) — simulates packet loss client-side to test
retransmission/congestion-control behavior without needing real network emulation.
Example: `./bin/client 20` simulates 20% packet loss.

The client sends a ~2.4KB test message split into 40-byte chunks, using the sliding
window + AIMD congestion control. Watch for `TIMEOUT`, `FAST RETRANSMIT`, and `cwnd=`
lines in the output — that's the reliability/congestion logic reacting live.

## Testing under real network conditions (tc/netem)

For more realistic testing than the built-in `loss_percent` simulator:

```bash
sudo tc qdisc add dev lo root netem loss 15%     # apply 15% loss on loopback
./bin/client 0                                    # 0% built-in loss — netem provides the real loss
sudo tc qdisc del dev lo root netem                # IMPORTANT: remove when done, or it persists
```

Other useful profiles: `delay 100ms`, `delay 100ms 20ms` (jitter), `corrupt 1%`, `duplicate 1%`.

**Always run `tc qdisc del dev lo root netem` when finished** — a leftover rule silently
affects every future test and can make things look broken when they aren't.

## Capturing packets with Wireshark

Wireshark's Windows GUI can't see traffic inside WSL's internal loopback. Capture with
`tshark` from inside WSL instead, then move the file to Windows to view:

```bash
sudo tshark -i lo -f "udp port 8888" -w /tmp/capture.pcapng
# (in other terminals, run server + client as usual)
# Ctrl+C to stop capturing, then:
sudo cp /tmp/capture.pcapng ~/capture.pcapng
sudo chown $USER:$USER ~/capture.pcapng
cp ~/capture.pcapng "/mnt/c/Users/<YourWindowsUsername>/Downloads/"
```
Then open it in Wireshark on Windows via File → Open. Filter with `udp.port == 8888`.

## Generating graphs (Phase 7)

```bash
python3 plot_cwnd.py logs/cwnd_log.csv cwnd_graph.png
```
Produces a congestion-window-over-time graph with timeout/fast-retransmit events marked —
generated automatically after every client run (logged to `logs/cwnd_log.csv`).

```bash
chmod +x benchmark.sh
./benchmark.sh   # requires server running separately; sweeps loss 0–40%, ~1 min
```
Produces `logs/benchmark_results.csv` — duration and final cwnd at each loss level.

## Known gotchas (things that bit us during development)

- **Server state machine**: originally only accepted a new connection while in `LISTEN`
  state. If a client got killed mid-session (Ctrl+C, crash), the server would get stuck
  and silently reject all future connections. Fixed — the server now accepts a fresh SYN
  in any state. If you ever see repeated handshake failures for no reason, restart the
  server fresh as a first troubleshooting step.
- **Leftover netem rules** persist across terminal sessions and stack with each other —
  always `tc qdisc show dev lo` to check before assuming a test result is "real."
- **`grep -oP`** (PCRE regex) isn't reliably available in all WSL setups — the benchmark
  script uses plain `sed`/`grep` instead for portability.

## Project Structure
```
rudp/
├── src/
│   ├── common.h        # packet struct, protocol constants, AIMD tuning
│   ├── common.c         # pack/unpack, checksum
│   ├── client.c          # sliding window sender + congestion control
│   └── server.c          # Go-Back-N receiver + connection state machine
├── logs/                  # cwnd_log.csv, benchmark_results.csv (generated at runtime)
├── plot_cwnd.py           # generates congestion window graph
├── benchmark.sh           # sweeps loss levels, times each transfer
├── docs/
│   ├── abstract.pdf
│   └── implementation-guide.md
├── Makefile
└── README.md
```
