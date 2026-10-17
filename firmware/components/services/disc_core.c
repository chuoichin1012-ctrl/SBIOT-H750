// SBIOT Finder's packets on the gateway (Plan 12, stm32/docs/specs/2026-10-17-lan-discovery-design.md §2):
// parse and check, the `found` and `ack` replies. Pure C, host-tested (test_disc_core).
#include "disc_core.h"
#include <stdio.h>
#include <string.h>
#include "jsonscan.h"
#include "netcfg.h"

/** The whole datagram is one strict JSON value (jsonscan: RFC 8259, nothing after it, no `01`), with no NUL in it,
 *  raw or escaped: cJSON stops a string at a NUL, so "mac":"<this MAC>\u0000x" would read as this MAC (Codex plan r1). */
static bool strict(const char *buf, size_t len)
{
    js_span_t root;
    if (memchr(buf, '\0', len) || !js_parse_root(buf, len, &root))
        return false;
    for (size_t i = 0; i + 6 <= len; i++)
        if (!memcmp(buf + i, "\\u0000", 6))
            return false;
    return true;
}

static bool hex_lower(const char *s, size_t n)
{
    if (!s || strlen(s) != n)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return false;
    return true;
}

static bool mac_ok(const char *s)
{
    if (!s || strlen(s) != 17)
        return false;
    for (int i = 0; i < 17; i++) {
        bool hex = (s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f');
        if (i % 3 == 2 ? s[i] != ':' : !hex)
            return false;
    }
    return true;
}

/** No member name twice in this object (cJSON keeps both, so a packet could name two targets or two values). */
static bool unique_keys(const cJSON *o)
{
    for (const cJSON *a = o->child; a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (a->string && b->string && !strcmp(a->string, b->string))
                return false;
    return true;
}

static bool mode(const cJSON *n, const char *k, int *out)
{
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(n, k);
    if (!cJSON_IsNumber(m) || (m->valuedouble != 0 && m->valuedouble != 1))
        return false;
    *out = (int)m->valuedouble;
    return true;
}

static bool addr(const cJSON *n, const char *k, char out[16])
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(n, k));
    uint32_t ip;
    if (!s || !netcfg_ip4(s, &ip))
        return false;
    snprintf(out, 16, "%s", s);  // a valid quad is at most 15 characters
    return true;
}

static bool net_ok(const cJSON *n, disc_net_t *out)
{
    return cJSON_IsObject(n) && mode(n, "ipMode", &out->ip_mode) && mode(n, "dnsMode", &out->dns_mode) &&
           addr(n, "ip", out->ip) && addr(n, "netmask", out->netmask) && addr(n, "gateway", out->gateway) &&
           addr(n, "dns1", out->dns1) && addr(n, "dns2", out->dns2);
}

disc_kind_t disc_parse(const char *buf, size_t len, const char *my_mac, disc_req_t *out)
{
    memset(out, 0, sizeof *out);
    if (len > DISC_MAX || !strict(buf, len))
        return DISC_IGNORE;
    cJSON *p = cJSON_ParseWithLength(buf, len);
    disc_kind_t k = DISC_IGNORE;
    const cJSON *net = cJSON_GetObjectItemCaseSensitive(p, "net");
    const char *sig = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "sbiot"));
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(p, "v");
    const char *op = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "op"));
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "id"));
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(p, "mac");
    const char *mac = cJSON_GetStringValue(m);
    if (!cJSON_IsObject(p) || !unique_keys(p) || (cJSON_IsObject(net) && !unique_keys(net)) || !sig ||
        strcmp(sig, "disc") || !cJSON_IsNumber(v) || v->valuedouble != 1 || !op || !hex_lower(id, 8) ||
        (m && !mac_ok(mac)))
        goto done;
    bool mine = mac && !strcmp(mac, my_mac);
    if (!strcmp(op, "search"))
        k = !m || mine ? DISC_SEARCH : DISC_IGNORE;
    else if (!mine)  // commands must name this gateway (spec §2.3)
        k = DISC_IGNORE;
    else if (!strcmp(op, "set"))
        k = net_ok(net, &out->net) ? DISC_SET : DISC_BAD;
    else if (!strcmp(op, "restart"))
        k = DISC_RESTART;
    else if (!strcmp(op, "factory"))
        k = DISC_FACTORY;
    if (k != DISC_IGNORE)
        memcpy(out->id, id, 9);
done:
    cJSON_Delete(p);
    return k;
}

