/*
 * Smart-RUDP client: RUDP sender with a pluggable congestion controller.
 *   --mode aimd : rule-based slow start + AIMD (the original RUDP policy)
 *   --mode rl   : Q-learning cwnd controller (rl_cc.h)
 * Both modes share the same reliability machinery (Go-Back-N, adaptive RTO,
 * dup-ACK fast retransmit that resends the window) so only the cwnd policy differs.
 * The network path (delay, jitter, random loss, bottleneck rate, tail-drop queue) is
 * emulated inside this process, so results do not need tc/netem or root.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"
#include "rl_cc.h"

#define PKT_BYTES 40
#define MAXN 4096
#define QCAP 8192

typedef struct { double t; int len; uint8_t buf[80]; } pend_t;
typedef struct { pend_t *a; int head, tail; } ring_t;

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
static double urand(void) { return rand() / (RAND_MAX + 1.0); }
static void rpush(ring_t *r, double t, const uint8_t *b, int len) {
    pend_t *p = &r->a[r->tail % QCAP]; p->t = t; p->len = len; memcpy(p->buf, b, (size_t)len); r->tail++;
}
static pend_t *rfront(ring_t *r) { return r->head < r->tail ? &r->a[r->head % QCAP] : NULL; }


/* ---- optional pcap export (raw-IP linktype 101): what a capture at the receiver would see ---- */
static FILE *pcapf = NULL; static double pcap_epoch = 0;
static uint16_t ipsum(const uint8_t *h, int n) { uint32_t a = 0; for (int i = 0; i < n; i += 2) a += (h[i] << 8) | h[i+1]; while (a >> 16) a = (a & 0xffff) + (a >> 16); return (uint16_t)~a; }
static void pcap_open(const char *path) {
    pcapf = fopen(path, "wb"); if (!pcapf) return;
    uint32_t m = 0xa1b2c3d4; uint16_t vM = 2, vm = 4; int32_t z = 0; uint32_t sl = 65535, lt = 101;
    fwrite(&m,4,1,pcapf); fwrite(&vM,2,1,pcapf); fwrite(&vm,2,1,pcapf); fwrite(&z,4,1,pcapf); fwrite(&z,4,1,pcapf); fwrite(&sl,4,1,pcapf); fwrite(&lt,4,1,pcapf);
    pcap_epoch = 1760000000.0;  /* arbitrary fixed epoch; only relative times matter */
}
/* to_server=1: client 192.168.10.10:40000 -> server 192.168.30.10:port ; else the reverse */
static void pcap_write(double t_ms, int to_server, int port, const uint8_t *pl, int n) {
    if (!pcapf) return;
    uint8_t pk[2048]; int tot = 20 + 8 + n; memset(pk, 0, 28);
    pk[0] = 0x45; pk[2] = tot >> 8; pk[3] = tot & 255; pk[8] = 62; pk[9] = 17;
    const uint8_t c[4] = {192,168,10,10}, sv[4] = {192,168,30,10};
    memcpy(pk + 12, to_server ? c : sv, 4); memcpy(pk + 16, to_server ? sv : c, 4);
    uint16_t cs = ipsum(pk, 20); pk[10] = cs >> 8; pk[11] = cs & 255;
    uint16_t sp = to_server ? 40000 : (uint16_t)port, dp = to_server ? (uint16_t)port : 40000, ul = (uint16_t)(8 + n);
    pk[20] = sp >> 8; pk[21] = sp & 255; pk[22] = dp >> 8; pk[23] = dp & 255; pk[24] = ul >> 8; pk[25] = ul & 255;
    memcpy(pk + 28, pl, (size_t)n);
    double ts = pcap_epoch + t_ms / 1000.0; uint32_t sec = (uint32_t)ts, us = (uint32_t)((ts - sec) * 1e6), l = (uint32_t)tot;
    fwrite(&sec,4,1,pcapf); fwrite(&us,4,1,pcapf); fwrite(&l,4,1,pcapf); fwrite(&l,4,1,pcapf); fwrite(pk, 1, (size_t)tot, pcapf);
}

