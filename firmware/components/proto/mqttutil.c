#include "mqttutil.h"

#include <ctype.h>
#include <string.h>

bool mqtt_topic_match(const char *f, const char *t, size_t len)
{
    size_t i = 0;
    // Topics starting with $ (broker internals) never match a leading wildcard.
    if (len && t[0] == '$' && (f[0] == '+' || f[0] == '#')) return false;
    while (*f) {
        if (f[0] == '#') return true;  // the rest, including the parent level
        if (f[0] == '+') {
            while (i < len && t[i] != '/') i++;
            f++;
        } else {
            if (i >= len || *f != t[i]) {
                // "a/#" also matches "a"
                return i == len && f[0] == '/' && f[1] == '#' && f[2] == '\0';
            }
            f++;
            i++;
        }
    }
    return i == len;
}

size_t mqtt_alias_match(const char *alias, const char *data, size_t len)
{
    size_t al = strlen(alias);
    if (!al || len <= al || memcmp(data, alias, al) != 0 || data[al] != ',') return 0;
    return al + 1;
}

/** Next comma-separated field from *p (up to end): sets f and fl, moves *p past the comma. */
static bool field(const char **p, const char *end, const char **f, size_t *fl)
{
    const char *c = memchr(*p, ',', (size_t)(end - *p));
    if (!c) return false;
    *f = *p;
    *fl = (size_t)(c - *p);
    *p = c + 1;
    return true;
}

static bool ieq(const char *a, size_t n, const char *b)
{
    if (strlen(b) != n) return false;
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != b[i]) return false;
    }
    return true;
}

bool mqtt_custom_parse(const char *data, size_t len, char *topic, size_t topic_max, int *qos, bool *retain,
                       const char **payload, size_t *payload_len)
{
    const char *p = data, *end = data + len, *t, *q, *r;
    size_t tl, ql, rl;
    if (!field(&p, end, &t, &tl) || !field(&p, end, &q, &ql) || !field(&p, end, &r, &rl)) return false;
    if (tl == 0 || tl >= topic_max || ql != 1 || q[0] < '0' || q[0] > '2') return false;
    if (ieq(r, rl, "1") || ieq(r, rl, "on")) *retain = true;
    else if (ieq(r, rl, "0") || ieq(r, rl, "off")) *retain = false;
    else return false;
    memcpy(topic, t, tl);
    topic[tl] = '\0';
    if (strpbrk(topic, "+#")) return false;  // no wildcards in a publish topic
    *qos = q[0] - '0';
    *payload = p;
    *payload_len = (size_t)(end - p);
    return true;
}
