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

// ---- streaming (Plan 09c) -------------------------------------------------------------------------------
// The same grammar as the scanners above, one byte at a time, so a body can be checked as it arrives.

enum {
    S_VALUE,      // a value must start (whitespace first)
    S_ARR_FIRST,  // just after '[': a value or ']'
    S_OBJ_FIRST,  // just after '{': a key or '}'
    S_KEY,        // after ',' in an object: a key
    S_COLON,      // after a key: ':'
    S_AFTER,      // after a value inside a container: ',' or its close
    S_END,        // the root value is whole: whitespace only
    S_STR,        // inside a string
    S_ESC,        // after '\' in a string
    S_HEX,        // in \uXXXX, s->sub digits read
    S_UTF8,       // in a UTF-8 sequence, s->sub continuation bytes still to come
    S_LIT,        // in true/false/null, s->sub letters matched
    N_MINUS,      // number: after '-'
    N_ZERO,       // a leading 0
    N_INT,        // integer digits
    N_DOT,        // after '.'
    N_FRAC,       // fraction digits
    N_EXP,        // after 'e'/'E'
    N_EXP_SIGN,   // after the exponent's sign
    N_EXP_DIG,    // exponent digits
};

void js_stream_init(js_stream_t *s, js_type_t want)
{
    memset(s, 0, sizeof *s);
    s->state = S_VALUE;
    s->want = want;
}

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static bool number_ends(int st) { return st == N_ZERO || st == N_INT || st == N_FRAC || st == N_EXP_DIG; }

static void value_done(js_stream_t *s) { s->state = s->depth ? S_AFTER : S_END; }

/** A value starts with c (whitespace already skipped). */
static bool value_start(js_stream_t *s, char c)
{
    js_type_t t;
    switch (c) {
    case '[':
    case '{':
        if (s->depth >= JS_MAX_DEPTH) return false;
        s->stack[s->depth++] = (unsigned char)c;
        s->state = c == '[' ? S_ARR_FIRST : S_OBJ_FIRST;
        t = c == '[' ? JS_ARRAY : JS_OBJECT;
        break;
    case '"':
        s->state = S_STR, s->key = false;
        t = JS_STRING;
        break;
    case 't':
    case 'f':
    case 'n':
        s->lit = c == 't' ? "true" : c == 'f' ? "false" : "null";
        s->state = S_LIT, s->sub = 1;
        t = c == 't' ? JS_TRUE : c == 'f' ? JS_FALSE : JS_NULL;
        break;
    case '-': s->state = N_MINUS, t = JS_NUMBER; break;
    case '0': s->state = N_ZERO, t = JS_NUMBER; break;
    default:
        if (c < '1' || c > '9') return false;
        s->state = N_INT, t = JS_NUMBER;
    }
    if (s->depth == (t == JS_ARRAY || t == JS_OBJECT ? 1 : 0)) s->root = t;  // the root value's own start
    return true;
}

/** A container closes with c, or not. */
static bool close_with(js_stream_t *s, char c)
{
    if (!s->depth || s->stack[s->depth - 1] != (c == ']' ? '[' : '{')) return false;
    s->depth--;
    value_done(s);
    return true;
}

