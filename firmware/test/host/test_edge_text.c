// cfg_edge_text against the tree-based cfg_validate / cfg_load, byte for byte, plus the
// arena limit of spec §9.4: make -C stm32/firmware/test/host
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "heap_meter.h"
#include "cfgmodel.h"

static const cfg_env_t ENV = {.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};
static int failures, compared;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

typedef struct {
    char *p;
    size_t n, cap;
} sbuf_t;

static bool sink(void *ctx, const char *s, size_t n)
{
    sbuf_t *b = ctx;
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
    return true;
}

/** The reference: the tree-based model, printed the way the ESP32 firmware stores it. */
static bool oracle(const char *json, bool strict, char **text, char *err, size_t errlen)
{
    *text = NULL;
    err[0] = '\0';
    cJSON *in = cJSON_Parse(json);
    if (!in) return snprintf(err, errlen, "The request body is not valid JSON."), false;
    cJSON *out = strict ? cfg_validate(&ENV, "edgeDevices", in, NULL, err, errlen) : cfg_load(&ENV, "edgeDevices", in, err, errlen);
    cJSON_Delete(in);
    if (!out) return false;
    *text = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return true;
}

/** One payload through both paths; they must agree on result, message and text. */
static void same(const char *json, bool strict, const char *label)
{
    char e1[200], e2[200] = "";
    char *t1;
    bool ok1 = oracle(json, strict, &t1, e1, sizeof e1);
    sbuf_t b = {0};
    cfg_result_t r = cfg_edge_text(&ENV, json, strlen(json), strict, CFG_EDGE_MAX, sink, &b, e2, sizeof e2);
    compared++;
    if (ok1) {
        CHECK(r == CFG_OK, "%s: text path failed (%d: %s) where the tree path passed", label, r, e2);
        CHECK(r != CFG_OK || (b.p && strcmp(t1, b.p) == 0), "%s: normalised text differs\n  tree: %s\n  text: %s", label, t1,
              b.p ? b.p : "(none)");
    } else {
        CHECK(r == CFG_INVALID, "%s: text path returned %d where the tree path said \"%s\"", label, r, e1);
        CHECK(strcmp(e1, e2) == 0, "%s: messages differ\n  tree: %s\n  text: %s", label, e1, e2);
    }
    free(t1);
    free(b.p);
}

static void both(const char *json, const char *label)
{
    same(json, true, label);
    same(json, false, label);
}

