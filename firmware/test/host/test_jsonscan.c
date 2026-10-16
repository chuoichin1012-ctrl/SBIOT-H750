// Host tests for proto/jsonscan.c: make -C stm32/firmware/test/host
#include <stdio.h>
#include <string.h>
#include "jsonscan.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static bool valid(const char *s)
{
    js_span_t r;
    return js_parse_root(s, strlen(s), &r);
}

static void test_validity(void)
{
    const char *good[] = {"[]", " {} ", "0", "-0.5e+10", "\"a\\u00e9\\n\"", "true", "null", "[1,[2,{\"a\":[]}],\"x\"]",
                          "{\"a\":1,\"a\":2}", "\t[ 1 , 2 ]\r\n", "\"\\uD83D\\uDE00\"", "\"\xc3\xa9\"", "\"\xe2\x82\xac\"",
                          "\"\xf0\x9f\x98\x80\"", "\"\\u0000\""};
    const char *bad[] = {"", " ", "[", "[1,]", "{\"a\"}", "{\"a\":}", "{a:1}", "01", "1.", "+1", ".5", "1e", "\"\\x\"",
                         "\"\\u12g4\"", "\"tab\there\"", "tru", "nul", "[1]x", "[1] [2]", "{\"a\":1,}", "\"open",
                         // surrogates as cJSON wants them: a high one followed by a low one, never alone
                         "\"\\uD800\"", "\"\\uDC00\"", "\"\\uD800x\"", "\"\\uD800\\u0041\"", "{\"\\uD800\":1}",
                         // UTF-8 (RFC 3629): overlong, encoded surrogate, beyond U+10FFFF, stray and cut bytes
                         "\"\xc0\x80\"", "\"\xe0\x80\x80\"", "\"\xed\xa0\x80\"", "\"\xf4\x90\x80\x80\"", "\"\xff\"",
                         "\"\x80\"", "\"\xc3\"", "[\"ok\",\"\xe2\x82\"]"};
    for (size_t i = 0; i < sizeof good / sizeof *good; i++) CHECK(valid(good[i]), "rejected valid %s", good[i]);
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(!valid(bad[i]), "accepted invalid %s", bad[i]);
    char deep[2 * JS_MAX_DEPTH + 3];
    memset(deep, '[', JS_MAX_DEPTH);
    memset(deep + JS_MAX_DEPTH, ']', JS_MAX_DEPTH);
    deep[2 * JS_MAX_DEPTH] = '\0';
    CHECK(valid(deep), "JS_MAX_DEPTH levels must pass");
    memset(deep, '[', JS_MAX_DEPTH + 1);
    memset(deep + JS_MAX_DEPTH + 1, ']', JS_MAX_DEPTH + 1);
    deep[2 * JS_MAX_DEPTH + 2] = '\0';
    CHECK(!valid(deep), "JS_MAX_DEPTH + 1 levels must fail");
}

// ---- the streaming scanner (Plan 09c) -------------------------------------------------------------

static bool stream_chunks(const char *t, size_t n, js_type_t want, size_t chunk, size_t cut)
{
    js_stream_t st;
    js_stream_init(&st, want);
    if (cut) {  // two pieces, cut at `cut`
        js_stream_feed(&st, t, cut);
        js_stream_feed(&st, t + cut, n - cut);
    } else {
        for (size_t i = 0; i < n; i += chunk) js_stream_feed(&st, t + i, n - i < chunk ? n - i : chunk);
    }
    return js_stream_end(&st);
}

static int s_stream_cases;

/** The stream gives js_parse_root's verdict for `t`, whatever the pieces, for any root type and for arrays. */
static void stream_same(const char *t, size_t n)
{
    js_span_t r;
    bool any = js_parse_root(t, n, &r), arr = any && js_type(r) == JS_ARRAY;
    bool ok = true;
    const size_t chunks[] = {0, 1, 3, 7};  // 0: whole
    for (int w = 0; w < 2; w++) {
        js_type_t want = w ? JS_ARRAY : JS_INVALID;
        bool expect = w ? arr : any;
        for (size_t c = 0; c < 4; c++) ok &= stream_chunks(t, n, want, chunks[c] ? chunks[c] : (n ? n : 1), 0) == expect;
        for (size_t cut = 1; cut < n; cut++) ok &= stream_chunks(t, n, want, 0, cut) == expect;
    }
    s_stream_cases++;
    CHECK(ok, "the stream disagrees with js_parse_root on \"%.*s\" (%s)", (int)(n > 60 ? 60 : n), t, any ? "valid" : "invalid");
}

