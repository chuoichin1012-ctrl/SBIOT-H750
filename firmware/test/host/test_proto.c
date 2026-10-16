// Host unit tests for the protocol code (components/proto): `make -C test/host`.
#include <stdio.h>
#include <string.h>
#include "ipacl.h"
#include "modbus.h"
#include "mqttutil.h"
#include "edgecore.h"
#include "rwprot.h"
#include <stdlib.h>
#include <math.h>

static int failures;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

static void test_crc(void)
{
    // Read 10 holding registers from slave 1: the textbook frame ends C5 CD.
    uint8_t f[8];
    size_t n = mb_rtu_read(f, 1, 3, 0, 10);
    const uint8_t want[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
    CHECK(n == 8 && memcmp(f, want, 8) == 0, "read frame/CRC");
    CHECK(mb_rtu_valid(f, n), "valid");
    f[3] ^= 1;
    CHECK(!mb_rtu_valid(f, n), "corruption detected");
    CHECK(mb_rtu_reply_len(want, 8) == 25, "reply len fc3 x10");
    uint8_t c[8];
    mb_rtu_read(c, 1, 1, 0, 9);
    CHECK(mb_rtu_reply_len(c, 8) == 7, "reply len fc1 x9");
    uint8_t w[8];
    mb_rtu_write1(w, 1, 5, 0, 1);
    const uint8_t wc[] = {0x01, 0x05, 0x00, 0x00, 0xFF, 0x00, 0x8C, 0x3A};
    CHECK(memcmp(w, wc, 8) == 0, "write coil frame");
    uint16_t regs[2] = {0x000A, 0x0102};
    uint8_t m[32];
    size_t mn = mb_rtu_write_regs(m, 1, 1, regs, 2);
    CHECK(mn == 13 && mb_rtu_valid(m, mn) && m[6] == 4, "write regs frame");
}

static void test_tcp(void)
{
    const uint8_t adu[] = {0x12, 0x34, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    CHECK(mb_tcp_frame_len(adu, sizeof adu) == 12, "frame len");
    CHECK(mb_tcp_frame_len(adu, 9) == 0, "incomplete");
    const uint8_t notmb[] = {0x01, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x94, 0x09};  // protocol id != 0
    CHECK(mb_tcp_frame_len(notmb, 8) == -1, "not Modbus TCP");
    uint8_t rtu[64];
    uint16_t tid = 0;
    size_t n = mb_tcp_to_rtu(adu, sizeof adu, rtu, sizeof rtu, &tid);
    const uint8_t want[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
    CHECK(n == 8 && tid == 0x1234 && memcmp(rtu, want, 8) == 0, "tcp->rtu");
    uint8_t reply[] = {0x01, 0x03, 0x02, 0x00, 0x2A, 0, 0};
    size_t rn = mb_rtu_seal(reply, 5);
    uint8_t back[64];
    size_t bn = mb_rtu_to_tcp(reply, rn, tid, back, sizeof back);
    const uint8_t wantb[] = {0x12, 0x34, 0x00, 0x00, 0x00, 0x05, 0x01, 0x03, 0x02, 0x00, 0x2A};
    CHECK(bn == sizeof wantb && memcmp(back, wantb, bn) == 0, "rtu->tcp");
    reply[3] ^= 1;
    CHECK(mb_rtu_to_tcp(reply, rn, tid, back, sizeof back) == 0, "bad crc rejected");
}

static bool coils[4];
static uint16_t holding[4] = {10, 20, 30, 40};
static bool no_data;  // holding register 3 exists but has no value yet
static uint8_t rbit(void *ctx, uint8_t fc, uint16_t a, bool *o)
{
    if (a >= 4) return MB_EX_ILLEGAL_ADDRESS;
    *o = coils[a];
    return 0;
}
static uint8_t rreg(void *ctx, uint8_t fc, uint16_t a, uint16_t *o)
{
    if (a >= 4 || fc != 3) return MB_EX_ILLEGAL_ADDRESS;
    if (a == 3 && no_data) return MB_EX_TARGET_NO_RESPONSE;
    *o = holding[a];
    return 0;
}
static uint16_t block[8];
static uint16_t block_addr, block_n;
static uint8_t wregs(void *ctx, uint16_t a, const uint16_t *v, uint16_t n)
{
    if (a + n > 8) return MB_EX_ILLEGAL_ADDRESS;
    block_addr = a, block_n = n;
    memcpy(block + a, v, n * sizeof *v);
    return 0;
}
static uint8_t wbit(void *ctx, uint16_t a, bool on)
{
    if (a >= 4) return MB_EX_ILLEGAL_ADDRESS;
    coils[a] = on;
    return 0;
}
static bool exists(void *ctx, uint8_t fc, uint16_t a) { return a < 4; }

// Plan 10c task 5 (Codex review, Important 5): one snapshot per register read. begin/end wrap the whole
// FC3/FC4 loop, so a 32-bit value is never made of two samples.
static int s_begins, s_ends, s_inside_reads, s_outside_reads, s_fail_reg = -1;
static bool s_begin_ok = true, s_in;
static bool hbegin(void *ctx) { s_begins++; if (s_begin_ok) s_in = true; return s_begin_ok; }
static void hend(void *ctx) { s_ends++; s_in = false; }
static uint8_t hreg(void *ctx, uint8_t fc, uint16_t a, uint16_t *v)
{
    if (s_in) s_inside_reads++; else s_outside_reads++;
    if ((int)a == s_fail_reg) return MB_EX_ILLEGAL_ADDRESS;
    *v = (uint16_t)(a * 10);
    return 0;
}

static void test_serve_snapshot(void)
{
    const mb_map_t map = {.read_reg = hreg, .begin = hbegin, .end = hend};
    uint8_t out[64];
    const uint8_t rd4[] = {7, 3, 0, 0, 0, 4};
    size_t n = mb_serve(&map, rd4, sizeof rd4, out, sizeof out);
    CHECK(n == 11 && s_begins == 1 && s_ends == 1 && s_inside_reads == 4 && s_outside_reads == 0,
          "4 registers: begin once, 4 reads inside, end once (%d %d %d %d)", s_begins, s_ends, s_inside_reads, s_outside_reads);
    s_begins = s_ends = s_inside_reads = s_outside_reads = 0;
    s_begin_ok = false;
    n = mb_serve(&map, rd4, sizeof rd4, out, sizeof out);
    CHECK(n == 3 && out[1] == 0x83 && out[2] == MB_EX_DEVICE_FAILURE && s_ends == 0 && s_inside_reads + s_outside_reads == 0,
          "begin fails: device failure, nothing read, no end (%u %d)", out[2], s_ends);
    s_begin_ok = true;
    s_begins = s_ends = s_inside_reads = s_outside_reads = 0;
    s_fail_reg = 2;
    n = mb_serve(&map, rd4, sizeof rd4, out, sizeof out);
    CHECK(n == 3 && out[2] == MB_EX_ILLEGAL_ADDRESS && s_begins == 1 && s_ends == 1, "a read fails on the 3rd: exception, end once (%d)", s_ends);
    s_fail_reg = -1;
    const mb_map_t plain = {.read_reg = hreg};
    s_in = false;
    n = mb_serve(&plain, rd4, sizeof rd4, out, sizeof out);
    CHECK(n == 11 && out[4] == 0 && out[6] == 10, "a map without hooks reads as before");
}

static void test_serve(void)
{
    const mb_map_t map = {.read_bit = rbit, .read_reg = rreg, .write_bit = wbit, .exists = exists};
    uint8_t out[64];
    const uint8_t rd[] = {7, 3, 0, 1, 0, 2};
    size_t n = mb_serve(&map, rd, sizeof rd, out, sizeof out);
    const uint8_t want[] = {7, 3, 4, 0, 20, 0, 30};
    CHECK(n == 7 && memcmp(out, want, 7) == 0, "read holding");
    const uint8_t wr[] = {7, 5, 0, 2, 0xFF, 0};
    n = mb_serve(&map, wr, sizeof wr, out, sizeof out);
    CHECK(n == 6 && memcmp(out, wr, 6) == 0 && coils[2], "write coil echo");
    const uint8_t rc[] = {7, 1, 0, 0, 0, 4};
    n = mb_serve(&map, rc, sizeof rc, out, sizeof out);
    CHECK(n == 4 && out[2] == 1 && out[3] == 0x04, "read coils");
    const uint8_t bad[] = {7, 3, 0, 3, 0, 2};
    n = mb_serve(&map, bad, sizeof bad, out, sizeof out);
    CHECK(n == 3 && out[1] == 0x83 && out[2] == MB_EX_ILLEGAL_ADDRESS, "illegal address");
    // A point that exists but has no data yet: 0x0B, not "illegal address"
    no_data = true;
    const uint8_t nd[] = {7, 3, 0, 3, 0, 1};
    n = mb_serve(&map, nd, sizeof nd, out, sizeof out);
    CHECK(n == 3 && out[2] == MB_EX_TARGET_NO_RESPONSE, "no data yet -> 0x0B (%02x)", out[2]);
    no_data = false;
    const uint8_t nofc[] = {7, 6, 0, 0, 0, 1};
    n = mb_serve(&map, nofc, sizeof nofc, out, sizeof out);
    CHECK(n == 3 && out[1] == 0x86 && out[2] == MB_EX_ILLEGAL_FUNCTION, "no write_reg");
    const uint8_t wm[] = {7, 15, 0, 0, 0, 3, 1, 0x05};
    n = mb_serve(&map, wm, sizeof wm, out, sizeof out);
    CHECK(n == 6 && coils[0] && !coils[1] && coils[2], "write multiple coils");
    // FC15 over coils 2..5 when only 0..3 exist: refused, and nothing changed (Codex F11)
    memset(coils, 0, sizeof coils);
    const uint8_t wm_bad[] = {7, 15, 0, 2, 0, 4, 1, 0x0F};
    n = mb_serve(&map, wm_bad, sizeof wm_bad, out, sizeof out);
    CHECK(n == 3 && out[2] == MB_EX_ILLEGAL_ADDRESS && !coils[2] && !coils[3], "FC15 bad range changes nothing");
    // FC16 as one block when the map offers it (a float32 spans two registers)
    const mb_map_t blk = {.write_regs = wregs};
    const uint8_t w16[] = {7, 16, 0, 2, 0, 2, 4, 0x3F, 0xC0, 0x00, 0x00};
    n = mb_serve(&blk, w16, sizeof w16, out, sizeof out);
    CHECK(n == 6 && block_addr == 2 && block_n == 2 && block[2] == 0x3FC0 && block[3] == 0, "FC16 block write");
    const uint8_t w16bad[] = {7, 16, 0, 7, 0, 2, 4, 0, 1, 0, 2};
    n = mb_serve(&blk, w16bad, sizeof w16bad, out, sizeof out);
    CHECK(n == 3 && out[1] == 0x90 && out[2] == MB_EX_ILLEGAL_ADDRESS, "FC16 block refused -> exception 2");
    // RTU request lengths, for framing a byte stream
    const uint8_t r3[] = {1, 3, 0, 0, 0, 1}, r16[] = {1, 16, 0, 0, 0, 2, 4};
    CHECK(mb_rtu_request_len(r3, 2) == 8 && mb_rtu_request_len(r16, 7) == 13 && mb_rtu_request_len(r16, 3) == 9 &&
              mb_rtu_request_len((const uint8_t *)"\x01\x2b", 2) == 0,
          "RTU request lengths");
}

static bool m(const char *f, const char *t) { return mqtt_topic_match(f, t, strlen(t)); }

static void test_mqtt(void)
{
    CHECK(m("a/b", "a/b") && !m("a/b", "a/bc") && !m("a/b", "a"), "exact");
    CHECK(m("a/+/c", "a/x/c") && !m("a/+/c", "a/x/y/c") && m("a/+", "a/") , "plus");
    CHECK(m("a/#", "a/b/c") && m("a/#", "a") && m("#", "x/y") && !m("b/#", "a/b"), "hash");
    CHECK(!m("#", "$SYS/x") && m("$SYS/#", "$SYS/x"), "dollar");
    char topic[32];
    int qos;
    bool retain;
    const char *p;
    size_t pl;
    const char *d = "site/a,1,1,v=1,2";
    CHECK(mqtt_custom_parse(d, strlen(d), topic, sizeof topic, &qos, &retain, &p, &pl) && strcmp(topic, "site/a") == 0 &&
              qos == 1 && retain && pl == 5 && memcmp(p, "v=1,2", 5) == 0,
          "custom parse");
    CHECK(!mqtt_custom_parse("x,3,0,a", 7, topic, sizeof topic, &qos, &retain, &p, &pl), "bad qos");
    CHECK(!mqtt_custom_parse("noformat", 8, topic, sizeof topic, &qos, &retain, &p, &pl), "no commas");
    CHECK(!mqtt_custom_parse("a/+,0,0,x", 9, topic, sizeof topic, &qos, &retain, &p, &pl), "wildcard topic");
    CHECK(mqtt_custom_parse("t,0,0,", 6, topic, sizeof topic, &qos, &retain, &p, &pl) && pl == 0, "empty payload");
    // The reference unit's example: retain as ON
    d = "/PubTopic3,0,ON,{\"message\":\"hello from M100\"}";
    CHECK(mqtt_custom_parse(d, strlen(d), topic, sizeof topic, &qos, &retain, &p, &pl) && retain && qos == 0 &&
              strcmp(topic, "/PubTopic3") == 0 && pl == strlen("{\"message\":\"hello from M100\"}"),
          "custom ON");
    CHECK(mqtt_custom_parse("t,1,off,x", 9, topic, sizeof topic, &qos, &retain, &p, &pl) && !retain, "custom off");
    CHECK(!mqtt_custom_parse("t,1,yes,x", 9, topic, sizeof topic, &qos, &retain, &p, &pl), "custom bad retain");
    // Topic distribution: "alias,payload", the alias is a whole field
    const char *a = "topic alias,payload-test,{\"message\":\"hello\"}";
    CHECK(mqtt_alias_match("topic alias", a, strlen(a)) == 12, "alias match");
    CHECK(mqtt_alias_match("topic", a, strlen(a)) == 0, "alias prefix only");
    CHECK(mqtt_alias_match("#A", "#Axyz", 5) == 0 && mqtt_alias_match("#A", "#A,xyz", 6) == 3, "alias needs comma");
}

// Template slots for the tests: a handle is a pointer into this table.
typedef struct {
    const char *name;
    edge_kind_t kind;
    double num;
    const char *text;
} test_slot_t;
static const test_slot_t SLOTS[] = {
    {"temp", EDGE_NUMBER, 21.5, NULL},
    {"count", EDGE_NUMBER, 7, NULL},
    {"dead", EDGE_NO_VALUE, 0, NULL},  // known but unread
    {"sys_mac", EDGE_TEXT, 0, "AA:BB:CC:DD:EE:FF"},
    {"sys_unix_time", EDGE_NUMBER, 1760400000, NULL},
    {"quoted", EDGE_TEXT, 0, "say \"hi\"\n"},
    {"broken", EDGE_NUMBER, 0, NULL},  // value set to NaN below
};

static void *resolve(void *ctx, const char *name)
{
    for (size_t i = 0; i < sizeof SLOTS / sizeof SLOTS[0]; i++) {
        if (strcmp(SLOTS[i].name, name) == 0) return (void *)&SLOTS[i];
    }
    return NULL;  // a constant
}

static edge_kind_t value(void *ctx, void *slot, double *v, const char **text)
{
    const test_slot_t *s = slot;
    *v = strcmp(s->name, "broken") == 0 ? NAN : s->num;
    *text = s->text;
    return s->kind;
}

/** Compiles and renders; checks the output (NULL want = compile must fail). */
static void tmpl(const char *template, bool quote, const char *ph, const char *want, size_t slots)
{
    edge_tmpl_t *t = edge_tmpl_compile(template, resolve, NULL, quote, ph);
    if (!want) {
        CHECK(!t, "template should be refused: %s", template);
        edge_tmpl_free(t);
        return;
    }
    CHECK(t, "template refused: %s", template);
    if (!t) return;
    char out[1024];
    size_t n = edge_tmpl_render(t, value, NULL, out, sizeof out);
    CHECK(n && strcmp(out, want) == 0, "render\n  tmpl %s\n  got  %s\n  want %s", template, n ? out : "(0)", want);
    CHECK(edge_tmpl_slots(t) == slots, "slots %zu, want %zu", edge_tmpl_slots(t), slots);
    CHECK(edge_tmpl_size(t) >= n + 1, "size bound %zu < %zu", edge_tmpl_size(t), n + 1);
    CHECK(edge_tmpl_render(t, value, NULL, out, n) == 0, "a buffer one byte short is refused");
    edge_tmpl_free(t);
}

static void test_edge(void)
{
    uint16_t r[4];
    CHECK(edge_dtype("float32_cdab") == DT_FLOAT32_CDAB && edge_dtype("x") == -1, "dtype names");
    CHECK(edge_dtype_regs(DT_FLOAT64) == 4 && edge_dtype_regs(DT_INT32_CDAB) == 2 && edge_dtype_regs(DT_BOOL) == 1, "regs");
    uint16_t f_abcd[] = {0x3FC0, 0x0000}, f_cdab[] = {0x0000, 0x3FC0}, i_cdab[] = {0xFFFE, 0xFFFF};
    CHECK(edge_decode(DT_FLOAT32_ABCD, f_abcd) == 1.5 && edge_decode(DT_FLOAT32_CDAB, f_cdab) == 1.5, "float32");
    CHECK(edge_decode(DT_INT32_CDAB, i_cdab) == -2 && edge_decode(DT_UINT32_CDAB, i_cdab) == 4294967294.0, "int32");
    uint16_t d[] = {0x3FF0, 0, 0, 0}, s16[] = {0xFF85};
    CHECK(edge_decode(DT_FLOAT64, d) == 1.0 && edge_decode(DT_INT16, s16) == -123 && edge_decode(DT_INT8, s16) == -123, "f64/i16/i8");
    CHECK(edge_encode(DT_FLOAT32_CDAB, 1.5, r) == 2 && r[0] == 0 && r[1] == 0x3FC0, "encode float");
    CHECK(edge_encode(DT_INT16, -123, r) == 1 && r[0] == 0xFF85, "encode int16");
    CHECK(edge_encode(DT_FLOAT64, -2.25, r) == 4 && edge_decode(DT_FLOAT64, r) == -2.25, "float64 round trip");

    double v, args[] = {200, 5};
    CHECK(edge_formula_eval("=%s*0.1", args, 1, &v) && fabs(v - 20) < 1e-9, "scale");
    CHECK(edge_formula_eval("=(%s-%s)/2+-1", args, 2, &v) && v == 96.5, "precedence/unary");
    CHECK(!edge_formula_eval("=%s/0", args, 1, &v) && !edge_formula_eval("=%s+%s", args, 1, &v), "div0 / missing arg");
    CHECK(!edge_formula_eval("=2*", args, 0, &v) && !edge_formula_eval("2", args, 0, &v), "syntax");
    char e[64], names[8][EDGE_NAME_MAX];
    int n = edge_formula_split("=%s*2+%s, a ,bb", e, sizeof e, names, 8);
    CHECK(n == 2 && strcmp(e, "=%s*2+%s") == 0 && strcmp(names[0], "a") == 0 && strcmp(names[1], "bb") == 0, "split");
    CHECK(edge_formula_split("=%s*0.1", e, sizeof e, names, 8) == 0 && strcmp(e, "=%s*0.1") == 0, "split no refs");

    uint8_t q[32];
    size_t qn = dlt645_read_req("123456789012", 0x02010100, q, sizeof q);
    const uint8_t want[] = {0xFE, 0xFE, 0xFE, 0xFE, 0x68, 0x12, 0x90, 0x78, 0x56, 0x34, 0x12, 0x68,
                            0x11, 0x04, 0x33, 0x34, 0x34, 0x35, 0x6B, 0x16};
    CHECK(qn == sizeof want && memcmp(q, want, qn) == 0, "dlt645 request");
    // Reply: 220.1 V as BCD 2201 (low byte first, +0x33)
    uint8_t rep[] = {0xFE, 0x68, 0x12, 0x90, 0x78, 0x56, 0x34, 0x12, 0x68, 0x91, 0x06,
                     0x33, 0x34, 0x34, 0x35, 0x01 + 0x33, 0x22 + 0x33, 0, 0x16};
    uint8_t cs = 0;
    for (int i = 1; i < 17; i++) cs += rep[i];
    rep[17] = cs;
    CHECK(dlt645_parse(rep, sizeof rep, "123456789012", 0x02010100, &v) == 0 && v == 2201, "dlt645 parse (%g)", v);
    CHECK(dlt645_parse(rep, sizeof rep, "123456789013", 0x02010100, &v) == -2, "other meter");
    rep[17]++;
    CHECK(dlt645_parse(rep, sizeof rep, "123456789012", 0x02010100, &v) == -2, "bad checksum");
    uint8_t err[] = {0x68, 0x12, 0x90, 0x78, 0x56, 0x34, 0x12, 0x68, 0xD1, 0x01, 0x35, 0, 0x16};
    cs = 0;
    for (int i = 0; i < 11; i++) cs += err[i];
    err[11] = cs;
    CHECK(dlt645_parse(err, sizeof err, "123456789012", 0x02010100, &v) == -1, "error reply");

    tmpl("{\"t\":\"temp\",\"x\":{\"c\":\"count\",\"d\":\"dead\"},\"k\":1}", false, NULL,
         "{\"t\":21.5,\"x\":{\"c\":7,\"d\":null},\"k\":1}", 3);
    tmpl("{\"t\":\"temp\",\"d\":\"dead\"}", true, "N/A", "{\"t\":\"21.5\",\"d\":\"N/A\"}", 2);
    tmpl("{\"d\":\"dead\"}", false, "-1", "{\"d\":-1}", 1);
    tmpl("[1]", false, NULL, NULL, 0);
    tmpl("not json", false, NULL, NULL, 0);
    // The reference unit's example: system fields, and "user_define" constants kept as written.
    tmpl("{\"service\":[{\"s1\":{\"t\":\"temp\",\"user_define\":\"bedroom\"},\"at\":\"sys_unix_time\"}],\"device_id\":\"sys_mac\"}",
         false, NULL,
         "{\"service\":[{\"s1\":{\"t\":21.5,\"user_define\":\"bedroom\"},\"at\":1760400000}],\"device_id\":\"AA:BB:CC:DD:EE:FF\"}", 3);
    // Duplicate keys each keep their own value; arrays of slots; no slots at all.
    tmpl("{\"a\":\"temp\",\"a\":\"count\"}", false, NULL, "{\"a\":21.5,\"a\":7}", 2);
    tmpl("{\"v\":[\"temp\",\"count\",\"x\"]}", false, NULL, "{\"v\":[21.5,7,\"x\"]}", 2);
    tmpl("{\"k\":\"const\"}", false, NULL, "{\"k\":\"const\"}", 0);
    // A constant holding a control character stays escaped (never mistaken for a slot); text is escaped.
    tmpl("{\"c\":\"a\\u0001b\",\"q\":\"quoted\"}", false, NULL, "{\"c\":\"a\\u0001b\",\"q\":\"say \\\"hi\\\"\\n\"}", 1);
    // NaN from a float register would not be JSON: the placeholder instead.
    tmpl("{\"f\":\"broken\"}", false, "-1", "{\"f\":-1}", 1);
    // Plan 10c task 10 (Codex review, Important 10): a placeholder is a literal only when all of it parses;
    // "1abc" copied raw made the whole report invalid JSON.
    tmpl("{\"d\":\"dead\"}", false, "1abc", "{\"d\":\"1abc\"}", 1);
    tmpl("{\"d\":\"dead\"}", false, "null-device", "{\"d\":\"null-device\"}", 1);
    tmpl("{\"d\":\"dead\"}", false, "truex", "{\"d\":\"truex\"}", 1);
    tmpl("{\"d\":\"dead\"}", false, "2.5", "{\"d\":2.5}", 1);
    tmpl("{\"d\":\"dead\"}", false, "true", "{\"d\":true}", 1);
    tmpl("{\"d\":\"dead\"}", false, "null", "{\"d\":null}", 1);
    // Codex 10c code review: numbers cJSON takes but JSON does not (RFC 8259) are text too.
    tmpl("{\"d\":\"dead\"}", false, "01", "{\"d\":\"01\"}", 1);
    tmpl("{\"d\":\"dead\"}", false, "1.", "{\"d\":\"1.\"}", 1);
    tmpl("{\"d\":\"dead\"}", false, "-0.5e3", "{\"d\":-0.5e3}", 1);

    edge_index_t ix;
    CHECK(edge_index_init(&ix, 300) && ix.cap >= 600, "index init");
    static char keys[300][16];
    for (int i = 0; i < 300; i++) {
        snprintf(keys[i], sizeof keys[i], "n%d", i);
        CHECK(edge_index_put(&ix, keys[i], &keys[i]), "put %d", i);
    }
    int found = 0;
    for (int i = 0; i < 300; i++) found += edge_index_get(&ix, keys[i]) == &keys[i];
    CHECK(found == 300, "all 300 names found (%d)", found);
    CHECK(!edge_index_get(&ix, "nope") && !edge_index_get(&ix, "n300"), "missing names");
    CHECK(!edge_index_put(&ix, "n7", NULL), "duplicate refused");
    edge_index_free(&ix);

    CHECK(edge_condition(0, 1, 0, 0) && !edge_condition(1, 1, 0, 0), "follow/opposite");
    CHECK(edge_condition(4, 5, 1, 10) && !edge_condition(4, 11, 1, 10) && edge_condition(5, 11, 1, 10), "between/outside");
    CHECK(edge_condition(2, 1, 1, 0) && !edge_condition(6, 1, 1, 0) && edge_condition(7, 0, 0, 1), ">=, >, <");
}

static bool rd(void *ctx, const char *name, char *value, size_t max)
{
    if (strcmp(name, "temperature") == 0) return snprintf(value, max, "25.3"), true;
    return false;
}
static char written[32];
static bool wr(void *ctx, const char *name, const char *value)
{
    if (strcmp(name, "humidity") != 0) return false;
    snprintf(written, sizeof written, "%s", value);
    return true;
}
static void rw(const char *req, const char *want, rwprot_write_fn w)
{
    char *got = rwprot_handle(req, strlen(req), rd, w, NULL);
    CHECK(want ? got && strcmp(got, want) == 0 : !got, "rw_prot\n  req  %s\n  got  %s\n  want %s", req, got, want);
    free(got);
}

// Plan 10c task 1 (Codex review, Critical 1): JSON nested past 16 levels is refused before it can overflow a
// task's stack (cJSON recurses about 48 B a level; mqtt_io has 4 KB). A valid request plus a deep member "z":
// the whole document `depth` levels deep.
static char *deep_request(int depth)
{
    static char buf[2048];
    int k = depth - 1, n = snprintf(buf, sizeof buf, "{\"rw_prot\":{\"ver\":\"1.0\",\"dir\":\"down\",\"id\":\"7\","
                                                     "\"r_data\":[{\"name\":\"temperature\"}]},\"z\":");
    for (int i = 0; i < k; i++) buf[n++] = '[';
    buf[n++] = '0';
    for (int i = 0; i < k; i++) buf[n++] = ']';
    buf[n++] = '}';
    buf[n] = 0;
    return buf;
}

// Plan 10c task 7 (Codex review, Important 7): rw_prot out of memory at any allocation answers NULL or valid
// JSON, and leaks nothing (LeakSanitizer at exit).
static int s_fail_at = -1;
static void *failing_malloc(size_t n)
{
    if (s_fail_at >= 0 && s_fail_at-- == 0)
        return NULL;
    return malloc(n);
}

static void test_rwprot_oom(void)
{
    const char *req = "{\"rw_prot\":{\"ver\":\"1.0\",\"dir\":\"down\",\"id\":\"9\",\"r_data\":[{\"name\":\"temperature\"},"
                      "{\"name\":\"humidity\"},{\"name\":\"nope\"}],\"w_data\":[{\"name\":\"humidity\",\"value\":\"40\"},"
                      "{\"name\":\"nope\",\"value\":1}]}}";
    cJSON_Hooks hooks = {failing_malloc, free};
    cJSON_InitHooks(&hooks);
    int answers = 0, nulls = 0;
    for (int n = 0; n < 1000; n++) {
        s_fail_at = n;
        char *got = rwprot_handle(req, strlen(req), rd, wr, NULL);
        bool hit = s_fail_at < 0;
        s_fail_at = -1;
        if (got) {
            cJSON *back = cJSON_Parse(got);
            CHECK(back != NULL, "allocation %d failed: the answer is JSON (%s)", n, got);
            cJSON_Delete(back);
            answers++;
        } else {
            nulls++;
        }
        free(got);
        if (!hit) break;
    }
    cJSON_InitHooks(NULL);
    CHECK(nulls > 0 && answers > 0, "the sweep met failures (%d) and an answer (%d)", nulls, answers);
}

static void test_depth_limit(void)
{
    const char *r16 = deep_request(16);
    char *got = rwprot_handle(r16, strlen(r16), rd, wr, NULL);
    CHECK(got && strstr(got, "25.3"), "16 levels: answered (%s)", got);
    free(got);
    const int too_deep[] = {17, 300};
    for (int i = 0; i < 2; i++) {
        const char *r = deep_request(too_deep[i]);
        got = rwprot_handle(r, strlen(r), rd, wr, NULL);
        CHECK(!got, "%d levels: refused, not answered (%s)", too_deep[i], got);
        free(got);
    }
    char t[256];
    for (int d = 16; d <= 17; d++) {  // {"a":{"a":...{"a":"temperature"}}}: d objects
        int n = 0;
        for (int i = 0; i < d; i++) n += snprintf(t + n, sizeof t - n, "{\"a\":");
        n += snprintf(t + n, sizeof t - n, "\"temperature\"");
        for (int i = 0; i < d; i++) t[n++] = '}';
        t[n] = 0;
        edge_tmpl_t *c = edge_tmpl_compile(t, resolve, NULL, false, NULL);
        CHECK(d == 16 ? c != NULL : c == NULL, "a %d-level template: %s", d, d == 16 ? "compiled" : "refused");
        edge_tmpl_free(c);
    }
}

static void test_rwprot(void)
{
    // The reference manual's read and write examples
    rw("{\"rw_prot\":{\"Ver\":\"1.0.1\",\"dir\":\"down\",\"id\":\"12345\",\"r_data\":[{\"name\":\"temperature\"},{\"name\":\"humidity\"}]}}",
       "{\"rw_prot\":{\"Ver\":\"1.0.1\",\"dir\":\"up\",\"id\":\"12345\",\"r_data\":[{\"name\":\"temperature\",\"value\":\"25.3\",\"err\":\"0\"},"
       "{\"name\":\"humidity\",\"value\":\"\",\"err\":\"1\"}]}}", wr);
    rw("{\"rw_prot\":{\"ver\":\"1.0.1\",\"dir\":\"down\",\"id\":7,\"w_data\":[{\"name\":\"humidity\",\"value\":\"52\"},{\"name\":\"temperature\",\"value\":15}]}}",
       "{\"rw_prot\":{\"Ver\":\"1.0.1\",\"dir\":\"up\",\"id\":\"7\",\"w_data\":[{\"name\":\"humidity\",\"value\":\"52\",\"err\":\"0\"},"
       "{\"name\":\"temperature\",\"value\":\"15\",\"err\":\"1\"}]}}", wr);
    CHECK(strcmp(written, "52") == 0, "write reached the callback");
    // writes disabled
    rw("{\"rw_prot\":{\"Ver\":\"1.0.1\",\"dir\":\"down\",\"id\":\"1\",\"w_data\":[{\"name\":\"humidity\",\"value\":\"1\"}]}}",
       "{\"rw_prot\":{\"Ver\":\"1.0.1\",\"dir\":\"up\",\"id\":\"1\",\"w_data\":[{\"name\":\"humidity\",\"value\":\"1\",\"err\":\"1\"}]}}", NULL);
    // bad header / no data -> error protocol (as valid JSON)
    rw("{\"rw_prot\":{\"Ver\":\"1.0.1\",\"dir\":\"up\",\"id\":\"9\",\"r_data\":[]}}",
       "{\"rw_prot\":{\"Ver\":\"1.0.1\",\"dir\":\"up\",\"id\":\"9\",\"err\":\"1\"}}", wr);
    rw("{\"rw_prot\":{\"Ver\":\"1.0.1\",\"dir\":\"down\"}}", "{\"rw_prot\":{\"Ver\":\"1.0.1\",\"dir\":\"up\",\"err\":\"1\"}}", wr);
    // at most 5 reads
    char req[400] = "{\"rw_prot\":{\"Ver\":\"1.0.1\",\"dir\":\"down\",\"id\":\"x\",\"r_data\":[";
    for (int i = 0; i < 6; i++) strcat(req, i ? ",{\"name\":\"temperature\"}" : "{\"name\":\"temperature\"}");
    strcat(req, "]}}");
    char *got = rwprot_handle(req, strlen(req), rd, wr, NULL);
    int ones = 0;
    for (char *q = got; q && (q = strstr(q, "\"err\":\"1\"")); q++) ones++;
    CHECK(ones == 1, "the 6th read is refused (%d)", ones);
    int zeros = 0;
    for (char *s = got; (s = strstr(s, "\"err\":\"0\"")); s++) zeros++;
    CHECK(zeros == 5, "five reads served (%d)", zeros);
    free(got);
    rw("{\"temp\":1}", NULL, wr);  // not rw_prot: passed on
    rw("plain text", NULL, wr);
}

static void test_ipacl(void)
{
    ipacl_t a;
    CHECK(ipacl_parse("", &a) && a.n == 0 && ipacl_allows(&a, 0x01020304), "empty list allows anyone");
    CHECK(ipacl_parse(" 192.168.1.0/24, 10.0.0.5;172.16.9.9/16 ", &a) && a.n == 3, "three entries");
    CHECK(ipacl_allows(&a, 0xC0A80142) && !ipacl_allows(&a, 0xC0A80242), "/24 range");
    CHECK(ipacl_allows(&a, 0x0A000005) && !ipacl_allows(&a, 0x0A000006), "single host");
    CHECK(ipacl_allows(&a, 0xAC100001), "/16 masks the host part of the entry");
    CHECK(ipacl_parse("0.0.0.0/0", &a) && ipacl_allows(&a, 0xFFFFFFFF), "/0 allows all");
    CHECK(!ipacl_parse("1.2.3", &a) && a.n == 0, "short address rejected");
    CHECK(!ipacl_parse("1.2.3.4/", &a), "missing prefix rejected");
    CHECK(!ipacl_parse("1.2.3.4x", &a), "trailing junk rejected");
    CHECK(!ipacl_parse("1.2.3.1000", &a), "octet > 255 rejected");
}

int main(void)
{
    test_ipacl();
    test_rwprot();
    test_depth_limit();
    test_rwprot_oom();
    test_edge();
    test_mqtt();
    test_crc();
    test_tcp();
    test_serve();
    test_serve_snapshot();
    printf(failures ? "%d FAILED\n" : "proto: all tests passed\n", failures);
    return failures != 0;
}
