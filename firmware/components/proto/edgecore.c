#include "edgecore.h"
#include "jsonscan.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Data types ---------------------------------------------------------------------------------

static const char *const DTYPES[DT_COUNT] = {"bool", "uint8", "int8", "uint16", "int16", "uint32_abcd", "uint32_cdab",
                                             "int32_abcd", "int32_cdab", "float32_abcd", "float32_cdab", "float64"};

int edge_dtype(const char *name)
{
    for (int i = 0; name && i < DT_COUNT; i++) {
        if (strcmp(DTYPES[i], name) == 0) return i;
    }
    return -1;
}

int edge_dtype_regs(int dt)
{
    switch (dt) {
    case DT_UINT32_ABCD:
    case DT_UINT32_CDAB:
    case DT_INT32_ABCD:
    case DT_INT32_CDAB:
    case DT_FLOAT32_ABCD:
    case DT_FLOAT32_CDAB: return 2;
    case DT_FLOAT64: return 4;
    default: return 1;
    }
}

/** The 32-bit word, high register first ("ABCD") or low first ("CDAB"). */
static uint32_t u32(const uint16_t *r, bool swap) { return swap ? (uint32_t)r[1] << 16 | r[0] : (uint32_t)r[0] << 16 | r[1]; }

double edge_decode(int dt, const uint16_t *r)
{
    switch (dt) {
    case DT_BOOL: return r[0] != 0;
    case DT_UINT8: return r[0] & 0xFF;
    case DT_INT8: return (int8_t)(r[0] & 0xFF);
    case DT_UINT16: return r[0];
    case DT_INT16: return (int16_t)r[0];
    case DT_UINT32_ABCD: return u32(r, false);
    case DT_UINT32_CDAB: return u32(r, true);
    case DT_INT32_ABCD: return (int32_t)u32(r, false);
    case DT_INT32_CDAB: return (int32_t)u32(r, true);
    case DT_FLOAT32_ABCD:
    case DT_FLOAT32_CDAB: {
        uint32_t w = u32(r, dt == DT_FLOAT32_CDAB);
        float f;
        memcpy(&f, &w, 4);
        return f;
    }
    case DT_FLOAT64: {
        uint64_t w = (uint64_t)r[0] << 48 | (uint64_t)r[1] << 32 | (uint64_t)r[2] << 16 | r[3];
        double d;
        memcpy(&d, &w, 8);
        return d;
    }
    default: return 0;
    }
}

static void put32(uint16_t *r, uint32_t w, bool swap)
{
    r[swap ? 1 : 0] = (uint16_t)(w >> 16);
    r[swap ? 0 : 1] = (uint16_t)w;
}

int edge_encode(int dt, double v, uint16_t *r)
{
    switch (dt) {
    case DT_BOOL: r[0] = v != 0; return 1;
    case DT_UINT8:
    case DT_INT8: r[0] = (uint16_t)((int)llround(v) & 0xFF); return 1;
    case DT_UINT16:
    case DT_INT16: r[0] = (uint16_t)(int32_t)llround(v); return 1;
    case DT_UINT32_ABCD:
    case DT_UINT32_CDAB:
    case DT_INT32_ABCD:
    case DT_INT32_CDAB: put32(r, (uint32_t)(int64_t)llround(v), dt == DT_UINT32_CDAB || dt == DT_INT32_CDAB); return 2;
    case DT_FLOAT32_ABCD:
    case DT_FLOAT32_CDAB: {
        float f = (float)v;
        uint32_t w;
        memcpy(&w, &f, 4);
        put32(r, w, dt == DT_FLOAT32_CDAB);
        return 2;
    }
    case DT_FLOAT64: {
        uint64_t w;
        memcpy(&w, &v, 8);
        for (int i = 0; i < 4; i++) r[i] = (uint16_t)(w >> (48 - 16 * i));
        return 4;
    }
    default: return 0;
    }
}

// ---- Formulas (recursive descent) ---------------------------------------------------------------

typedef struct {
    const char *p;
    const double *args;
    int nargs, next;
    bool ok;
} parser_t;

static double expr(parser_t *ps);

static void skip(parser_t *ps)
{
    while (*ps->p == ' ') ps->p++;
}

static double factor(parser_t *ps)
{
    skip(ps);
    if (*ps->p == '-') {
        ps->p++;
        return -factor(ps);
    }
    if (*ps->p == '+') {
        ps->p++;
        return factor(ps);
    }
    if (*ps->p == '(') {
        ps->p++;
        double v = expr(ps);
        skip(ps);
        if (*ps->p != ')') ps->ok = false;
        else ps->p++;
        return v;
    }
    if (ps->p[0] == '%' && ps->p[1] == 's') {
        ps->p += 2;
        if (ps->next >= ps->nargs) {
            ps->ok = false;
            return 0;
        }
        return ps->args[ps->next++];
    }
    if (isdigit((unsigned char)*ps->p) || *ps->p == '.') {
        char *end;
        double v = strtod(ps->p, &end);
        ps->p = end;
        return v;
    }
    ps->ok = false;
    return 0;
}