static void test_cases(void)
{
    both("[]", "empty");
    both("{}", "not a list");
    both("[1]", "device not an object");
    both("[{\"name\":\"a\"}]", "one device, no nodes key");
    both("[{\"name\":\"a\",\"nodes\":[]}]", "empty nodes");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"p\"}]}]", "one point");
    both("[{\"name\":\"a\",\"nodes\":{\"x\":{\"name\":\"p\"},\"y\":{\"name\":\"q\"}}}]", "nodes as an object");
    both("[{\"name\":\"a\",\"nodes\":7}]", "nodes as a number");
    both("[{\"name\":\"a\",\"Nodes\":[{\"name\":\"p\"}]}]", "Nodes in another case");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"p\"}],\"NODES\":[{\"name\":\"q\"}]}]", "two nodes keys");
    both("[{\"na\\u006de\":\"esc\",\"nodes\":[{\"n\\u0061me\":\"p\"}]}]", "escaped keys");
    both("[{\"name\\u0000x\":\"nul\"}]", "key with a NUL");
    both("[{\"name\":\"a\",\"name\":\"b\"}]", "duplicate key: first wins");
    both("[{\"name\":\"a\",\"junk\":[1,2,{\"deep\":true}]}]", "unknown keys dropped");
    both("[{\"name\":\"a\",\"ip\":\"\\uD83D\\uDE00\",\"nodes\":[{\"name\":\"p\",\"formula\":\"\xc3\xa9\"}]}]", "non-ASCII values");
    both("[{\"name\":\"\xc3\xa9\"}]", "non-ASCII device name");
    both("[{\"name\":\"a\",\"nodes\":[5]}]", "point not an object");
    both("[{\"name\":\"a\",\"uart\":3}]", "header range error");
    both("[{\"name\":\"a\"},{\"name\":\"b\",\"uart\":\"x\"}]", "second header bad");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"p\",\"fc\":9}]},{\"name\":\"b\",\"uart\":7}]", "header errors come before points");
    both("[{\"name\":\"bad name\"}]", "invalid device name");
    both("[{\"name\":\"SBIOT_IO\"}]", "reserved name");
    both("[{\"name\":\"d\"},{\"name\":\"d\"}]", "duplicate device");
    both("[{\"name\":\"a\",\"source\":\"io\"},{\"name\":\"b\",\"source\":\"io\"}]", "two IO devices");
    both("[{\"name\":\"a\",\"source\":\"virtual\"},{\"name\":\"b\",\"source\":\"virtual\"}]", "two virtual devices");
    both("[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"ioPoint\":\"AI2\"},{\"ioPoint\":\"DO1\"},{\"ioPoint\":\"DI2\"}]}]", "IO points renamed");
    both("[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"ioPoint\":\"AI3\"}]}]", "IO channel missing");
    both("[{\"name\":\"v\",\"source\":\"virtual\",\"addr\":\"200\",\"nodes\":[{\"name\":\"x\",\"dtype\":\"bool\"}]}]", "virtual bool");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"p\"}]},{\"name\":\"b\",\"nodes\":[{\"name\":\"p\"}]}]", "duplicate point across devices");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"bad name\"}]}]", "invalid point name");
    both("[{\"name\":\"m\",\"protocol\":\"dlt645\",\"addr\":\"123\"}]", "DL/T645 address");
    both("[{\"name\":\"m\",\"protocol\":\"dlt645\",\"addr\":\"123456789012\",\"nodes\":[{\"name\":\"v\",\"di\":\"0201ZZ00\"}]}]", "DL/T645 di");
    both("[{\"name\":\"m\",\"protocol\":\"dlt645\",\"addr\":\"123456789012\",\"nodes\":[{\"name\":\"v\",\"di\":\"02010100\",\"dtype\":\"bool\"}]}]", "DL/T645 bool");
    both("[{\"name\":\"a\",\"addr\":\"0\"}]", "slave id 0");
    both("[{\"name\":\"a\",\"addr\":\"256\"}]", "slave id 256");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"c\",\"fc\":1,\"dtype\":\"uint16\"}]}]", "coil not bool");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"h\",\"fc\":3,\"dtype\":\"bool\"}]}]", "register bool");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"r\",\"reg\":65534,\"dtype\":\"float32_abcd\"}]}]", "registers past 65535");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"r\",\"mapEn\":true,\"mapAddr\":65535,\"dtype\":\"float64\"}]}]", "mapped past 65535");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"x\",\"reg\":10,\"dtype\":\"float32_abcd\"},{\"name\":\"y\",\"mapEn\":true,\"mapAddr\":11}]}]",
         "mapped overlaps an unmapped point");
    both("[{\"name\":\"a\",\"addr\":\"5\"},{\"name\":\"b\",\"addr\":\"6\",\"mapEn\":true,\"mapAddr\":5}]", "mapped id taken by an unmapped device");
    both("[{\"name\":\"v\",\"source\":\"virtual\",\"addr\":\"7\"},{\"name\":\"b\",\"addr\":\"7\"}]", "virtual id taken");
    char many[16384];
    size_t o = (size_t)snprintf(many, sizeof many, "[");
    for (int i = 0; i < 65; i++) o += (size_t)snprintf(many + o, sizeof many - o, "%s{\"name\":\"d%d\"}", i ? "," : "", i);
    snprintf(many + o, sizeof many - o, "]");
    both(many, "65 devices");
    o = (size_t)snprintf(many, sizeof many, "[");
    for (int i = 0; i < 6; i++) o += (size_t)snprintf(many + o, sizeof many - o, "%s{\"name\":\"e%d\",\"source\":\"eth\",\"addr\":\"%d\"}", i ? "," : "", i, i + 1);
    snprintf(many + o, sizeof many - o, "]");
    both(many, "6 Modbus TCP devices");
    o = (size_t)snprintf(many, sizeof many, "[{\"name\":\"a\",\"nodes\":[");
    for (int i = 0; i < 101; i++) o += (size_t)snprintf(many + o, sizeof many - o, "%s{\"name\":\"p%d\",\"reg\":%d}", i ? "," : "", i, i);
    snprintf(many + o, sizeof many - o, "]}]");
    both(many, "101 points");
}