/* ---- emulated path parameters ---- */
static double delay_ms = 10, jitter_ms = 0, loss_pct = 0, rate_pps = 0;
static int queue_cap = 50;
static int emu = 1;
static double last_depart = 0, last_arrive_f = 0, last_arrive_r = 0;
static double depart_q[QCAP]; static int dq_h = 0, dq_t = 0;
static long drops_random = 0, drops_queue = 0;

static double jit(void) { return jitter_ms > 0 ? (urand() * 2 - 1) * jitter_ms : 0; }

/* Returns arrival time at the server, or -1 if the path drops the packet. */
static double fwd_path(double now) {
    if (!emu) return now;
    if (loss_pct > 0 && urand() * 100.0 < loss_pct) { drops_random++; return -1; }
    double depart = now;
    if (rate_pps > 0) {
        while (dq_h < dq_t && depart_q[dq_h % QCAP] <= now) dq_h++;
        if (dq_t - dq_h >= queue_cap) { drops_queue++; return -1; }
        double svc = 1000.0 / rate_pps;
        depart = (last_depart > now ? last_depart : now) + svc;
        last_depart = depart; depart_q[dq_t++ % QCAP] = depart;
    }
    double arr = depart + delay_ms + jit();
    if (arr < last_arrive_f) arr = last_arrive_f;      /* keep path in-order */
    last_arrive_f = arr; return arr;
}
static double rev_path(double now) {
    if (!emu) return now;
    double arr = now + delay_ms + jit();
    if (arr < last_arrive_r) arr = last_arrive_r;
    last_arrive_r = arr; return arr;
}

