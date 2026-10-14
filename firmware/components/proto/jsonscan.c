#include "jsonscan.h"

#include <ctype.h>
#include <string.h>

static size_t skip_ws(const char *s, size_t n, size_t i)
{
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    return i;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Each scanner returns the index just past the value that starts at i, or 0 on a syntax
// error: a value always has at least one character, so a real end is never 0.
static size_t scan_value(const char *s, size_t n, size_t i, int depth);

/** The code unit of a \\uXXXX escape whose 'u' is at s[i], or -1. */
static long u_escape(const char *s, size_t n, size_t i)
{
    if (i + 4 >= n) return -1;
    long v = 0;
    for (int k = 1; k <= 4; k++) {
        int h = hexval(s[i + k]);
        if (h < 0) return -1;
        v = v << 4 | h;
    }
    return v;
}

/** Length of the well-formed UTF-8 sequence at s[i] (RFC 3629: no overlongs, no surrogates), or 0. */
static size_t utf8_len(const unsigned char *s, size_t n, size_t i)
{
    unsigned char c = s[i];
    size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    if (c < 0xC2 || c > 0xF4 || i + len > n) return 0;
    for (size_t k = 1; k < len; k++)
        if ((s[i + k] & 0xC0) != 0x80) return 0;
    unsigned char d = s[i + 1];
    if (c == 0xE0 && d < 0xA0) return 0;  // overlong
    if (c == 0xED && d > 0x9F) return 0;  // U+D800..U+DFFF
    if (c == 0xF0 && d < 0x90) return 0;  // overlong
    if (c == 0xF4 && d > 0x8F) return 0;  // above U+10FFFF
    return len;
}

static size_t scan_string(const char *s, size_t n, size_t i)
{
    for (i++; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') return i + 1;
        if (c < 0x20) return 0;
        if (c >= 0x80) {
            size_t len = utf8_len((const unsigned char *)s, n, i);
            if (!len) return 0;
            i += len - 1;
            continue;
        }
        if (c != '\\') continue;
        if (++i >= n) return 0;
        c = (unsigned char)s[i];
        if (c == 'u') {
            long u = u_escape(s, n, i);
            if (u < 0 || (u >= 0xDC00 && u <= 0xDFFF)) return 0;  // a low surrogate on its own
            i += 4;
            if (u >= 0xD800 && u <= 0xDBFF) {  // a high surrogate must be followed by a low one, as cJSON requires
                if (i + 2 >= n || s[i + 1] != '\\' || s[i + 2] != 'u') return 0;
                long lo = u_escape(s, n, i + 2);
                if (lo < 0xDC00 || lo > 0xDFFF) return 0;
                i += 6;
            }
        } else if (!strchr("\"\\/bfnrt", c) || c == '\0') {
            return 0;
        }
    }
    return 0;
}

static size_t scan_number(const char *s, size_t n, size_t i)
{
    if (i < n && s[i] == '-') i++;
    if (i >= n) return 0;
    if (s[i] == '0') {
        i++;
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < n && isdigit((unsigned char)s[i])) i++;
    } else {
        return 0;
    }
    if (i < n && s[i] == '.') {
        if (++i >= n || !isdigit((unsigned char)s[i])) return 0;
        while (i < n && isdigit((unsigned char)s[i])) i++;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        if (++i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (i >= n || !isdigit((unsigned char)s[i])) return 0;
        while (i < n && isdigit((unsigned char)s[i])) i++;
    }
    return i;
}

static size_t scan_literal(const char *s, size_t n, size_t i, const char *lit)
{
    size_t k = strlen(lit);
    return i + k <= n && memcmp(s + i, lit, k) == 0 ? i + k : 0;
}

static size_t scan_container(const char *s, size_t n, size_t i, int depth)
{
    char close = s[i] == '{' ? '}' : ']';
    bool object = close == '}';
    if (depth >= JS_MAX_DEPTH) return 0;
    i = skip_ws(s, n, i + 1);
    if (i < n && s[i] == close) return i + 1;
    for (;;) {
        if (object) {
            if (i >= n || s[i] != '"' || !(i = scan_string(s, n, i))) return 0;
            i = skip_ws(s, n, i);
            if (i >= n || s[i] != ':') return 0;
            i = skip_ws(s, n, i + 1);
        }
        if (!(i = scan_value(s, n, i, depth + 1))) return 0;
        i = skip_ws(s, n, i);
        if (i < n && s[i] == close) return i + 1;
        if (i >= n || s[i] != ',') return 0;
        i = skip_ws(s, n, i + 1);
    }
}

static size_t scan_value(const char *s, size_t n, size_t i, int depth)
{
    if (i >= n) return 0;
    switch (s[i]) {
    case '{':
    case '[': return scan_container(s, n, i, depth);
    case '"': return scan_string(s, n, i);
    case 't': return scan_literal(s, n, i, "true");
    case 'f': return scan_literal(s, n, i, "false");
    case 'n': return scan_literal(s, n, i, "null");
    default: return scan_number(s, n, i);
    }
}

bool js_parse_root(const char *s, size_t n, js_span_t *root)
{
    size_t a = skip_ws(s, n, 0);
    size_t b = scan_value(s, n, a, 0);
    if (!b || skip_ws(s, n, b) != n) return false;
    root->p = s + a;
    root->n = b - a;
    return true;
}

js_type_t js_type(js_span_t v)
{
    if (!v.n) return JS_INVALID;
    switch (v.p[0]) {
    case '{': return JS_OBJECT;
    case '[': return JS_ARRAY;
    case '"': return JS_STRING;
    case 't': return JS_TRUE;
    case 'f': return JS_FALSE;
    case 'n': return JS_NULL;
    default: return JS_NUMBER;
    }
}

void js_iter_init(js_iter_t *it, js_span_t container)
{
    it->in = container;
    js_type_t t = js_type(container);
    it->pos = (t == JS_OBJECT || t == JS_ARRAY) ? 1 : container.n;  // scalars: nothing to walk
}

bool js_iter_next(js_iter_t *it, js_span_t *key, js_span_t *val)
{
    const char *s = it->in.p;
    size_t n = it->in.n, i = skip_ws(s, n, it->pos);
    if (i >= n || s[i] == '}' || s[i] == ']') return false;
    if (s[i] == ',') i = skip_ws(s, n, i + 1);
    js_span_t k = {s + i, 0};
    if (s[0] == '{') {
        size_t e = scan_string(s, n, i);
        k.n = e - i;
        i = skip_ws(s, n, skip_ws(s, n, e) + 1);  // past the ':'
    }
    size_t e = scan_value(s, n, i, 0);
    if (key) *key = k;
    val->p = s + i;
    val->n = e - i;
    it->pos = e;
    return true;
}

size_t js_count(js_span_t container)
{
    js_iter_t it;
    js_span_t v;
    size_t c = 0;
    js_iter_init(&it, container);
    while (js_iter_next(&it, NULL, &v)) c++;
    return c;
}

bool js_key_is(js_span_t key, const char *lit, bool nocase)
{
    if (key.n < 2 || key.p[0] != '"') return false;
    const char *s = key.p + 1, *end = key.p + key.n - 1;
    for (;;) {
        unsigned char c;
        if (s >= end) {
            c = '\0';
        } else if (*s != '\\') {
            c = (unsigned char)*s++;
        } else {
            char e = s[1];
            s += 2;
            if (e == 'u') {
                unsigned cp = 0;
                for (int k = 0; k < 4; k++) cp = cp << 4 | (unsigned)hexval(s[k]);
                s += 4;
                if (cp >= 0x80) return false;  // field names are ASCII
                c = (unsigned char)cp;
            } else {
                c = (unsigned char)(e == 'b' ? '\b' : e == 'f' ? '\f' : e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : e);
            }
        }
        unsigned char l = (unsigned char)*lit;
        if (c == '\0' || l == '\0') return c == l;  // cJSON keys end at a decoded NUL
        if (nocase ? tolower(c) != tolower(l) : c != l) return false;
        lit++;
    }
}
