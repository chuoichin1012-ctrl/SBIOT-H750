#include "web_core.h"
#include <string.h>

static const char k_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t web_token_make(const char *user, const char *pass, char *out, size_t cap)
{
    size_t ul = strlen(user), pl = strlen(pass), n = ul + 1 + pl;
    size_t need = (n + 2) / 3 * 4;
    if (need + 1 > cap)
        return 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = 0;
        int k;
        for (k = 0; k < 3 && i + (size_t)k < n; k++) {
            size_t j = i + (size_t)k;
            unsigned char c = (unsigned char)(j < ul ? user[j] : j == ul ? ':' : pass[j - ul - 1]);
            v |= (uint32_t)c << (16 - 8 * k);
        }
        out[o++] = k_b64[v >> 18 & 63];
        out[o++] = k_b64[v >> 12 & 63];
        out[o++] = k > 1 ? k_b64[v >> 6 & 63] : '=';
        out[o++] = k > 2 ? k_b64[v & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

bool web_token_eq(const char *want, const char *got, size_t got_len)
{
    size_t a = strlen(want);
    unsigned diff = (unsigned)(a ^ got_len);
    // Walks the whole of `want`, whatever `got` holds: the time tells nothing about a near miss.
    for (size_t i = 0; i < a; i++)
        diff |= (unsigned)(unsigned char)want[i] ^ (unsigned)(unsigned char)(i < got_len ? got[i] : 0);
    return diff == 0 && a > 0;
}

bool web_basic_ok(const char *want, const char *hdr, size_t len)
{
    return len > 6 && memcmp(hdr, "Basic ", 6) == 0 && web_token_eq(want, hdr + 6, len - 6);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = (char)(c | 0x20);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

size_t web_url_decode(const char *in, size_t len, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (o + 1 >= cap)
            return SIZE_MAX;
        if (in[i] == '%' && i + 2 < len && hexval(in[i + 1]) >= 0 && hexval(in[i + 2]) >= 0) {
            out[o++] = (char)(hexval(in[i + 1]) * 16 + hexval(in[i + 2]));
            i += 2;
        } else {
            out[o++] = in[i] == '+' ? ' ' : in[i];
        }
    }
    if (o >= cap)
        return SIZE_MAX;
    out[o] = '\0';
    return o;
}

#define CONFIG_PREFIX "/api/config/"

static bool has_prefix(const char *s, size_t len, const char *p)
{
    size_t n = strlen(p);
    return len >= n && memcmp(s, p, n) == 0;
}

bool web_config_name(const char *uri, size_t len, char *out, size_t cap)
{
    if (!has_prefix(uri, len, CONFIG_PREFIX))
        return false;
    const char *n = uri + strlen(CONFIG_PREFIX);
    size_t left = len - strlen(CONFIG_PREFIX), k = 0;
    while (k < left && n[k] != '?' && n[k] != '#')
        k++;
    if (k == 0 || k >= cap || memchr(n, '/', k))
        return false;
    memcpy(out, n, k);
    out[k] = '\0';
    return true;
}

// The path of a URI, up to '?'.
static size_t path_len(const char *uri, size_t len)
{
    const char *q = memchr(uri, '?', len);
    return q ? (size_t)(q - uri) : len;
}

static bool path_is(const char *uri, size_t len, const char *p)
{
    return path_len(uri, len) == strlen(p) && memcmp(uri, p, strlen(p)) == 0;
}

web_ep_t web_endpoint(bool post, const char *uri, size_t len)
{
    if (!post)
        return WEB_EP_PLAIN;
    if (path_is(uri, len, "/api/ota"))
        return WEB_EP_OTA;
    if (path_is(uri, len, "/api/cert"))
        return WEB_EP_CERT;
    if (path_is(uri, len, CONFIG_PREFIX "edgeDevices"))
        return WEB_EP_EDGE;
    return WEB_EP_PLAIN;
}

int web_body_check(web_ep_t ep, bool has_length, bool chunked, size_t length, size_t slot_max)
{
    if (chunked)
        return 411;
    if (ep != WEB_EP_PLAIN && !has_length)
        return 411;
    size_t max = ep == WEB_EP_OTA ? slot_max : ep == WEB_EP_CERT ? WEB_CERT_MAX : ep == WEB_EP_EDGE ? WEB_EDGE_MAX : WEB_BODY_MAX;
    return length > max ? 413 : 0;
}

bool web_stalled(uint64_t last_byte_ms, uint64_t now_ms)
{
    return now_ms - last_byte_ms >= WEB_STALL_MS;
}

// ---- PEM ----------------------------------------------------------------------------------------

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool b64char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
}

// "-----<what> <label>-----" at p (n bytes left); its length, or 0.
static size_t armor(const char *p, size_t n, const char *what, const char *label)
{
    size_t wl = strlen(what), ll = strlen(label), total = 5 + wl + 1 + ll + 5;
    if (n < total || memcmp(p, "-----", 5) || memcmp(p + 5, what, wl) || p[5 + wl] != ' ' ||
        memcmp(p + 6 + wl, label, ll) || memcmp(p + 6 + wl + ll, "-----", 5))
        return 0;
    return total;
}

static int pem_label(const char *label, const char *pem, size_t len)
{
    size_t i = 0;
    while (i < len && space(pem[i]))
        i++;
    size_t a = armor(pem + i, len - i, "BEGIN", label);
    if (!a)
        return -1;
    i += a;
    size_t body = 0;
    for (;;) {
        while (i < len && space(pem[i]))
            i++;
        if (i >= len)
            return -1;  // no END
        if (pem[i] == '-')
            break;
        if (!b64char(pem[i]))
            return -1;
        i++;
        body++;
    }
    size_t e = armor(pem + i, len - i, "END", label);
    if (!e || body == 0)
        return -1;
    for (i += e; i < len; i++)
        if (!space(pem[i]))
            return -1;  // one object per file, nothing after it
    return 0;
}

int web_pem_check(const char *kind, const char *pem, size_t len)
{
    if (!strcmp(kind, "ca") || !strcmp(kind, "cert"))
        return pem_label("CERTIFICATE", pem, len);
    if (!strcmp(kind, "key"))
        return pem_label("PRIVATE KEY", pem, len) == 0 || pem_label("RSA PRIVATE KEY", pem, len) == 0 ||
                       pem_label("EC PRIVATE KEY", pem, len) == 0
                   ? 0
                   : -1;
    return -1;
}

bool web_send_poll(web_send_t *t, size_t queued, bool wrote, uint64_t now_ms)
{
    if (!queued) {
        t->on = false;
        return false;
    }
    if (!t->on || wrote) {
        t->on = true;
        t->since = now_ms;
    }
    return now_ms - t->since >= WEB_STALL_MS;
}

size_t web_mon_line(char *out, size_t cap, bool tx, const uint8_t *data, size_t len)
{
    static const char HEX[] = "0123456789ABCDEF";
    if (cap < 5)
        return cap ? (out[0] = '\0', 0) : 0;
    if (tx && !len && cap >= sizeof "TX not sent\n") {  // the record after a frame that did not go out
        memcpy(out, "TX not sent\n", sizeof "TX not sent\n");
        return sizeof "TX not sent\n" - 1;
    }
    memcpy(out, tx ? "TX" : "RX", 2);
    size_t n = 2;
    for (size_t i = 0; i < len && n + 3 + 2 <= cap; i++) {  // " XX", then room for "\n" and the NUL
        out[n++] = ' ';
        out[n++] = HEX[data[i] >> 4];
        out[n++] = HEX[data[i] & 15];
    }
    out[n++] = '\n';
    out[n] = '\0';
    return n;
}

void web_mon_each(const uint8_t *data, size_t len, size_t max, void (*fn)(const uint8_t *d, size_t n, void *ctx), void *ctx)
{
    size_t off = 0;
    do {
        size_t n = len - off < max ? len - off : max;
        fn(data ? data + off : NULL, n, ctx);
        off += n;
    } while (off < len);
}

bool web_mon_wait(size_t queued, size_t high, uint64_t *full_since, uint64_t now_ms)
{
    if (queued < high) {
        *full_since = 0;
        return false;
    }
    if (!*full_since)
        *full_since = now_ms;
    return now_ms - *full_since < WEB_MON_STUCK_MS;
}
