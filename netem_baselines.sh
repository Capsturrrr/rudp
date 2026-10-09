#!/bin/bash
# Run on YOUR WSL/Linux (needs sudo + iproute2). Real kernel TCP vs plain UDP vs RUDP on the same netem path.
#   sudo bash netem_baselines.sh lossy        (or 5g, sat)   -- build first: make smart
sc=${1:-lossy}
case $sc in
  5g)    D=10;  J=3;  L=0.2; R=12mbit ;;
  lossy) D=25;  J=10; L=2;   R=6mbit  ;;
  sat)   D=150; J=10; L=0.3; R=3mbit  ;;
esac
tc qdisc del dev lo root 2>/dev/null
tc qdisc add dev lo root netem delay ${D}ms ${J}ms loss ${L}% rate $R limit 60
echo "== kernel TCP and plain UDP =="; python3 web/real_baselines.py 500
echo "== RUDP =="; ./bin/server >/dev/null 2>&1 & SP=$!
sleep 0.5
for m in aimd rl; do ./bin/smart_client --mode $m --emu 0 --scenario $sc --packets 500 --qfile results/q_$sc.txt | grep RESULT; done
kill $SP; tc qdisc del dev lo root
