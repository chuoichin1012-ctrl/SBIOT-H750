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
    test_walk();
    test_keys();
    printf(failures ? "%d FAILED\n" : "jsonscan: all tests passed\n", failures);
    return failures != 0;
}