static double term(parser_t *ps)
{
    double v = factor(ps);
    for (;;) {
        skip(ps);
        char op = *ps->p;
        if (op != '*' && op != '/') return v;
        ps->p++;
        double r = factor(ps);
        if (op == '*') v *= r;
        else if (r == 0) ps->ok = false;
        else v /= r;
    }
}

static double expr(parser_t *ps)
{
    double v = term(ps);
    for (;;) {
        skip(ps);
        char op = *ps->p;
        if (op != '+' && op != '-') return v;
        ps->p++;
        double r = term(ps);
        v = op == '+' ? v + r : v - r;
    }
}

bool edge_formula_eval(const char *e, const double *args, int nargs, double *out)
{
    if (!e || e[0] != '=') return false;
    parser_t ps = {.p = e + 1, .args = args, .nargs = nargs, .ok = true};
    double v = expr(&ps);
    skip(&ps);
    if (!ps.ok || *ps.p != '\0' || !isfinite(v)) return false;
    *out = v;
    return true;
}

int edge_formula_split(const char *f, char *e, size_t emax, char names[][EDGE_NAME_MAX], int max)
{
    size_t el = strcspn(f, ",");
    if (el >= emax) el = emax - 1;
    memcpy(e, f, el);
    e[el] = '\0';
    while (el && e[el - 1] == ' ') e[--el] = '\0';
    int n = 0;
    for (const char *p = f + strcspn(f, ","); *p == ',' && n < max;) {
        p++;
        while (*p == ' ') p++;
        size_t l = strcspn(p, ",");
        size_t k = l;
        while (k && p[k - 1] == ' ') k--;
        if (k >= EDGE_NAME_MAX) k = EDGE_NAME_MAX - 1;
        memcpy(names[n], p, k);
        names[n][k] = '\0';
        n++;
        p += l;
    }
    return n;
}

// ---- DL/T645-2007 ------------------------------------------------------------------------------

/** "123456789012" -> A0..A5 (A0 = the last two digits). */
static bool meter_address(const char *addr, uint8_t a[6])
{
    if (!addr || strlen(addr) != 12) return false;
    for (int i = 0; i < 6; i++) {
        const char *d = addr + 10 - 2 * i;
        if (!isdigit((unsigned char)d[0]) || !isdigit((unsigned char)d[1])) return false;
        a[i] = (uint8_t)((d[0] - '0') << 4 | (d[1] - '0'));
    }
    return true;
}

size_t dlt645_read_req(const char *addr, uint32_t di, uint8_t *out, size_t max)
{
    uint8_t a[6];
    if (max < 20 || !meter_address(addr, a)) return 0;
    size_t n = 0;
    for (int i = 0; i < 4; i++) out[n++] = 0xFE;  // wake-up preamble
    size_t start = n;
    out[n++] = 0x68;
    memcpy(out + n, a, 6);
    n += 6;
    out[n++] = 0x68;
    out[n++] = 0x11;  // read data
    out[n++] = 0x04;
    for (int i = 0; i < 4; i++) out[n++] = (uint8_t)((di >> (8 * i)) + 0x33);
    uint8_t cs = 0;
    for (size_t i = start; i < n; i++) cs += out[i];
    out[n++] = cs;
    out[n++] = 0x16;
    return n;
}

int dlt645_parse(const uint8_t *buf, size_t len, const char *addr, uint32_t di, double *value)
{
    uint8_t a[6];
    if (!meter_address(addr, a)) return -2;
    for (size_t s = 0; s + 12 <= len; s++) {
        const uint8_t *f = buf + s;
        if (f[0] != 0x68 || f[7] != 0x68) continue;
        size_t dl = f[9];
        if (s + 12 + dl > len || f[10 + dl + 1] != 0x16) continue;
        uint8_t cs = 0;
        for (size_t i = 0; i < 10 + dl; i++) cs += f[i];
        if (cs != f[10 + dl] || memcmp(f + 1, a, 6) != 0) continue;
        uint8_t ctrl = f[8];
        if (ctrl == 0xD1) return -1;  // abnormal reply
        if (ctrl != 0x91 || dl < 4) continue;
        uint32_t got = 0;
        for (int i = 0; i < 4; i++) got |= (uint32_t)(uint8_t)(f[10 + i] - 0x33) << (8 * i);
        if (got != di) continue;
        // Data: little-endian BCD bytes, each stored + 0x33.
        double v = 0;
        for (size_t i = dl; i > 4; i--) {
            uint8_t b = (uint8_t)(f[10 + i - 1] - 0x33);
            if ((b >> 4) > 9 || (b & 0xF) > 9) return -2;
            v = v * 100 + (b >> 4) * 10 + (b & 0xF);
        }
        *value = v;
        return 0;
    }
    return -2;
}