// ---- Random models: every field drawn from a pool of valid and invalid values ----------

static unsigned s_rng = 12345;
static unsigned rnd(unsigned n) { return (s_rng = s_rng * 1103515245u + 12345u) / 65536u % n; }
static const char *pick(const char *const *pool, size_t n) { return pool[rnd((unsigned)n)]; }
#define PICK(pool) pick(pool, sizeof pool / sizeof *pool)

static const char *const NAMES[] = {"\"a\"", "\"b\"", "\"meter_1\"", "\"SBIOT_IO\"", "\"bad name\"", "\"\"", "\"abcdefghijklmnopq\"", "7"};
static const char *const SRC[] = {"\"uart\"", "\"uart\"", "\"eth\"", "\"io\"", "\"virtual\"", "\"bogus\""};
static const char *const PROTO[] = {"\"modbus\"", "\"dlt645\"", "\"x\""};
static const char *const ADDR[] = {"\"1\"", "\"5\"", "\"255\"", "\"0\"", "\"256\"", "\"abc\"", "\"123456789012\"", "\"1234567890123\"", "5"};
static const char *const BOOLS[] = {"true", "false", "\"1\"", "0", "\"x\""};
static const char *const SMALL[] = {"1", "5", "200", "0", "255", "256", "\"7\""};
static const char *const PNAMES[] = {"\"p\"", "\"q\"", "\"r\"", "\"t_1\"", "\"bad name\"", "\"\""};
static const char *const FC[] = {"1", "2", "3", "4", "3", "5", "\"3\""};
static const char *const REG[] = {"0", "1", "10", "11", "65534", "65535", "70000", "-1"};
static const char *const DTYPE[] = {"\"uint16\"", "\"bool\"", "\"float32_abcd\"", "\"float64\"", "\"int32_cdab\"", "\"nope\""};
static const char *const DI[] = {"\"02010100\"", "\"0201ZZ00\"", "\"\"", "\"123\""};
static const char *const MAPADDR[] = {"0", "10", "11", "65534", "65535", "70000"};
static const char *const IOPT[] = {"\"DO1\"", "\"DO2\"", "\"DI2\"", "\"AI1\"", "\"AI2\"", "\"AI3\"", "\"\""};

static size_t put(char *b, size_t o, size_t cap, const char *k, const char *v, bool *first)
{
    if (rnd(5) == 0) return o;  // leave the key out: the default applies
    o += (size_t)snprintf(b + o, cap - o, "%s\"%s\":%s", *first ? "" : ",", k, v);
    *first = false;
    return o;
}

static void random_model(char *b, size_t cap)
{
    size_t o = (size_t)snprintf(b, cap, "[");
    int ndev = (int)rnd(5);
    for (int d = 0; d < ndev; d++) {
        bool first = true;
        o += (size_t)snprintf(b + o, cap - o, "%s{", d ? "," : "");
        o = put(b, o, cap, "name", PICK(NAMES), &first);
        o = put(b, o, cap, "source", PICK(SRC), &first);
        o = put(b, o, cap, "protocol", PICK(PROTO), &first);
        o = put(b, o, cap, "addr", PICK(ADDR), &first);
        o = put(b, o, cap, "mapEn", PICK(BOOLS), &first);
        o = put(b, o, cap, "mapAddr", PICK(SMALL), &first);
        if (rnd(4) == 0) o = put(b, o, cap, "junk", "{\"x\":[1,2]}", &first);
        o += (size_t)snprintf(b + o, cap - o, "%s\"nodes\":[", first ? "" : ",");
        int nn = (int)rnd(5);
        for (int i = 0; i < nn; i++) {
            bool f2 = true;
            o += (size_t)snprintf(b + o, cap - o, "%s{", i ? "," : "");
            o = put(b, o, cap, "name", PICK(PNAMES), &f2);
            o = put(b, o, cap, "fc", PICK(FC), &f2);
            o = put(b, o, cap, "reg", PICK(REG), &f2);
            o = put(b, o, cap, "dtype", PICK(DTYPE), &f2);
            o = put(b, o, cap, "di", PICK(DI), &f2);
            o = put(b, o, cap, "mapEn", PICK(BOOLS), &f2);
            o = put(b, o, cap, "mapAddr", PICK(MAPADDR), &f2);
            o = put(b, o, cap, "ioPoint", PICK(IOPT), &f2);
            o += (size_t)snprintf(b + o, cap - o, "}");
        }
        o += (size_t)snprintf(b + o, cap - o, "]}");
    }
    snprintf(b + o, cap - o, "]");
}

