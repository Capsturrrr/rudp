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

Extensions (done):

- [x] **Smart-RUDP** — tabular Q-learning congestion controller compared with AIMD on three emulated paths (`src/smart_client.c`, `src/rl_cc.h`)
- [x] **Network design** — four-router OSPF network in Cisco Packet Tracer: VLANs, DHCP, ACL, link failover tested (`network/`)
- [x] **Wireshark** — Lua dissector for the RUDP header and exported traces (`network/rudp.lua`, `network/*.pcap`)
- [x] Final report and slides in `docs/final/`

Still to do: case study + video submission (due 21st Oct).

## Selective Repeat is the default

The receiver buffers out-of-order packets (64-packet window) and puts a bitmap of what it holds in every ACK; the sender keeps one timer per packet and resends only the missing ones. Go-Back-N is kept for comparison: `--gbn` (smart_client, gateway), `RUDP_GBN=1` (client, chat), `make server_gbn` / `-DRUDP_GBN` (server). Evidence: `eval_ci7.sh`, `results/eval_ci7_summary.txt`; earlier `eval_ci*` results were Go-Back-N.

## Smart-RUDP (learning-based congestion control)

Performance defaults: the neural agent runs with a window cap of 128 and an AIMD hand-over at 10% loss (`--maxcwnd 32 --hybrid 0` or `RUDP_CLASSIC=1` for the original settings); the receive window is 256 packets. Evidence: `eval_perf*.sh`, `results/eval_perf3_summary.txt`.

```bash
make smart                      # builds bin/server and bin/smart_client
./run_experiments.sh            # trains 40 episodes, evaluates 6 runs per scenario
python3 analyze.py              # writes smart_graphs/*.png and summary.csv
```

Modes: `--mode aimd|rl|rl2|deep`. They share the same reliability code; only the window policy differs.
`rl` is the original table agent, `rl2` is the chat-style table agent (no-shrink rule, window cap 32) and
`deep` is a small neural network (`src/deep_cc.h`, weights in `results/dqn_chat.txt`, trained by `web/train_deep.py`).
`--init N` sets the initial window (the chat-style modes use 10; give AIMD `--init 10` for a fair comparison).
Selective acknowledgments: build `make sack`, run the client with `--sack 1` against `bin/server_sack`.
`--reorder 1` lets jitter reorder packets on the emulated path.
The path (delay, jitter, loss, bottleneck rate, queue) is emulated inside the client.
Result in short: Smart-RUDP matched AIMD on the lossy path and was slower on the 5G-like and
satellite-like paths, with somewhat fewer retransmissions. Raw results are in `results/`.
`netem_scenarios.sh` repeats the scenarios with kernel tc/netem (needs root, not used in the report).

## Tests

```bash
make test        # wire format, end-to-end delivery over UDP, all controller modes, SACK, simulator
```

## More experiments (simulated, Python)

`python3 web/stress.py` (48 unseen paths), `web/baselines.py` (plain UDP and a TCP model), `web/sack_test.py`,
`web/reorder_test.py`, `web/train_deep.py`. Repeated C runs with confidence intervals: `eval_ci.sh`, `eval_ci2.sh`,
`eval_ci3.sh` with `ci_summary*.py`.
`web/fairness_test.py` (two flows sharing a bottleneck, Jain index; results in `results/fairness.txt`) and
`eval_ci4.sh` + `ci_summary4.py` (C transport with equal window caps), `RUDP_RG=1` (simulator with the realistic fast-retransmit rule; results/stress_rg.txt), and
`--hybrid 0.10` (neural agent hands over to AIMD under heavy loss; `eval_ci5.sh`, `eval_ci6.sh`, results/eval_ci6_summary.txt), and
`web/ablate.py` (retrains the neural agent with one design choice removed; `results/ablation.txt`). Real kernel TCP/UDP on netem (Linux, root): `netem_baselines.sh`.

## RUDP Chat (demo application)

A two-way messenger built on the RUDP packet format (`src/chat.c`): per-message sequence numbers,
cumulative ACKs, Go-Back-N window, timeout and fast retransmit, with simulated packet loss.

```bash
make chat
./bin/chat 9001 9002 Alice 30     # terminal 1:  my_port peer_port name loss%
./bin/chat 9002 9001 Bob   30     # terminal 2
```

Type in either terminal. Lines in grey brackets show what the network lost and what RUDP did about it;
every message still arrives, complete and in order. Ctrl+D quits and prints statistics.

The web version (`web/rudp_web.py`, see `docs/CHAT_ONLINE.txt`) adds a live dashboard (window, in flight, RTT, loss over
the last 60 s) and `--rto adaptive` for an RFC 6298 retransmission timer with backoff (default stays fixed 300 ms).

## Network design (Cisco Packet Tracer)

Four routers (R1-R4), OSPF area 0, two VLANs with router-on-a-stick, DHCP on R1 and R4, and an
extended ACL blocking students from staff. Device configs are in `network/cfg/`; the diagram is
`network/topology_final.png`. Tested: OSPF neighbours, routes (metric 2 to the server LAN, 3 to the
branch LAN), tracert, ACL block, and failover (70 of 71 pings during a link shutdown).
Packet Tracer cannot run the C programs, so the protocol and the network are validated separately.

## Wireshark

`network/rudp.lua` is a dissector (`wireshark -X lua_script:network/rudp.lua`). See
`network/WIRESHARK_STEPS.txt`. `network/smart_lossy_*.pcap` are traces exported by the client (`--pcap`).

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