static void quad(char out[16], uint32_t ip)
{
    snprintf(out, 16, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)(ip >> 16 & 255), (unsigned)(ip >> 8 & 255), (unsigned)(ip & 255));
}

static cJSON *head(const char *op, const char *id, const char *mac)
{
    cJSON *o = cJSON_CreateObject();
    if (o && cJSON_AddStringToObject(o, "sbiot", "disc") && cJSON_AddNumberToObject(o, "v", 1) &&
        cJSON_AddStringToObject(o, "op", op) && cJSON_AddStringToObject(o, "id", id) && cJSON_AddStringToObject(o, "mac", mac))
        return o;
    cJSON_Delete(o);
    return NULL;
}

/** The text of o into buf (without the terminating NUL in the count), or -1; o is deleted. */
static int emit(cJSON *o, bool ok, char *buf, size_t cap)
{
    char *t = ok && o ? cJSON_PrintUnformatted(o) : NULL;
    cJSON_Delete(o);
    int n = -1;
    if (t && strlen(t) < cap) {
        n = (int)strlen(t);
        memcpy(buf, t, (size_t)n + 1);
    }
    cJSON_free(t);
    return n;
}

cJSON *disc_net_json(const disc_net_t *n)
{
    cJSON *o = cJSON_CreateObject();
    if (o && cJSON_AddNumberToObject(o, "ipMode", n->ip_mode) && cJSON_AddStringToObject(o, "ip", n->ip) &&
        cJSON_AddStringToObject(o, "netmask", n->netmask) && cJSON_AddStringToObject(o, "gateway", n->gateway) &&
        cJSON_AddNumberToObject(o, "dnsMode", n->dns_mode) && cJSON_AddStringToObject(o, "dns1", n->dns1) &&
        cJSON_AddStringToObject(o, "dns2", n->dns2))
        return o;
    cJSON_Delete(o);
    return NULL;
}

int disc_found(const disc_info_t *in, const char *id, char *buf, size_t cap)
{
    cJSON *o = head("found", id, in->mac), *eth = cJSON_CreateObject(), *net = disc_net_json(&in->saved);
    char a[16], b[16], c[16];
    quad(a, in->eth_ip);
    quad(b, in->eth_mask);
    quad(c, in->eth_gw);
    bool ok = o && eth && net && cJSON_AddStringToObject(o, "sn", in->sn) && cJSON_AddStringToObject(o, "model", in->model) &&
              cJSON_AddStringToObject(o, "fw", in->fw) && cJSON_AddStringToObject(o, "hostname", in->hostname) &&
              cJSON_AddNumberToObject(o, "web", in->web) && cJSON_AddNumberToObject(o, "boot", in->boot) &&
              cJSON_AddStringToObject(eth, "ip", a) && cJSON_AddStringToObject(eth, "netmask", b) &&
              cJSON_AddStringToObject(eth, "gateway", c) && cJSON_AddItemToObject(o, "eth", eth);
    if (ok)
        eth = NULL;  // o owns it now
    ok = ok && cJSON_AddItemToObject(o, "net", net);
    if (ok)
        net = NULL;
    cJSON_Delete(eth);
    cJSON_Delete(net);
    return emit(o, ok, buf, cap);
}

int disc_ack(const char *id, const char *mac, const char *err, char *buf, size_t cap)
{
    cJSON *o = head("ack", id, mac);
    bool ok = o && cJSON_AddBoolToObject(o, "ok", err == NULL) && (!err || cJSON_AddStringToObject(o, "err", err));
    return emit(o, ok, buf, cap);
}

void disc_net_from(const cJSON *network, disc_net_t *out)
{
    memset(out, 0, sizeof *out);
    out->ip_mode = cJSON_GetObjectItemCaseSensitive(network, "ipMode") ? cJSON_GetObjectItemCaseSensitive(network, "ipMode")->valueint : 0;
    out->dns_mode = cJSON_GetObjectItemCaseSensitive(network, "dnsMode") ? cJSON_GetObjectItemCaseSensitive(network, "dnsMode")->valueint : 0;
    const char *keys[] = {"ip", "netmask", "gateway", "dns1", "dns2"};
    char *dst[] = {out->ip, out->netmask, out->gateway, out->dns1, out->dns2};
    for (int i = 0; i < 5; i++) {
        const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(network, keys[i]));
        snprintf(dst[i], 16, "%s", s ? s : "");
    }
}
