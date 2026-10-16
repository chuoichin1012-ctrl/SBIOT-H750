// cfg_validate_text: the round trip, the 10 240-byte limit and the heap peak for the configs other than
// edgeDevices (spec §9.3). cJSON runs on a metered heap (heap_meter.h, Plan 09c) limited to 60 KB of live
// blocks, so "no out-of-memory here" proves the need is at most 60 KB (on the 64-bit host, which needs more
// than the 32-bit target): make -C stm32/firmware/test/host
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfgmodel.h"
#include "heap_meter.h"

static const cfg_env_t ENV = {.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};
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

static size_t s_worst;  // highest live-block peak seen in this run
static char s_out[CFG_SMALL_BUF], s_err[200];

/** cfg_validate_text on the metered heap; its peak of live blocks is noted. */
static cfg_result_t run(const char *name, const char *text, const cJSON *current)
{
    meter_peak_restart();
    cfg_result_t r = cfg_validate_text(&ENV, name, text, strlen(text), current, s_out, sizeof s_out, s_err, sizeof s_err);
    size_t peak = meter_peak_restart();
    if (peak > s_worst) s_worst = peak;
    CHECK(r != CFG_NO_MEMORY, "%s: out of memory within 60 KB of live blocks (%s)", name, s_err);
    return r;
}

/** The tree-based reference, on the heap: normalised length of `text`, or 0 if it is rejected. */
static size_t reference_len(const char *name, const char *text)
{
    char err[200];
    cJSON *in = cJSON_Parse(text);
    cJSON *out = in ? cfg_validate(&ENV, name, in, NULL, err, sizeof err) : NULL;
    char *t = out ? cJSON_PrintUnformatted(out) : NULL;
    size_t n = t ? strlen(t) : 0;
    cJSON_free(t);
    cJSON_Delete(out);
    cJSON_Delete(in);
    return n;
}

static bool accepted(const char *name, const char *text) { return reference_len(name, text) > 0; }

/**
 * The largest document the schema accepts for `name`, found by probing the reference
 * validator: every string field as long as it allows (the last three characters are
 * an index, so list items stay unique), and a list as long as it allows.
 */
static char *largest_doc(const char *name)
{
    cJSON *def = cfg_default(&ENV, name);
    bool list = cJSON_IsArray(def);
    cJSON *item;
    if (list) {  // one item with every field at its default
        cJSON *probe = cJSON_Parse("[{}]");
        char err[200];
        cJSON *v = cfg_validate(&ENV, name, probe, NULL, err, sizeof err);
        item = v ? cJSON_DetachItemFromArray(v, 0) : cJSON_CreateObject();
        cJSON_Delete(v);
        cJSON_Delete(probe);
    } else {
        item = cJSON_Duplicate(def, true);
    }
    cJSON_Delete(def);
    cJSON *f;
    cJSON_ArrayForEach(f, item)
    {
        if (!cJSON_IsString(f)) continue;
        int lo = -1, hi = 4096;  // largest accepted length of 'a's, by bisection
        while (hi - lo > 1) {
            int mid = (lo + hi) / 2;
            char *s = malloc((size_t)mid + 1);
            memset(s, 'a', (size_t)mid), s[mid] = '\0';
            cJSON *t = cJSON_Duplicate(item, true);
            cJSON_ReplaceItemInObjectCaseSensitive(t, f->string, cJSON_CreateString(s));
            cJSON *doc = list ? cJSON_CreateArray() : t;
            if (list) cJSON_AddItemToArray(doc, t);
            char *txt = cJSON_PrintUnformatted(doc);
            if (accepted(name, txt)) lo = mid;
            else hi = mid;
            cJSON_free(txt), free(s), cJSON_Delete(doc);
        }
        if (lo >= 3) {
            char *s = malloc((size_t)lo + 1);
            memset(s, 'a', (size_t)lo), s[lo] = '\0';
            cJSON_SetValuestring(f, s);
            free(s);
        }
    }
    cJSON *doc = item;
    if (list) {
        doc = cJSON_CreateArray();
        for (int n = 1; n <= 64; n++) {  // grow until the schema refuses another item
            cJSON *t = cJSON_Duplicate(item, true);
            cJSON *g;
            cJSON_ArrayForEach(g, t)
            {
                size_t k = cJSON_IsString(g) ? strlen(g->valuestring) : 0;
                if (k >= 3) snprintf(g->valuestring + k - 3, 4, "%03u", (unsigned)n % 1000u);
            }
            cJSON_AddItemToArray(doc, t);
            char *txt = cJSON_PrintUnformatted(doc);
            bool ok = accepted(name, txt);
            cJSON_free(txt);
            if (!ok) {
                cJSON_DeleteItemFromArray(doc, cJSON_GetArraySize(doc) - 1);
                break;
            }
        }
        cJSON_Delete(item);
    }
    char *txt = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    return txt;
}