// ---- Report template ---------------------------------------------------------------------------

void edge_format(double v, char *out, size_t max)
{
    if (v == floor(v) && fabs(v) < 1e15) snprintf(out, max, "%.0f", v);
    else snprintf(out, max, "%.6g", v);
}

#define MARK '\x01'  // stands for a slot in the printed template; JSON text never contains it raw
#define NUM_MAX 32      // longest formatted number

struct edge_tmpl {
    char *text;       // the printed template; fragments are NUL-separated slices of it
    const char **frag; // nslots + 1 fragments
    size_t *frag_len;
    void **slot;
    size_t nslots, literal_len;
    bool quote;
    char ph[48];      // placeholder, already as JSON text ("null", "-1", "\"N/A\"")
};

/** Writes `s` as a JSON string (quoted, escaped); returns the length or 0 when it does not fit. */
static size_t json_string(const char *s, char *out, size_t max)
{
    size_t n = 0;
    if (max < 3) return 0;
    out[n++] = '"';
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char esc[8];
        size_t el;
        if (c == '"' || c == '\\') esc[0] = '\\', esc[1] = (char)c, el = 2;
        else if (c == '\n' || c == '\r' || c == '\t') esc[0] = '\\', esc[1] = c == '\n' ? 'n' : c == '\r' ? 'r' : 't', el = 2;
        else if (c < 0x20) el = (size_t)snprintf(esc, sizeof esc, "\\u%04x", c);
        else esc[0] = (char)c, el = 1;
        if (n + el + 2 > max) return 0;
        memcpy(out + n, esc, el);
        n += el;
    }
    out[n++] = '"';
    out[n] = '\0';
    return n;
}

/** Replaces each string leaf that resolves to a slot with a raw MARK, remembering the slots in print order. */
static bool mark_slots(cJSON *node, edge_resolve_fn resolve, void *ctx, void ***slots, size_t *n, size_t *cap)
{
    for (cJSON *child = node->child; child; child = child->next) {
        if (cJSON_IsString(child)) {
            void *h = resolve(ctx, child->valuestring);
            if (!h) continue;  // a constant
            if (*n == *cap) {
                size_t nc = *cap ? *cap * 2 : 16;
                void **grown = realloc(*slots, nc * sizeof *grown);
                if (!grown) return false;
                *slots = grown;
                *cap = nc;
            }
            (*slots)[(*n)++] = h;
            // Turn the item into a raw "\x01" in place: the key and position stay as they are.
            cJSON_free(child->valuestring);
            child->valuestring = cJSON_malloc(2);
            if (!child->valuestring) return false;
            child->valuestring[0] = MARK;
            child->valuestring[1] = '\0';
            child->type = cJSON_Raw;
        } else if ((cJSON_IsObject(child) || cJSON_IsArray(child)) && !mark_slots(child, resolve, ctx, slots, n, cap)) {
            return false;
        }
    }
    return true;
}

edge_tmpl_t *edge_tmpl_compile(const char *tmpl, edge_resolve_fn resolve, void *ctx, bool quote, const char *ph)
{
    cJSON *doc = cJSON_Parse(tmpl);
    if (!cJSON_IsObject(doc)) {
        cJSON_Delete(doc);
        return NULL;
    }
    edge_tmpl_t *t = calloc(1, sizeof *t);
    size_t cap = 0;
    if (!t || !mark_slots(doc, resolve, ctx, &t->slot, &t->nslots, &cap) || !(t->text = cJSON_PrintUnformatted(doc))) {
        cJSON_Delete(doc);
        edge_tmpl_free(t);
        return NULL;
    }
    cJSON_Delete(doc);
    t->frag = malloc((t->nslots + 1) * sizeof *t->frag);
    t->frag_len = malloc((t->nslots + 1) * sizeof *t->frag_len);
    if (!t->frag || !t->frag_len) {
        edge_tmpl_free(t);
        return NULL;
    }
    // Cut the printed text at the marks: fragment i ends where slot i stands.
    char *p = t->text;
    for (size_t i = 0; i <= t->nslots; i++) {
        char *m = i < t->nslots ? strchr(p, MARK) : p + strlen(p);
        t->frag[i] = p;
        t->frag_len[i] = (size_t)(m - p);
        t->literal_len += t->frag_len[i];
        if (i < t->nslots) {
            *m = '\0';
            p = m + 1;
        }
    }
    t->quote = quote;
    // The placeholder as JSON text: a literal (-1, null, true) stays one, other text becomes a string.
    if (!ph) {
        strcpy(t->ph, "null");
    } else {
        // A literal only when all of it is one by RFC 8259 (jsonscan): cJSON would take "1abc", "01" or "1."
        // (Plan 10c task 10, Codex review).
        js_span_t root;
        js_type_t k = js_parse_root(ph, strlen(ph), &root) ? js_type(root) : JS_INVALID;
        bool literal = k == JS_NUMBER || k == JS_TRUE || k == JS_FALSE || k == JS_NULL;
        if (literal) snprintf(t->ph, sizeof t->ph, "%s", ph);
        else if (!json_string(ph, t->ph, sizeof t->ph)) strcpy(t->ph, "null");
    }
    return t;
}

