#include "ipacl.h"

#include <stddef.h>
#include <string.h>

/** Decimal 0..max from `*p`, at most `digits` digits; advances `*p`. */
static bool number(const char **p, int digits, unsigned max, unsigned *out)
{
    unsigned v = 0;
    int k = 0;
    while (k < digits && **p >= '0' && **p <= '9') v = v * 10 + (unsigned)(*(*p)++ - '0'), k++;
    *out = v;
    return k > 0 && v <= max;
}

static bool entry(const char *p, const char *end, uint32_t *net, uint32_t *mask)
{
    uint32_t ip = 0;
    for (int i = 0; i < 4; i++) {
        unsigned b;
        if (i && (p >= end || *p++ != '.')) return false;
        if (!number(&p, 3, 255, &b)) return false;
        ip = ip << 8 | b;
    }
    unsigned bits = 32;
    if (p < end && *p == '/') {
        p++;
        if (!number(&p, 2, 32, &bits)) return false;
    }
    if (p != end) return false;
    *mask = bits ? 0xFFFFFFFFu << (32 - bits) : 0;
    *net = ip & *mask;
    return true;
}

static bool sep(char c) { return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool ipacl_parse(const char *s, ipacl_t *out)
{
    out->n = 0;
    if (!s) return true;
    while (*s) {
        while (*s && sep(*s)) s++;
        if (!*s) break;
        const char *end = s;
        while (*end && !sep(*end)) end++;
        if (out->n >= IPACL_MAX || !entry(s, end, &out->r[out->n].net, &out->r[out->n].mask)) {
            out->n = 0;
            return false;
        }
        out->n++;
        s = end;
    }
    return true;
}

bool ipacl_allows(const ipacl_t *acl, uint32_t ip)
{
    if (acl->n == 0) return true;
    for (int i = 0; i < acl->n; i++) {
        if ((ip & acl->r[i].mask) == acl->r[i].net) return true;
    }
    return false;
}