static void test_random(void)
{
    char b[8192];
    int before = failures;
    for (int i = 0; i < 4000 && failures - before < 5; i++) {
        random_model(b, sizeof b);
        char label[32];
        snprintf(label, sizeof label, "random #%d", i);
        both(b, label);
    }
}

// ---- Limits and the arena -------------------------------------------------------------


/** The largest model this firmware accepts: 64 devices, 100 points, every text field at its limit. */
static void largest_model(char *b, size_t cap)
{
    size_t o = (size_t)snprintf(b, cap, "[");
    for (int d = 0, k = 0; d < 64; d++) {
        o += (size_t)snprintf(b + o, cap - o,
                              "%s{\"name\":\"device_%02d_abcdef\",\"source\":\"uart\",\"uart\":%d,\"ip\":\"192.168.100.200\",\"ethPort\":502,"
                              "\"protocol\":\"modbus\",\"addr\":\"%d\",\"poll\":1000,\"mapEn\":false,\"mapAddr\":1,\"merge\":true,\"nodes\":[",
                              d ? "," : "", d, 1 + d % 2, d + 1);
        int per = 100 / 64 + (d < 100 % 64);
        for (int i = 0; i < per; i++, k++) {
            o += (size_t)snprintf(b + o, cap - o,
                                  "%s{\"name\":\"point_%03d_abcdef\",\"fc\":3,\"reg\":%d,\"di\":\"\",\"dtype\":\"float32_abcd\",\"timeout\":200,"
                                  "\"onChange\":true,\"range\":1,\"mapEn\":false,\"mapAddr\":0,\"formula\":\"=%%s*0.1+0.123456789-0.123456789*1.000000001+12345.6789-98765.43\","
                                  "\"ioPoint\":\"\",\"ioReport\":0,\"upper\":0,\"lower\":0}",
                                  i ? "," : "", k, i * 2);
        }
        o += (size_t)snprintf(b + o, cap - o, "]}");
    }
    snprintf(b + o, cap - o, "]");
}

static void test_limits(void)
{
    static char b[65536];
    largest_model(b, sizeof b);
    char err[200];
    sbuf_t out = {0};

    meter_peak_restart();
    cfg_result_t r = cfg_edge_text(&ENV, b, strlen(b), true, CFG_EDGE_MAX, sink, &out, err, sizeof err);
    size_t peak = meter_peak_restart();
    CHECK(r == CFG_OK, "largest model rejected (%d: %s)", r, err);
    CHECK(out.n <= CFG_EDGE_MAX, "largest model normalises to %zu bytes", out.n);
    printf("  largest Edge model: %zu bytes in, %zu bytes out, live-block peak %zu bytes within 61 440\n", strlen(b), out.n, peak);

    // The normalised text over the limit: 413, and nothing is valid to keep.
    out.n = 0;
    r = cfg_edge_text(&ENV, b, strlen(b), true, 1000, sink, &out, err, sizeof err);
    CHECK(r == CFG_TOO_LARGE && strcmp(err, "The Edge model is too large.") == 0, "small max_out: %d %s", r, err);
    free(out.p);
}

