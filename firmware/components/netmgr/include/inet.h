// The Internet check's verdict per link (Plan 10 ruling 14). Pure C: netmgr's probe feeds it once a round.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define INET_MISSES 3  // rounds in a row without a reply: no Internet
#define INET_PROBE_ID 0x5B10
#define INET_TARGET0 0x08080808u  // 8.8.8.8
#define INET_TARGET1 0x01010101u  // 1.1.1.1
#include <stddef.h>

typedef struct {
    bool ok;         // taken to have Internet until it fails
    uint8_t misses;  // rounds in a row without a reply
} inet_t;

void inet_init(inet_t *s);
/** One round: replied, or not. */
void inet_result(inet_t *s, bool replied);
/** An ICMP message from the probe's receive callback: an echo reply with the probe's id and a good checksum, for this
 *  round, from the target its sequence names, received on the link its sequence names (seq = round << 2 | link << 1 |
 *  target, round on 14 bits). in_link: 0 Ethernet, 1 the cell, the netif it came in on; src: its source. *link: the link. */
bool inet_reply_ok(const uint8_t *icmp, size_t n, uint32_t src, int in_link, uint16_t round, int *link);