/** mqttPublish whose normalised text is exactly `target` bytes, or NULL. */
static char *publish_of_len(size_t target)
{
    static char buf[CFG_SMALL_MAX * 2];
    // Escaped characters (6 output bytes each) fill the identifier (64) and then the delimiter (16)
    // of every topic; the length of the last topic (1 byte each) fills the gaps between steps.
    for (int esc = 0; esc <= 80; esc++) {
        for (int last = 0; last <= 128; last++) {
            size_t o = (size_t)snprintf(buf, sizeof buf, "[");
            for (int i = 0; i < 16; i++) {
                int tl = i == 15 ? last : 128;
                o += (size_t)snprintf(buf + o, sizeof buf - o, "%s{\"topic\":\"", i ? "," : "");
                for (int k = 0; k < tl; k++) buf[o++] = 't';
                o += (size_t)snprintf(buf + o, sizeof buf - o, "\",\"identifier\":\"");
                for (int k = 0; k < 64; k++) o += (size_t)snprintf(buf + o, sizeof buf - o, k < esc ? "\\u0001" : "i");
                o += (size_t)snprintf(buf + o, sizeof buf - o, "\",\"delimiter\":\"");
                for (int k = 0; k < 16; k++) o += (size_t)snprintf(buf + o, sizeof buf - o, 64 + k < esc ? "\\u0001" : "d");
                o += (size_t)snprintf(buf + o, sizeof buf - o, "\"}");
            }
            snprintf(buf + o, sizeof buf - o, "]");
            size_t n = reference_len("mqttPublish", buf);
            if (n == target) return buf;
            if (n > target) break;
        }
    }
    return NULL;
}

static void test_exact_limit(void)
{
    for (size_t want = CFG_SMALL_MAX - 1; want <= CFG_SMALL_MAX + 1; want++) {
        char *in = publish_of_len(want);
        CHECK(in != NULL, "could not build a %zu-byte mqttPublish", want);
        if (!in) continue;
        cfg_result_t r = run("mqttPublish", in, NULL);
        if (want <= CFG_SMALL_MAX) {
            CHECK(r == CFG_OK && strlen(s_out) == want, "%zu bytes must be accepted (%d %s)", want, r, s_err);
        } else {
            CHECK(r == CFG_TOO_LARGE && strcmp(s_err, "These settings are too large.") == 0, "%zu bytes must be 413 (%d %s)", want, r,
                  s_err);
        }
    }
}

static void test_largest_each(void)
{
    const char *names[16];
    size_t n = cfg_names(&ENV, names, 16);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(names[i], "edgeDevices") == 0) continue;
        char *doc = largest_doc(names[i]);
        size_t ref = reference_len(names[i], doc);
        cfg_result_t r = run(names[i], doc, NULL);
        CHECK(ref > 0, "%s: the probed document is not accepted by the reference", names[i]);
        CHECK(r == (ref <= CFG_SMALL_MAX ? CFG_OK : CFG_TOO_LARGE), "%s: largest document gives %d (%s), reference %zu bytes", names[i], r,
              s_err, ref);
        printf("  %-13s largest: %5zu bytes in, %5zu normalised, result %d\n", names[i], strlen(doc), ref, r);
        cJSON_free(doc);
    }
}

static void test_round_trip_and_errors(void)
{
    static char again[CFG_SMALL_BUF];
    const char *names[16];
    size_t n = cfg_names(&ENV, names, 16);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(names[i], "edgeDevices") == 0) continue;
        char *doc = largest_doc(names[i]);
        if (run(names[i], doc, NULL) == CFG_OK) {
            strcpy(again, s_out);
            cfg_result_t r = run(names[i], again, NULL);
            CHECK(r == CFG_OK && strcmp(again, s_out) == 0, "%s: POST -> GET -> POST changes the text", names[i]);
        }
        cJSON_free(doc);
        cJSON *d = cfg_default(&ENV, names[i]);
        char *t = cJSON_PrintUnformatted(d);
        cfg_result_t r = run(names[i], t, NULL);
        CHECK(r == CFG_OK && strcmp(s_out, t) == 0, "%s defaults do not round-trip (%d %s)", names[i], r, s_err);
        cJSON_free(t);
        cJSON_Delete(d);
    }
    // `current` (a full stored document, as the store passes it) supplies the keys the request leaves out.
    cJSON *cur = cfg_default(&ENV, "port1");
    cJSON_ReplaceItemInObjectCaseSensitive(cur, "baudRate", cJSON_CreateNumber(9600));
    cfg_result_t r = run("port1", "{\"dataBits\":7}", cur);
    CHECK(r == CFG_OK && strstr(s_out, "\"baudRate\":9600") && strstr(s_out, "\"dataBits\":7"), "current values kept (%d %s)", r, s_err);
    cJSON_Delete(cur);
    r = run("port1", "{\"baudRate\":300}", NULL);
    CHECK(r == CFG_INVALID && strcmp(s_err, "baudRate must be 600 to 230400.") == 0, "schema error: %d %s", r, s_err);
    r = run("port1", "{\"baudRate\":", NULL);
    CHECK(r == CFG_INVALID && strcmp(s_err, "The request body is not valid JSON.") == 0, "bad JSON: %d %s", r, s_err);
    r = run("system", "{\"hostname\":\"\\uD800\"}", NULL);
    CHECK(r == CFG_INVALID && strcmp(s_err, "The request body is not valid JSON.") == 0, "lone surrogate: %d %s", r, s_err);
}

int main(void)
{
    meter_install(60 * 1024);
    test_exact_limit();
    test_largest_each();
    test_round_trip_and_errors();
    printf("  worst live-block peak: %zu bytes, within the 61 440-byte limit\n", s_worst);
    printf(failures ? "%d FAILED\n" : "text_small: all tests passed\n", failures);
    return failures != 0;
}