static void test_stream(void)
{
    const char *cases[] = {"[]", " {} ", "0", "-0.5e+10", "\"a\\u00e9\\n\"", "true", "null", "[1,[2,{\"a\":[]}],\"x\"]",
                           "{\"a\":1,\"a\":2}", "\t[ 1 , 2 ]\r\n", "\"\\uD83D\\uDE00\"", "\"\xc3\xa9\"", "\"\xe2\x82\xac\"",
                           "\"\xf0\x9f\x98\x80\"", "\"\\u0000\"", "123", "123 ", "-0", "0e5", "[0.0,1E-2,-12.5e3]",
                           "", " ", "[", "[1,]", "{\"a\"}", "{\"a\":}", "{a:1}", "01", "1.", "+1", ".5", "1e", "\"\\x\"",
                           "\"\\u12g4\"", "\"tab\there\"", "tru", "nul", "[1]x", "[1] [2]", "{\"a\":1,}", "\"open",
                           "\"\\uD800\"", "\"\\uDC00\"", "\"\\uD800x\"", "\"\\uD800\\u0041\"", "{\"\\uD800\":1}",
                           "\"\xc0\x80\"", "\"\xe0\x80\x80\"", "\"\xed\xa0\x80\"", "\"\xf4\x90\x80\x80\"", "\"\xff\"",
                           "\"\x80\"", "\"\xc3\"", "[\"ok\",\"\xe2\x82\"]", "[1 2]", "{\"a\" 1}", "[-]", "[1e+]", "[\"a\\", "[\"\\u12",
                           "{\"a\":[1,{\"b\":null}],\"c\":\"d\"}", "[true,false,null]", "[truex]", "[nulll]", "{\"a\":1}}", "]",
                           "[\"\\/\\b\\f\\r\\t\\\"\\\\\"]", "{\"\":\"\"}", "[{},[],{}]", "[1,{\"a\":\"\xf4\x8f\xbf\xbf\"}]"};
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) stream_same(cases[i], strlen(cases[i]));
    char deep[2 * JS_MAX_DEPTH + 3];
    for (int extra = 0; extra < 2; extra++) {
        int d = JS_MAX_DEPTH + extra;
        memset(deep, '[', (size_t)d);
        memset(deep + d, ']', (size_t)d);
        stream_same(deep, 2 * (size_t)d);
    }
    stream_same("\"a\0b\"", 5);  // a raw NUL inside a string: invalid either way
    // Random texts over the characters that matter: the stream and js_parse_root agree on every one.
    static const char alpha[] = "[]{}\":,0123456789eE.+- tfnrul\\\xc3\xa9\xed\xa0\x80uD8C0\x01";
    unsigned seed = 12345;
    int disagree = 0;
    for (int k = 0; k < 20000; k++) {
        char t[16];
        size_t n = (size_t)((seed = seed * 1103515245u + 12345u) >> 16) % 13;
        for (size_t i = 0; i < n; i++) t[i] = alpha[((seed = seed * 1103515245u + 12345u) >> 16) % (sizeof alpha - 1)];
        js_span_t r;
        bool want = js_parse_root(t, n, &r);
        disagree += stream_chunks(t, n, JS_INVALID, n ? n : 1, 0) != want || stream_chunks(t, n, JS_INVALID, 1, 0) != want;
    }
    CHECK(disagree == 0, "random texts: the stream disagrees on %d of 20000", disagree);
    printf("  stream: %d texts, each whole, cut at every byte, byte by byte, in chunks of 3 and 7\n", s_stream_cases);
}

static void test_walk(void)
{
    const char *s = "[ {\"a\":1} , [2,3], \"s,]\" ,4 ]";
    js_span_t r, k, v;
    CHECK(js_parse_root(s, strlen(s), &r) && js_type(r) == JS_ARRAY, "root");
    CHECK(js_count(r) == 4, "count %zu", js_count(r));
    js_iter_t it;
    js_iter_init(&it, r);
    const char *want[] = {"{\"a\":1}", "[2,3]", "\"s,]\"", "4"};
    for (int i = 0; i < 4; i++) {
        CHECK(js_iter_next(&it, &k, &v) && v.n == strlen(want[i]) && memcmp(v.p, want[i], v.n) == 0, "element %d", i);
        CHECK(k.n == 0, "array elements have no key");
    }
    CHECK(!js_iter_next(&it, &k, &v), "end of array");

    const char *o = "{ \"x\" : [1] , \"y\":{\"z\":null} }";
    CHECK(js_parse_root(o, strlen(o), &r) && js_type(r) == JS_OBJECT, "object root");
    js_iter_init(&it, r);
    CHECK(js_iter_next(&it, &k, &v) && js_key_is(k, "x", false) && v.n == 3 && js_type(v) == JS_ARRAY, "member x");
    CHECK(js_iter_next(&it, &k, &v) && js_key_is(k, "y", false) && js_type(v) == JS_OBJECT && js_count(v) == 1, "member y");
    CHECK(!js_iter_next(&it, &k, &v), "end of object");

    js_span_t num = {"5", 1};
    CHECK(js_count(num) == 0, "a scalar has no children");
    js_span_t none = {"", 0};
    js_iter_init(&it, none);
    CHECK(!js_iter_next(&it, &k, &v), "an empty span has no children");
}

static void test_keys(void)
{
    js_span_t k1 = {"\"name\"", 6}, k2 = {"\"na\\u006De\"", 11}, k3 = {"\"NoDeS\"", 7}, k4 = {"\"name\\u0000junk\"", 16},
              k5 = {"\"nam\"", 5}, k6 = {"\"n\\u00e9\"", 9};
    CHECK(js_key_is(k1, "name", false), "plain");
    CHECK(js_key_is(k2, "name", false), "escaped letter");
    CHECK(js_key_is(k3, "nodes", true) && !js_key_is(k3, "nodes", false), "case only when asked");
    CHECK(js_key_is(k4, "name", false), "cJSON keys end at a decoded NUL");
    CHECK(!js_key_is(k5, "name", false) && !js_key_is(k1, "nam", false), "prefixes differ");
    CHECK(!js_key_is(k6, "n", false), "non-ASCII never matches");
}

int main(void)
{
    test_validity();
    test_stream();
    test_walk();
    test_keys();
    printf(failures ? "%d FAILED\n" : "jsonscan: all tests passed\n", failures);
    return failures != 0;
}