size_t edge_tmpl_slots(const edge_tmpl_t *t) { return t->nslots; }

size_t edge_tmpl_size(const edge_tmpl_t *t)
{
    size_t per_slot = 2 + 6 * EDGE_TEXT_MAX;  // worst case: an escaped, quoted text value
    if (per_slot < sizeof t->ph) per_slot = sizeof t->ph;
    return t->literal_len + t->nslots * per_slot + 1;
}

size_t edge_tmpl_render(const edge_tmpl_t *t, edge_value_fn value, void *ctx, char *out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i <= t->nslots; i++) {
        if (n + t->frag_len[i] + 1 > max) return 0;
        memcpy(out + n, t->frag[i], t->frag_len[i]);
        n += t->frag_len[i];
        if (i == t->nslots) break;
        double v = 0;
        const char *text = NULL;
        char num[NUM_MAX];
        size_t w;
        edge_kind_t kind = value(ctx, t->slot[i], &v, &text);
        if (kind == EDGE_NUMBER && !isfinite(v)) kind = EDGE_NO_VALUE;  // NaN/Inf (a float register) is not JSON
        switch (kind) {
        case EDGE_TEXT: w = json_string(text ? text : "", out + n, max - n); break;
        case EDGE_NUMBER:
            edge_format(v, num, sizeof num);
            w = t->quote ? json_string(num, out + n, max - n) : (size_t)snprintf(out + n, max - n, "%s", num);
            break;
        default: w = (size_t)snprintf(out + n, max - n, "%s", t->ph); break;
        }
        if (w == 0 || n + w + 1 > max) return 0;
        n += w;
    }
    out[n] = '\0';
    return n;
}

void edge_tmpl_free(edge_tmpl_t *t)
{
    if (!t) return;
    cJSON_free(t->text);
    free(t->frag);
    free(t->frag_len);
    free(t->slot);
    free(t);
}

// ---- Name index ---------------------------------------------------------------------------------

static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

bool edge_index_init(edge_index_t *ix, size_t n)
{
    size_t cap = 16;
    while (cap < 2 * n) cap *= 2;  // load factor <= 0.5: short probe chains
    ix->keys = calloc(cap, sizeof *ix->keys);
    ix->vals = calloc(cap, sizeof *ix->vals);
    ix->cap = cap;
    return ix->keys && ix->vals;
}

bool edge_index_put(edge_index_t *ix, const char *key, void *val)
{
    size_t mask = ix->cap - 1;
    for (size_t i = fnv1a(key) & mask, probes = 0; probes < ix->cap; i = (i + 1) & mask, probes++) {
        if (!ix->keys[i]) {
            ix->keys[i] = key;
            ix->vals[i] = val;
            return true;
        }
        if (strcmp(ix->keys[i], key) == 0) return false;
    }
    return false;  // full (cannot happen with the sizing above)
}

void *edge_index_get(const edge_index_t *ix, const char *key)
{
    if (!ix->keys || !key) return NULL;
    size_t mask = ix->cap - 1;
    for (size_t i = fnv1a(key) & mask, probes = 0; probes < ix->cap && ix->keys[i]; i = (i + 1) & mask, probes++) {
        if (strcmp(ix->keys[i], key) == 0) return ix->vals[i];
    }
    return NULL;
}

void edge_index_free(edge_index_t *ix)
{
    free(ix->keys);
    free(ix->vals);
    ix->keys = NULL;
    ix->vals = NULL;
}

// ---- Linkage -----------------------------------------------------------------------------------

bool edge_condition(int cond, double v, double lo, double hi)
{
    switch (cond) {
    case 0: return v != 0;
    case 1: return v == 0;
    case 2: return v >= lo;
    case 3: return v <= hi;
    case 4: return v >= lo && v <= hi;
    case 5: return v < lo || v > hi;
    case 6: return v > lo;
    case 7: return v < hi;
    default: return false;
    }
}