/** Unknown keys are skipped without being parsed, and a heap that runs out is reported, never half a model. */
static void test_memory_edges(void)
{
    static char b[49152];
    char err[200];
    size_t o = (size_t)snprintf(b, sizeof b, "[{\"name\":\"a\",\"junk\":[");
    while (o < 40000) o += (size_t)snprintf(b + o, sizeof b - o, "1,");
    snprintf(b + o, sizeof b - o, "1],\"nodes\":[{\"name\":\"p\"}]}]");
    sbuf_t out = {0};
    meter_peak_restart();
    cfg_result_t r = cfg_edge_text(&ENV, b, strlen(b), true, CFG_EDGE_MAX, sink, &out, err, sizeof err);
    size_t peak = meter_peak_restart();
    CHECK(r == CFG_OK, "a 40 KB unknown key must be skipped (%d: %s)", r, err);
    CHECK(peak < 20 * 1024, "the unknown key was parsed: heap peak %zu bytes", peak);
    free(out.p);

    largest_model(b, sizeof b);
    out = (sbuf_t){0};
    s_meter_limit = s_meter_live + 12 * 1024;  // 12 KB more for cJSON, then it runs out
    r = cfg_edge_text(&ENV, b, strlen(b), true, CFG_EDGE_MAX, sink, &out, err, sizeof err);
    s_meter_limit = 60 * 1024;
    CHECK(r == CFG_NO_MEMORY && strcmp(err, "Out of memory.") == 0, "a heap that runs out: %d %s", r, err);
    free(out.p);
}

static bool refuse(void *ctx, const char *s, size_t n) { (void)ctx, (void)s, (void)n; return false; }

static int s_calls;
static bool refuse_third(void *ctx, const char *s, size_t n)
{
    (void)ctx, (void)s, (void)n;
    return ++s_calls < 3;
}

/** Malformed text inside a member the parser would skip is still malformed JSON: nothing is emitted. */
static void test_bad_text_in_skipped_member(void)
{
    const char *bad[] = {"[{\"name\":\"a\",\"junk\":\"\\uD800\"}]", "[{\"name\":\"a\",\"junk\":[\"\\uDC00x\"]}]",
                         "[{\"name\":\"a\",\"junk\":\"\xc0\x80\"}]", "[{\"name\":\"a\",\"nodes\":[{\"junk\":\"\xed\xa0\x80\"}]}]",
                         "[{\"name\":\"a\",\"\\uD800\":1}]"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        char err[200];
        s_calls = 0;
        cfg_result_t r = cfg_edge_text(&ENV, bad[i], strlen(bad[i]), true, CFG_EDGE_MAX, refuse_third, NULL, err, sizeof err);
        CHECK(r == CFG_INVALID && strcmp(err, "The request body is not valid JSON.") == 0, "bad text %zu: %d %s", i, r, err);
        CHECK(s_calls == 0, "bad text %zu: emit called %d times", i, s_calls);
    }
}

static void test_abort(void)
{
    char err[200];
    cfg_result_t r = cfg_edge_text(&ENV, "[{\"name\":\"a\"}]", 14, true, CFG_EDGE_MAX, refuse, NULL, err, sizeof err);
    CHECK(r == CFG_ABORTED && strcmp(err, "The Edge model could not be stored.") == 0, "emit failure: %d %s", r, err);
    // The store fails on its third write; a schema error further on must not replace the abort.
    const char *later = "[{\"name\":\"a\",\"nodes\":[{\"name\":\"p\"}]},{\"name\":\"a\"}]";
    s_calls = 0;
    r = cfg_edge_text(&ENV, later, strlen(later), true, CFG_EDGE_MAX, refuse_third, NULL, err, sizeof err);
    CHECK(r == CFG_ABORTED, "emit failure before a schema error: %d %s", r, err);
    CHECK(s_calls == 3, "emitting goes on after a refusal (%d calls)", s_calls);
}

int main(void)
{
    test_cases();
    test_random();
    meter_install(60 * 1024);
    test_limits();
    test_abort();
    test_bad_text_in_skipped_member();
    test_memory_edges();
    if (failures) printf("%d FAILED (%d payloads compared)\n", failures, compared);
    else printf("edge_text: all tests passed (%d payloads compared)\n", compared);
    return failures != 0;
}
