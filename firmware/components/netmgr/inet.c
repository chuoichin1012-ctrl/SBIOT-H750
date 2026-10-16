// The Internet check's verdict per link (Plan 10 ruling 14). See inet.h.
#include "inet.h"
#include <stdint.h>

void inet_init(inet_t *s)
{
    s->ok = true;
    s->misses = 0;
}

void inet_result(inet_t *s, bool replied)
{
    if (replied) {
        s->ok = true;
        s->misses = 0;
    } else if (s->misses < INET_MISSES && ++s->misses == INET_MISSES) {
        s->ok = false;
    }
}

bool inet_reply_ok(const uint8_t *icmp, size_t n, uint32_t src, int in_link, uint16_t round, int *link)
{
    if (n < 8 || icmp[0] != 0 || icmp[1] != 0)
        return false;  // an echo reply only
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < n; i += 2)
        sum += (uint32_t)icmp[i] << 8 | icmp[i + 1];
    if (n & 1)
        sum += (uint32_t)icmp[n - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    if (sum != 0xFFFF)
        return false;  // the checksum
    uint16_t id = (uint16_t)(icmp[4] << 8 | icmp[5]), seq = (uint16_t)(icmp[6] << 8 | icmp[7]);
    if (id != INET_PROBE_ID || (seq >> 2) != (round & 0x3FFF))
        return false;
    int l = (seq >> 1) & 1, t = seq & 1;
    if (l != in_link || src != (t ? INET_TARGET1 : INET_TARGET0))
        return false;
    *link = l;
    return true;
}
