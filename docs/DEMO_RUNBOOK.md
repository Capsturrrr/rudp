# Demo run-of-show (about 8-10 minutes)

Before starting: Packet Tracer open with the saved `.pkt`; a WSL terminal in the repo; report PDF and slides open.
Build once: `make all smart chat`.

1. **Idea (1 min)** - slides 2-3. UDP unreliable, TCP locked in the kernel, we rebuilt TCP's core ideas over UDP.
2. **RUDP live (2 min)** - terminal 1 `./bin/server`, terminal 2 `./bin/client 20` (20% simulated loss). Show retransmissions, "All 62 packets delivered", reassembled message.
3. **Chat (2 min)** - `./bin/chat 9001 9002 Alice 30` and `./bin/chat 9002 9001 Bob 30`. Type messages; point at the "network lost" and "timeout resending" lines while messages still arrive in order. Start both terminals before typing.
4. **Smart-RUDP (1 min)** - comparison graph (`smart_graphs/`). Honest result: matched AIMD, slower on two of three paths, somewhat fewer retransmissions.
5. **Network (2 min)** - Packet Tracer topology; R2 `show ip ospf neighbor` (3 FULL); PC0 `tracert 192.168.30.10`; ACL block (`ping 192.168.20.11` fails); failover (`ping -t`, shut R1 Gi0/2, tracert shows the R2 path). Restore with `no shutdown`.
6. **Wireshark (1 min)** - open `rudp_capture_loss.pcapng` with `network/rudp.lua`; `frame.number <= 3` (handshake); `smartrudp.seq == 1327198699` (same segment sent at 0.0004 s and 0.501 s: timeout + Go-Back-N resend).

Wireshark launch from WSL:
`"/mnt/c/Program Files/Wireshark/Wireshark.exe" -X 'lua_script:C:\Users\Hanuman\Downloads\rudp_wireshark\rudp.lua' 'C:\Users\Hanuman\Downloads\rudp_wireshark\rudp_capture_loss.pcapng'`

Honest limits: Packet Tracer cannot run the C programs, so the protocol and the network are validated separately;
loss in the experiments and in the chat is emulated in the program; Smart-RUDP did not beat a well-tuned AIMD.