static bool step(js_stream_t *s, char ch)
{
    unsigned char c = (unsigned char)ch;
    switch (s->state) {
    case S_VALUE: return is_ws(ch) || value_start(s, ch);
    case S_ARR_FIRST: return is_ws(ch) || (ch == ']' ? close_with(s, ch) : value_start(s, ch));
    case S_OBJ_FIRST:
        if (is_ws(ch)) return true;
        if (ch == '}') return close_with(s, ch);
        if (ch != '"') return false;
        s->state = S_STR, s->key = true;
        return true;
    case S_KEY:
        if (is_ws(ch)) return true;
        if (ch != '"') return false;
        s->state = S_STR, s->key = true;
        return true;
    case S_COLON:
        if (is_ws(ch)) return true;
        if (ch != ':') return false;
        s->state = S_VALUE;
        return true;
    case S_AFTER:
        if (is_ws(ch)) return true;
        if (ch == ',') {
            s->state = s->stack[s->depth - 1] == '{' ? S_KEY : S_VALUE;
            return true;
        }
        return (ch == ']' || ch == '}') && close_with(s, ch);
    case S_END: return is_ws(ch);
    case S_STR:
        if (s->high) {  // a high surrogate must be followed by \u and a low one, as cJSON requires
            if (ch != '\\') return false;
            s->state = S_ESC;
            return true;
        }
        if (ch == '"') {
            if (s->key) s->state = S_COLON;
            else value_done(s);
            return true;
        }
        if (c < 0x20) return false;
        if (ch == '\\') {
            s->state = S_ESC;
            return true;
        }
        if (c >= 0x80) {  // RFC 3629, as utf8_len
            if (c < 0xC2 || c > 0xF4) return false;
            s->lead = c;
            s->sub = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            s->state = S_UTF8;
        }
        return true;
    case S_UTF8: {
        if ((c & 0xC0) != 0x80) return false;
        bool second = s->lead && (s->sub == (s->lead >= 0xF0 ? 3 : s->lead >= 0xE0 ? 2 : 1));
        if (second && ((s->lead == 0xE0 && c < 0xA0) || (s->lead == 0xED && c > 0x9F) || (s->lead == 0xF0 && c < 0x90) ||
                       (s->lead == 0xF4 && c > 0x8F)))
            return false;  // overlong, an encoded surrogate, beyond U+10FFFF
        if (--s->sub == 0) s->state = S_STR, s->lead = 0;
        return true;
    }
    case S_ESC:
        if (ch == 'u') {
            s->state = S_HEX, s->sub = 0, s->u = 0;
            return true;
        }
        if (s->high || !ch || !strchr("\"\\/bfnrt", ch)) return false;
        s->state = S_STR;
        return true;
    case S_HEX: {
        int h = hexval(ch);
        if (h < 0) return false;
        s->u = s->u << 4 | (unsigned long)h;
        if (++s->sub < 4) return true;
        bool lo = s->u >= 0xDC00 && s->u <= 0xDFFF, hi = s->u >= 0xD800 && s->u <= 0xDBFF;
        if (s->high ? !lo : lo) return false;  // a low one alone, or a high one not followed by a low one
        s->high = !s->high && hi;
        s->state = S_STR;
        return true;
    }
    case S_LIT:
        if (ch != s->lit[s->sub]) return false;
        if (!s->lit[++s->sub]) value_done(s);
        return true;
    default: {  // a number: a byte that does not extend it ends it, and is then read as what follows it
        bool digit = ch >= '0' && ch <= '9';
        switch (s->state) {
        case N_MINUS:
            if (ch == '0') return s->state = N_ZERO, true;
            if (digit) return s->state = N_INT, true;
            return false;
        case N_INT:
            if (digit) return true;
            /* fall through */
        case N_ZERO:
            if (ch == '.') return s->state = N_DOT, true;
            if (ch == 'e' || ch == 'E') return s->state = N_EXP, true;
            break;
        case N_DOT:
            if (digit) return s->state = N_FRAC, true;
            return false;
        case N_FRAC:
            if (digit) return true;
            if (ch == 'e' || ch == 'E') return s->state = N_EXP, true;
            break;
        case N_EXP:
            if (ch == '+' || ch == '-') return s->state = N_EXP_SIGN, true;
            /* fall through */
        case N_EXP_SIGN:
            if (digit) return s->state = N_EXP_DIG, true;
            return false;
        case N_EXP_DIG:
            if (digit) return true;
            break;
        }
        if (!number_ends(s->state)) return false;
        value_done(s);
        return step(s, ch);
    }
    }
}

bool js_stream_feed(js_stream_t *s, const char *data, size_t n)
{
    for (size_t i = 0; i < n && !s->bad; i++)
        if (!step(s, data[i])) s->bad = true;
    return !s->bad;
}

bool js_stream_end(js_stream_t *s)
{
    if (!s->bad && number_ends(s->state) && !s->depth) value_done(s);  // a number at the root ends with the text
    return !s->bad && s->state == S_END && (s->want == JS_INVALID || s->root == s->want);
}
