/* Unit tests for the RUDP wire format: pack/unpack round trip, checksum, corruption, size limits. */
#include <stdio.h>
#include <string.h>
#include "../src/common.h"

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)

int main(void) {
    rudp_packet_t a, b; uint8_t buf[BUFFER_SIZE];
    memset(&a, 0, sizeof a);
    a.seq_num = 0xDEADBEEF; a.ack_num = 12345; a.flags = FLAG_DATA | FLAG_ACK; a.payload_len = 5; memcpy(a.payload, "hello", 5);
    int n = rudp_pack(&a, buf, sizeof buf);
    CHECK(n == RUDP_HEADER_SIZE + 5, "packed length is 13-byte header + payload");
    CHECK(RUDP_HEADER_SIZE == 13, "header is 13 bytes");
    CHECK(rudp_unpack(buf, (size_t)n, &b) == 0, "unpack accepts a valid packet");
    CHECK(b.seq_num == a.seq_num && b.ack_num == a.ack_num && b.flags == a.flags && b.payload_len == 5 && !memcmp(b.payload, "hello", 5), "round trip preserves every field");
    CHECK(buf[0] == 0xDE && buf[3] == 0xEF, "sequence number is big-endian on the wire");
    buf[RUDP_HEADER_SIZE + 2] ^= 0x01;
    CHECK(rudp_unpack(buf, (size_t)n, &b) != 0, "a flipped payload bit is rejected by the checksum");
    buf[RUDP_HEADER_SIZE + 2] ^= 0x01; buf[0] ^= 0x80;
    CHECK(rudp_unpack(buf, (size_t)n, &b) != 0, "a flipped header bit is rejected by the checksum");
    buf[0] ^= 0x80;
    CHECK(rudp_unpack(buf, 5, &b) != 0, "a truncated packet is rejected");
    a.payload_len = MAX_PAYLOAD + 1;
    CHECK(rudp_pack(&a, buf, sizeof buf) < 0, "an oversize payload is refused");
    memset(&a, 0, sizeof a); a.flags = FLAG_SYN;
    n = rudp_pack(&a, buf, sizeof buf);
    CHECK(n == RUDP_HEADER_SIZE && rudp_unpack(buf, (size_t)n, &b) == 0 && b.flags == FLAG_SYN, "an empty SYN round-trips");
    printf("%s\n", fails ? "SOME TESTS FAILED" : "all wire-format tests passed");
    return fails != 0;
}