int main(int argc, char **argv) {
    const char *mode = "aimd", *scenario = "custom", *qfile = NULL, *trace = NULL, *pcapp = NULL;
    int port = SERVER_PORT, npk = 1500, train = 0; unsigned seed = 1;
    double eps = 0.0, maxcwnd = 128, ref = 0;
    for (int i = 1; i < argc; i++) {
#define ARG(n) (!strcmp(argv[i], n) && i + 1 < argc)
        if (ARG("--mode")) mode = argv[++i];
        else if (ARG("--port")) port = atoi(argv[++i]);
        else if (ARG("--delay")) delay_ms = atof(argv[++i]);
        else if (ARG("--jitter")) jitter_ms = atof(argv[++i]);
        else if (ARG("--loss")) loss_pct = atof(argv[++i]);
        else if (ARG("--rate")) rate_pps = atof(argv[++i]);
        else if (ARG("--queue")) queue_cap = atoi(argv[++i]);
        else if (ARG("--packets")) npk = atoi(argv[++i]);
        else if (ARG("--seed")) seed = (unsigned)atoi(argv[++i]);
        else if (ARG("--qfile")) qfile = argv[++i];
        else if (ARG("--train")) train = atoi(argv[++i]);
        else if (ARG("--eps")) eps = atof(argv[++i]);
        else if (ARG("--trace")) trace = argv[++i];
        else if (ARG("--emu")) emu = atoi(argv[++i]);
        else if (ARG("--scenario")) scenario = argv[++i];
        else if (ARG("--maxcwnd")) maxcwnd = atof(argv[++i]);
        else if (ARG("--pcap")) pcapp = argv[++i];
        else if (ARG("--ref")) ref = atof(argv[++i]);
        else { fprintf(stderr, "bad arg %s\n", argv[i]); return 2; }
    }
    int use_rl = !strcmp(mode, "rl");
    if (npk > MAXN) npk = MAXN;
    if (queue_cap > QCAP / 2) queue_cap = QCAP / 2;
    if (ref <= 0) ref = rate_pps > 0 ? rate_pps : 1000;
    srand(seed);
    if (pcapp) pcap_open(pcapp);

    rl_agent_t ag; int have_q = 0;
    rl_init(&ag, 0.15, 0.9, train ? eps : 0.0);
    if (use_rl && qfile && rl_load(&ag, qfile) == 0) have_q = 1;
    (void)have_q;
    ag.eps = train ? eps : 0.0;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, SERVER_IP, &sa.sin_addr);
    struct timeval tv = {1, 0}; setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    /* ---- handshake (direct, not emulated) ---- */
    uint32_t cseq = (uint32_t)rand(), sseq = 0; uint8_t buf[BUFFER_SIZE]; int ok = 0;
    for (int at = 0; at < 8 && !ok; at++) {
        rudp_packet_t syn; memset(&syn, 0, sizeof syn); syn.seq_num = cseq; syn.flags = FLAG_SYN;
        int l = rudp_pack(&syn, buf, sizeof buf);
        sendto(sockfd, buf, (size_t)l, 0, (struct sockaddr *)&sa, sizeof sa);
        ssize_t n = recvfrom(sockfd, buf, sizeof buf, 0, NULL, NULL);
        rudp_packet_t rp;
        if (n > 0 && rudp_unpack(buf, (size_t)n, &rp) == 0 && (rp.flags & FLAG_SYN) &&
            (rp.flags & FLAG_ACK) && rp.ack_num == cseq + 1) {
            sseq = rp.seq_num;
            rudp_packet_t ack; memset(&ack, 0, sizeof ack);
            ack.seq_num = cseq + 1; ack.ack_num = sseq + 1; ack.flags = FLAG_ACK;
            l = rudp_pack(&ack, buf, sizeof buf);
            sendto(sockfd, buf, (size_t)l, 0, (struct sockaddr *)&sa, sizeof sa);
            ok = 1;
        }
    }
    if (!ok) { fprintf(stderr, "handshake failed (port %d)\n", port); return 1; }

    /* ---- packets ---- */
    static rudp_packet_t pk[MAXN];
    static double sent_at[MAXN]; static uint8_t retx_flag[MAXN];
    uint32_t base_seq = cseq + 1;
    for (int i = 0; i < npk; i++) {
        memset(&pk[i], 0, sizeof pk[i]);
        pk[i].seq_num = base_seq + (uint32_t)i; pk[i].flags = FLAG_DATA; pk[i].payload_len = PKT_BYTES;
        for (int j = 0; j < PKT_BYTES; j++) pk[i].payload[j] = (uint8_t)('A' + (i + j) % 26);
    }
    ring_t fwd = { malloc(sizeof(pend_t) * QCAP), 0, 0 }, rev = { malloc(sizeof(pend_t) * QCAP), 0, 0 };

    FILE *tr = trace ? fopen(trace, "w") : NULL;
    if (tr) fprintf(tr, "time_ms,event,cwnd,rtt_ms\n");

    /* ---- sender state ---- */
    int base = 0, next = 0, dup = 0, recover = -1; uint32_t last_ack = 0;
    double cwnd = 1.0, ssthresh = 16.0, srtt = -1, rttvar = 0, rto = 1000;
    double min_rtt = 1e9, rtt_sum = 0, dev_floor = 1e9; long rtt_n = 0;
    long tx_total = 0, retx = 0, timeouts = 0, fast_retx = 0;
    double timer_start = 0; int timer_on = 0;
    /* RL interval stats */
    double iv_start = 0, iv_rtt_sum = 0; long iv_rtt_n = 0, iv_acked = 0, iv_sent = 0, iv_retx = 0;
    int prev_s = -1, prev_a = -1; double rew_sum = 0; long rew_n = 0;

    double t0 = now_ms(); iv_start = t0; timer_start = t0;
    if (tr) fprintf(tr, "0,START,%.2f,0\n", cwnd);

