/* impair: a small user-space network impairment proxy for UDP (a stand-in for tc/netem when no root or kernel module is
 * available). Sits between a client and a server on real sockets:
 *
 *     client --UDP--> impair :LISTEN --UDP--> server :TARGET        (and back)
 *
 * Forward direction (client->server): random loss, one-way delay + jitter (kept in order), a bottleneck of RATE packets/s
 * with a drop-tail queue of QUEUE packets. Reverse direction: delay + jitter only.
 *   usage: impair LISTEN TARGET [--delay ms] [--jitter ms] [--loss pct] [--rate pps] [--queue n] [--seed n]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define RING 8192
typedef struct { double due; int len; uint8_t d[1500]; } item_t;
typedef struct { item_t *q; int h, t; double last_due, last_dep; } lane_t;

static volatile sig_atomic_t stop;
static void on_term(int sig) { (void)sig; stop = 1; }
static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
static double urand(void) { return rand() / (RAND_MAX + 1.0); }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s LISTEN TARGET [--delay ms] [--jitter ms] [--loss pct] [--rate pps] [--queue n] [--seed n]\n", argv[0]); return 1; }
    int lp = atoi(argv[1]), tp = atoi(argv[2]), queue = 50; double delay = 10, jitter = 0, loss = 0, rate = 0; unsigned seed = 1;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--delay")) delay = atof(argv[i + 1]); else if (!strcmp(argv[i], "--jitter")) jitter = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--loss")) loss = atof(argv[i + 1]); else if (!strcmp(argv[i], "--rate")) rate = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--queue")) queue = atoi(argv[i + 1]); else if (!strcmp(argv[i], "--seed")) seed = (unsigned)atoi(argv[i + 1]);
    }
    srand(seed); signal(SIGTERM, on_term); signal(SIGINT, on_term);
    int in = socket(AF_INET, SOCK_DGRAM, 0), out = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {0}, srv = {0}, cli = {0}; int have_cli = 0;
    a.sin_family = AF_INET; a.sin_addr.s_addr = inet_addr("127.0.0.1"); a.sin_port = htons((uint16_t)lp);
    if (bind(in, (struct sockaddr *)&a, sizeof a) < 0) { perror("bind"); return 1; }
    srv = a; srv.sin_port = htons((uint16_t)tp);
    lane_t fw = { calloc(RING, sizeof(item_t)), 0, 0, 0, 0 }, rv = { calloc(RING, sizeof(item_t)), 0, 0, 0, 0 };
    long drops_rand = 0, drops_queue = 0, fwd = 0;
    for (;;) {
        double now = now_ms(), next = now + 50;
        lane_t *L[2] = { &fw, &rv };
        for (int k = 0; k < 2; k++) if (L[k]->h < L[k]->t) { double d = L[k]->q[L[k]->h % RING].due; if (d < next) next = d; }
        struct pollfd p[2] = { { in, POLLIN, 0 }, { out, POLLIN, 0 } };
        int to = (int)(next - now); if (to < 0) to = 0;
        if (poll(p, 2, to) > 0) {
            for (int k = 0; k < 2; k++) if (p[k].revents & POLLIN) {
                uint8_t buf[1500]; struct sockaddr_in from; socklen_t fl = sizeof from;
                int n = (int)recvfrom(k == 0 ? in : out, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl); if (n <= 0) continue;
                double t = now_ms(); lane_t *ln = k == 0 ? &fw : &rv;
                if (k == 0) { cli = from; have_cli = 1; if (loss > 0 && urand() * 100.0 < loss) { drops_rand++; continue; } }
                double dep = t;
                if (k == 0 && rate > 0) {
                    if (ln->last_dep - t > queue * 1000.0 / rate) { drops_queue++; continue; }   /* backlog (in time) exceeds the queue */
                    dep = (ln->last_dep > t ? ln->last_dep : t) + 1000.0 / rate; ln->last_dep = dep;
                }
                double due = dep + delay + (jitter > 0 ? (urand() * 2 - 1) * jitter : 0); if (due < ln->last_due) due = ln->last_due; ln->last_due = due;
                if (ln->t - ln->h >= RING) continue;
                item_t *it = &ln->q[ln->t % RING]; it->due = due; it->len = n; memcpy(it->d, buf, (size_t)n); ln->t++;
                if (k == 0) fwd++;
            }
        }
        double t = now_ms();
        while (fw.h < fw.t && fw.q[fw.h % RING].due <= t) { item_t *it = &fw.q[fw.h++ % RING]; sendto(out, it->d, (size_t)it->len, 0, (struct sockaddr *)&srv, sizeof srv); }
        while (rv.h < rv.t && rv.q[rv.h % RING].due <= t && have_cli) { item_t *it = &rv.q[rv.h++ % RING]; sendto(in, it->d, (size_t)it->len, 0, (struct sockaddr *)&cli, sizeof cli); }
        if (stop) { fprintf(stderr, "impair: forwarded %ld, random drops %ld, queue drops %ld\n", fwd, drops_rand, drops_queue); return 0; }
    }
}