#define TX(i, isretx) do { \
        uint8_t b_[BUFFER_SIZE]; int l_ = rudp_pack(&pk[i], b_, sizeof b_); \
        double tn_ = now_ms(); double ar_ = fwd_path(tn_); \
        tx_total++; iv_sent++; if (isretx) { retx++; iv_retx++; retx_flag[i] = 1; } \
        sent_at[i] = tn_; \
        if (ar_ >= 0) rpush(&fwd, ar_, b_, l_); \
    } while (0)

    while (base < npk) {
        double now = now_ms();
        /* 1. release forward packets whose arrival time has come */
        pend_t *p;
        while ((p = rfront(&fwd)) && p->t <= now) {
            pcap_write(now, 1, port, p->buf, p->len);
            sendto(sockfd, p->buf, (size_t)p->len, 0, (struct sockaddr *)&sa, sizeof sa); fwd.head++;
        }
        /* 2. drain real socket (server replies) into the delayed return path */
        for (;;) {
            struct pollfd pf = { sockfd, POLLIN, 0 };
            if (poll(&pf, 1, 0) <= 0) break;
            ssize_t n = recvfrom(sockfd, buf, sizeof buf, 0, NULL, NULL);
            if (n <= 0) break;
            rpush(&rev, rev_path(now), buf, (int)n);
        }
        /* 3. process ACKs that have "arrived" */
        while ((p = rfront(&rev)) && p->t <= now) {
            rudp_packet_t ack; int okp = rudp_unpack(p->buf, (size_t)p->len, &ack) == 0;
            pcap_write(now, 0, port, p->buf, p->len); rev.head++;
            if (!okp || !(ack.flags & FLAG_ACK)) continue;
            uint32_t a = ack.ack_num;
            if (a > pk[base].seq_num) {
                int nb = base;
                while (nb < npk && pk[nb].seq_num < a) nb++;
                int newly = nb - base;
                if (!retx_flag[nb - 1]) {
                    double rtt = now - sent_at[nb - 1];
                    if (rtt < min_rtt) min_rtt = rtt;
                    rtt_sum += rtt; rtt_n++; iv_rtt_sum += rtt; iv_rtt_n++;
                    if (srtt < 0) { srtt = rtt; rttvar = rtt / 2; }
                    else { rttvar = 0.75 * rttvar + 0.25 * fabs(srtt - rtt); srtt = 0.875 * srtt + 0.125 * rtt; }
                    if (rtt_n >= 8 && rttvar < dev_floor) dev_floor = rttvar;
                    rto = srtt + 4 * rttvar; if (rto < 60) rto = 60; if (rto > 3000) rto = 3000;
                }
                iv_acked += newly; base = nb; dup = 0; last_ack = a;
                if (!use_rl) {
                    for (int k = 0; k < newly; k++) {
                        if (cwnd < ssthresh) cwnd += 1.0; else cwnd += 1.0 / cwnd;
                    }
                    if (cwnd > maxcwnd) cwnd = maxcwnd;
                }
                timer_on = base < npk; timer_start = now;
                if (tr) fprintf(tr, "%.1f,ACK,%.2f,%.1f\n", now - t0, cwnd, srtt);
            } else if (a == last_ack && a == pk[base].seq_num) {
                dup++;
                if (dup >= DUP_ACK_THRESHOLD && base > recover) {
                    recover = next - 1; fast_retx++;
                    if (!use_rl) {
                        ssthresh = cwnd / 2; if (ssthresh < 1) ssthresh = 1; cwnd = ssthresh;
                    }
                    for (int i = base; i < next; i++) TX(i, 1);
                    dup = 0; timer_start = now;
                    if (tr) fprintf(tr, "%.1f,FAST_RETX,%.2f,%.1f\n", now - t0, cwnd, srtt);
                }
            } else last_ack = a;
        }
        /* 4. RTO */
        if (timer_on && base < npk && now - timer_start >= rto) {
            timeouts++;
            ssthresh = cwnd / 2; if (ssthresh < 2) ssthresh = 2;
            if (use_rl) { cwnd = cwnd / 2 < 2 ? 2 : cwnd / 2; } else { cwnd = 1.0; }
            rto = rto * 2 > 3000 ? 3000 : rto * 2;
            recover = next - 1; dup = 0;
            for (int i = base; i < next; i++) TX(i, 1);
            timer_start = now;
            if (tr) fprintf(tr, "%.1f,TIMEOUT,%.2f,%.1f\n", now - t0, cwnd, srtt);
        }
        /* 5. RL monitor interval */
        if (use_rl) {
            double ivlen = srtt > 0 ? (srtt < 10 ? 10 : srtt) : 30;
            if (now - iv_start >= ivlen) {
                double secs = (now - iv_start) / 1000.0;
                double thr = iv_acked / secs;
                double avg_rtt = iv_rtt_n ? iv_rtt_sum / iv_rtt_n : (srtt > 0 ? srtt : 1);
                double base_rtt = min_rtt + 2 * (dev_floor < 1e8 ? dev_floor : 0);
                double ratio = min_rtt < 1e8 ? avg_rtt / base_rtt : 1.0;
                double lossf = iv_sent ? (double)iv_retx / iv_sent : 0.0;
                if (lossf > 1) lossf = 1;
                double tn = thr / ref; if (tn > 1) tn = 1;
                double reward = sqrt(tn) - 0.5 * (ratio > 1 ? ratio - 1 : 0) - 4.0 * lossf;
                if (reward > 1.5) reward = 1.5;
                int s2 = rl_state(ratio, lossf, cwnd);
                if (prev_s >= 0) {
                    if (train) rl_update(&ag, prev_s, prev_a, reward, s2);
                    rew_sum += reward; rew_n++;
                }
                int act = rl_choose(&ag, s2);
                double nc = cwnd * RL_MULT[act];
                if (RL_MULT[act] > 1.0 && nc < cwnd + 1) nc = cwnd + 1;
                if (nc < 1) { nc = 1; }
                if (nc > maxcwnd) { nc = maxcwnd; }
                cwnd = nc; prev_s = s2; prev_a = act;
                iv_start = now; iv_rtt_sum = 0; iv_rtt_n = iv_acked = iv_sent = iv_retx = 0;
                if (tr) fprintf(tr, "%.1f,RL_%d,%.2f,%.1f\n", now - t0, act, cwnd, srtt);
            }
        }
        /* 6. send within window */
        while (next < npk && next < base + (int)cwnd) {
            TX(next, 0); if (!timer_on) { timer_on = 1; timer_start = now_ms(); } next++;
        }
        /* 7. wait a little for the next event */
        struct pollfd pf = { sockfd, POLLIN, 0 };
        poll(&pf, 1, 0);
        struct timespec ts = { 0, 300000 }; nanosleep(&ts, NULL);
        if (now_ms() - t0 > 600000) { fprintf(stderr, "abort: stuck\n"); return 3; }
    }
    double done = now_ms() - t0;
    if (tr) fprintf(tr, "%.1f,DONE,%.2f,%.1f\n", done, cwnd, srtt);

    /* teardown */
    rudp_packet_t fin; memset(&fin, 0, sizeof fin);
    fin.seq_num = base_seq + (uint32_t)npk; fin.flags = FLAG_FIN;
    int l = rudp_pack(&fin, buf, sizeof buf);
    sendto(sockfd, buf, (size_t)l, 0, (struct sockaddr *)&sa, sizeof sa);
    tv.tv_sec = 0; tv.tv_usec = 200000; setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    recvfrom(sockfd, buf, sizeof buf, 0, NULL, NULL);

    if (use_rl && train && qfile) rl_save(&ag, qfile);
    printf("RESULT,%s,%s,%.1f,%d,%ld,%ld,%ld,%ld,%.2f,%.2f,%.1f,%.2f,%.4f\n",
           scenario, mode, done, npk, tx_total, retx, timeouts, fast_retx,
           rtt_n ? rtt_sum / rtt_n : 0.0, min_rtt < 1e8 ? min_rtt : 0.0,
           npk / (done / 1000.0), cwnd, rew_n ? rew_sum / rew_n : 0.0);
    if (tr) fclose(tr);
    if (pcapf) fclose(pcapf);
    close(sockfd);
    return 0;
}
