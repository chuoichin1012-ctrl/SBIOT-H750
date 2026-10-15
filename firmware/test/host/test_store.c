// Host tests for components/store on FlashDB over simulated NOR (spec §9.1, §9.4, §12.1 "Store
// Edge"): make -C stm32/firmware/test/host
// A reboot is store_deinit + store_init on the same flash; a power cut is a longjmp out of an erase
// or program, then that reboot. Every case checks that the store's lock is balanced afterwards.
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flashdb.h"
#include "nor_sim.h"
#include "store.h"
#include "store_port.h"

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

static const cfg_env_t ENV = {.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};

// ---- the port ------------------------------------------------------------------------------

static int s_depth;  // lock depth: 0 between API calls
static int64_t s_wall;
static uint32_t s_uptime;
static bool s_quiet;

void store_port_lock(void) { s_depth++; }
void store_port_unlock(void) { s_depth--; }
int64_t store_port_wall_ms(void) { return s_wall; }
uint32_t store_port_uptime_ms(void) { return s_uptime; }
void store_port_log(const char *fmt, ...)
{
    if (s_quiet)
        return;
    va_list ap;
    va_start(ap, fmt);
    printf("  store: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

#define BALANCED(what) CHECK(s_depth == 0, "%s: lock depth %d", what, s_depth)

// ---- the flash -----------------------------------------------------------------------------

static sim_world_t s_w;
static nor_sim_t s_spi;
static jmp_buf s_jb;
static long s_erases;
static int (*s_sim_erase)(nor_t *, uint32_t);

static int (*s_sim_program)(nor_t *, uint32_t, const void *, uint32_t);
static long s_trace_from = -1;  // STORE_TRACE=<op>: print the operations from there on

static int counting_erase(nor_t *n, uint32_t off)
{
    s_erases++;
    if (s_trace_from >= 0 && s_w.ops >= s_trace_from)
        printf("    op %ld: erase 0x%05x\n", s_w.ops, (unsigned)off);
    return s_sim_erase(n, off);
}

static int tracing_program(nor_t *n, uint32_t off, const void *buf, uint32_t len)
{
    if (s_trace_from >= 0 && s_w.ops >= s_trace_from) {
        const uint8_t *b = buf;
        printf("    op %ld: program 0x%05x len %u: %02x %02x %02x %02x\n", s_w.ops, (unsigned)off, (unsigned)len, b[0],
               len > 1 ? b[1] : 0, len > 2 ? b[2] : 0, len > 3 ? b[3] : 0);
    }
    return s_sim_program(n, off, buf, len);
}

static bool s_have_flash;

static void flash_new(uint32_t seed)
{
    store_deinit();
    if (s_have_flash)
        nor_sim_free(&s_spi);
    s_have_flash = true;
    sim_world_init(&s_w, seed);
    s_w.cut_jmp = &s_jb;
    nor_sim_init(&s_spi, &s_w, 2, 0x400000);
    s_sim_erase = s_spi.nor.erase_sector;
    s_spi.nor.erase_sector = counting_erase;
    s_sim_program = s_spi.nor.program;
    s_spi.nor.program = tracing_program;
}

static void reboot(void)
{
    store_deinit();
    s_depth = 0;
    CHECK(store_init(&ENV, &s_spi.nor) == STORE_OK, "store_init");
    BALANCED("store_init");
}

// ---- helpers -------------------------------------------------------------------------------

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

/** What store_edge_save must store for `json`: cfg_edge_text's own output. */
static char *edge_oracle(const char *json)
{
    sbuf_t out = {0};
    char err[200];
    cfg_result_t r = cfg_edge_text(&ENV, json, strlen(json), true, CFG_EDGE_MAX, sink, &out, err, sizeof err);
    CHECK(r == CFG_OK, "oracle rejects the model: %s", err);
    return out.p;
}

/** A valid model of `devs` Modbus RTU devices with `per` points each; `tag` makes the names differ. */
static char *edge_model(int devs, int per, int tag)
{
    size_t cap = 200000, o = 0;
    char *b = malloc(cap);
    o += (size_t)snprintf(b + o, cap - o, "[");
    for (int d = 0, k = 0; d < devs; d++) {
        o += (size_t)snprintf(b + o, cap - o,
                              "%s{\"name\":\"dev%d_%d\",\"source\":\"uart\",\"uart\":1,\"protocol\":\"modbus\",\"addr\":\"%d\",\"nodes\":[",
                              d ? "," : "", tag, d, d + 1);
        for (int i = 0; i < per; i++, k++)
            o += (size_t)snprintf(b + o, cap - o, "%s{\"name\":\"p%d_%d\",\"fc\":3,\"reg\":%d,\"dtype\":\"uint16\"}", i ? "," : "",
                                  tag, k, i);
        o += (size_t)snprintf(b + o, cap - o, "]}");
    }
    snprintf(b + o, cap - o, "]");
    return b;
}

static char *edge_now(void)
{
    char *b = malloc(CFG_EDGE_MAX + 2);
    size_t n = store_edge_text(b, CFG_EDGE_MAX + 2);
    BALANCED("store_edge_text");
    if (!n) {
        free(b);
        return NULL;
    }
    return b;
}

/** Keys in the edge database with the given prefix (the store is unmounted meanwhile). */
static int edge_keys(const char *prefix)
{
    store_deinit();
    struct fdb_kvdb db = {0};
    uint32_t sec = 16 * 1024;
    fdb_kvdb_control(&db, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec);
    int n = -1;
    if (fdb_kvdb_init(&db, "edge", "edge", NULL, NULL) == FDB_NO_ERR) {
        struct fdb_kv_iterator it;
        fdb_kv_iterator_init(&db, &it);
        n = 0;
        while (fdb_kv_iterate(&db, &it))
            n += strncmp(it.curr_kv.name, prefix, strlen(prefix)) == 0;
        fdb_kvdb_deinit(&db);
    }
    reboot();
    return n;
}

/** Writes a raw value under `key` in database `part`, behind the store's back. */
static void raw_put(const char *part, const char *key, const char *value)
{
    store_deinit();
    struct fdb_kvdb db = {0};
    uint32_t sec = 16 * 1024;
    fdb_kvdb_control(&db, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec);
    CHECK(fdb_kvdb_init(&db, part, part, NULL, NULL) == FDB_NO_ERR, "raw open %s", part);
    struct fdb_blob blob;
    CHECK(fdb_kv_set_blob(&db, key, fdb_blob_make(&blob, value, strlen(value))) == FDB_NO_ERR, "raw put %s", key);
    fdb_kvdb_deinit(&db);
    reboot();
}

static char *default_text(const char *name)
{
    cJSON *d = cfg_default(&ENV, name);
    char *s = cJSON_PrintUnformatted(d);
    cJSON_Delete(d);
    return s;
}

// ---- small configs --------------------------------------------------------------------------

static void test_first_boot(void)
{
    flash_new(1);
    reboot();
    const char *names[32];
    size_t n = cfg_names(&ENV, names, 32);
    for (size_t i = 0; i < n; i++) {
        if (!strcmp(names[i], "edgeDevices"))
            continue;
        char *got = store_get_json(names[i]), *want = default_text(names[i]);
        CHECK(got && !strcmp(got, want), "%s: the default on a blank flash", names[i]);
        free(got);
        free(want);
    }
    BALANCED("store_get_json");
    CHECK(store_get_json("edgeDevices") == NULL, "edgeDevices is never cached");
    CHECK(store_get_json("nonsense") == NULL && !store_known("nonsense"), "unknown name");
    CHECK(store_boot_count() == 1, "boot 1 (%u)", (unsigned)store_boot_count());
    reboot();
    CHECK(store_boot_count() == 2, "boot 2 after a reboot");
}

static void test_set_get(void)
{
    flash_new(2);
    reboot();
    const char *in = "{\"hostname\":\"plant-7\"}";
    char want[CFG_SMALL_BUF], err[200], err2[200];
    cJSON *cur = store_get("system");
    CHECK(cfg_validate_text(&ENV, "system", in, strlen(in), cur, want, sizeof want, err, sizeof err) == CFG_OK, "oracle");
    cJSON_Delete(cur);
    const char *names[32];
    size_t nn = cfg_names(&ENV, names, 32);
    char *before[32] = {0};
    for (size_t i = 0; i < nn; i++)
        before[i] = store_get_json(names[i]);
    CHECK(store_set_text("system", in, strlen(in), err, sizeof err) == STORE_OK, "set system: %s", err);
    BALANCED("store_set_text");
    for (size_t i = 0; i < nn; i++) {  // the text cache moved the others to make room: each intact
        char *now = store_get_json(names[i]);
        if (strcmp(names[i], "system"))
            CHECK((!now && !before[i]) || (now && before[i] && !strcmp(now, before[i])), "%s unchanged by the system save", names[i]);
        free(now);
        free(before[i]);
    }
    char *got = store_get_json("system");
    CHECK(got && !strcmp(got, want), "system reads back as cfg_validate_text made it");
    free(got);
    reboot();
    got = store_get_json("system");
    CHECK(got && !strcmp(got, want), "and after a reboot");
    free(got);

    const char *bad = "{\"hostname\":5}";
    cur = store_get("system");
    cfg_validate_text(&ENV, "system", bad, strlen(bad), cur, want, sizeof want, err2, sizeof err2);
    cJSON_Delete(cur);
    CHECK(store_set_text("system", bad, strlen(bad), err, sizeof err) == STORE_EINVALID && !strcmp(err, err2),
          "invalid: 400 with cfg_validate_text's message (%s)", err);
    CHECK(store_set_text("system", "{", 1, err, sizeof err) == STORE_EINVALID, "not JSON: 400");
    got = store_get_json("system");
    CHECK(got && strstr(got, "plant-7"), "the stored copy is unchanged");
    free(got);
    CHECK(store_set_text("nonsense", "{}", 2, err, sizeof err) == STORE_ENOTFOUND, "unknown name: 404");
    CHECK(store_set_text("edgeDevices", "[]", 2, err, sizeof err) == STORE_ENOTFOUND, "edgeDevices has its own entry point");
    BALANCED("rejected sets");

    cJSON *tree = store_get("system");
    CHECK(tree && cJSON_IsString(cJSON_GetObjectItem(tree, "hostname")), "store_get parses the cached text");
    cJSON_Delete(tree);
}

static void test_revalidate_at_boot(void)
{
    flash_new(3);
    reboot();
    raw_put("cfg", "system", "{\"hostname\":\"old-fw\"}");  // written by an older firmware: keys missing
    char *got = store_get_json("system");
    CHECK(got && strstr(got, "old-fw") && strstr(got, "\"username\""), "missing keys filled at boot (%s)", got);
    free(got);
    raw_put("cfg", "network", "not json at all");
    char *want = default_text("network");
    got = store_get_json("network");
    CHECK(got && !strcmp(got, want), "an unusable stored copy falls back to the default");
    free(got);
    free(want);
}

static int s_heard;
static char s_last[32];

static void listener(const char *name, void *ctx)
{
    s_heard++;
    snprintf(s_last, sizeof s_last, "%s", name);
    CHECK(ctx == &s_heard, "listener context");
}

static void test_listeners_and_state(void)
{
    flash_new(4);
    reboot();
    store_subscribe(listener, &s_heard);
    char err[200];
    s_heard = 0;
    store_set_text("system", "{\"hostname\":\"x\"}", 16, err, sizeof err);
    CHECK(s_heard == 1 && !strcmp(s_last, "system"), "set notifies once (%d %s)", s_heard, s_last);
    store_set_text("system", "{\"hostname\":5}", 14, err, sizeof err);
    CHECK(s_heard == 1, "a rejected set does not notify");

    uint8_t do_state[2] = {1, 0}, back[2] = {9, 9};
    CHECK(store_state_set("do", do_state, 2) == STORE_OK, "state set");
    CHECK(store_state_get("do", back, 2) == 2 && back[0] == 1 && back[1] == 0, "state get");
    CHECK(store_state_get("absent", back, 2) == 0, "absent state");
    reboot();
    CHECK(store_state_get("do", back, 2) == 2 && back[0] == 1, "state kept across a reboot");
    BALANCED("state");
}

static void test_certs_and_factory_reset(void)
{
    flash_new(5);
    reboot();
    store_subscribe(listener, &s_heard);
    s_heard = 0;
    const char *pem = "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n";
    CHECK(store_cert_set("mqtt", "ca", pem, strlen(pem)) == STORE_OK && s_heard == 1 && !strcmp(s_last, "mqtt"),
          "cert set notifies its scope");
    char *got = store_cert_get("mqtt", "ca");
    CHECK(got && !strcmp(got, pem), "cert reads back NUL-terminated");
    free(got);
    CHECK(store_cert_get("mqtt", "key") == NULL, "absent cert");
    CHECK(store_cert_set("ftp", "ca", pem, 5) == STORE_EINVALID && store_cert_set("mqtt", "pin", pem, 5) == STORE_EINVALID,
          "unknown scope or kind");
    char *big = calloc(1, STORE_CERT_MAX + 1);
    memset(big, 'A', STORE_CERT_MAX + 1);
    CHECK(store_cert_set("mqtt", "cert", big, STORE_CERT_MAX + 1) == STORE_EINVALID, "over 12 KB");
    CHECK(store_cert_set("mqtt", "cert", big, STORE_CERT_MAX) == STORE_OK, "exactly 12 KB");
    free(big);

    char err[200];
    store_set_text("system", "{\"hostname\":\"keep-me-not\"}", 26, err, sizeof err);
    char *model = edge_model(2, 3, 1);
    CHECK(store_edge_save(model, strlen(model), err, sizeof err) == STORE_OK, "edge saved: %s", err);
    free(model);
    store_log_append("a line to keep", 14);
    CHECK(store_factory_reset() == STORE_OK, "factory reset");
    BALANCED("factory reset");
    char *want = default_text("system");
    got = store_get_json("system");
    CHECK(got && !strcmp(got, want), "configs back to defaults");
    free(got);
    free(want);
    CHECK(store_cert_get("mqtt", "ca") == NULL, "certificates gone");
    char *e = edge_now(), *edef = default_text("edgeDevices");
    CHECK(e && !strcmp(e, edef), "edgeDevices back to the default");
    free(e);
    free(edef);
    reboot();
    CHECK(edge_keys("ed.") == 0, "no edge records left");  // the log is checked in test_log
}

#if STORE_CACHE_SIZE < 5000
// Built with a 3800-byte cache (test_store_small): the defaults take 3707, so 93 bytes are free.
static void test_cache_full(void)
{
    flash_new(6);
    reboot();
    char in[300], err[200], a[200];
    memset(a, 'a', sizeof a);
    snprintf(in, sizeof in, "{\"host\":\"%.*s\"}", 120, a);
    char *was = store_get_json("mqtt");
    CHECK(store_set_text("mqtt", in, strlen(in), err, sizeof err) == STORE_ETOOLARGE &&
              !strcmp(err, "The settings do not fit in the gateway's memory."),
          "120 more bytes than fit: 413 (%s)", err);
    char *now = store_get_json("mqtt");
    CHECK(now && !strcmp(now, was), "the cached copy unchanged");
    free(now);
    reboot();
    now = store_get_json("mqtt");
    CHECK(now && !strcmp(now, was), "and nothing was written to flash");
    free(now);
    free(was);
    snprintf(in, sizeof in, "{\"host\":\"%.*s\"}", 60, a);
    CHECK(store_set_text("mqtt", in, strlen(in), err, sizeof err) == STORE_OK, "60 bytes more: fits (%s)", err);
    snprintf(in, sizeof in, "{\"host\":\"%.*s\"}", 90, a);
    CHECK(store_set_text("mqtt", in, strlen(in), err, sizeof err) == STORE_OK, "30 more over its own old text: fits (%s)", err);
    BALANCED("cache full");
}
#endif

// ---- edgeDevices -----------------------------------------------------------------------------

static void test_edge_save_and_read(void)
{
    flash_new(10);
    reboot();
    char *def = default_text("edgeDevices"), *e = edge_now();
    CHECK(e && !strcmp(e, def), "no model yet: the default text");
    free(e);
    store_edge_pin_t p;
    store_edge_pin(&p);
    char *rec = malloc(STORE_EDGE_RECORD);
    CHECK(p.gen == 0 && p.count == 1 && store_edge_record(&p, 0, rec, STORE_EDGE_RECORD) == strlen(def) &&
              !memcmp(rec, def, strlen(def)),
          "no model yet: GET streams the default as one record");
    store_edge_unpin(&p);
    BALANCED("default pin");
    free(def);

    char *model = edge_model(20, 5, 1), *want = edge_oracle(model), err[200];
    CHECK(store_edge_save(model, strlen(model), err, sizeof err) == STORE_OK, "save: %s", err);
    BALANCED("store_edge_save");
    e = edge_now();
    CHECK(e && !strcmp(e, want), "the stored text is cfg_edge_text's output (%zu bytes)", strlen(want));
    free(e);
    store_edge_pin(&p);
    size_t total = 0;
    for (uint32_t i = 0; i < p.count; i++) {
        size_t n = store_edge_record(&p, i, rec, STORE_EDGE_RECORD);
        CHECK(n > 0 && !memcmp(rec, want + total, n), "record %u", (unsigned)i);
        total += n;
    }
    CHECK(p.gen == 1 && total == strlen(want) && p.count == (strlen(want) + STORE_EDGE_RECORD - 1) / STORE_EDGE_RECORD,
          "generation 1, %u records of 8 KB", (unsigned)p.count);
    store_edge_unpin(&p);
    reboot();
    e = edge_now();
    CHECK(e && !strcmp(e, want), "and after a reboot");
    free(e);
    free(model);
    free(want);

    model = edge_model(10, 4, 2);
    want = edge_oracle(model);
    CHECK(store_edge_save(model, strlen(model), err, sizeof err) == STORE_OK, "second save");
    store_edge_pin(&p);
    CHECK(p.gen == 2, "generation 2 (%u)", (unsigned)p.gen);
    store_edge_unpin(&p);
    CHECK(edge_keys("ed.1.") == 0 && edge_keys("ed.2.") == (int)p.count, "generation 1's records deleted");

    // rejected models leave the committed one alone
    const char *bad = "[{\"name\":\"x\",\"source\":\"uart\",\"uart\":1,\"protocol\":\"modbus\",\"addr\":\"0\"}]";
    CHECK(store_edge_save(bad, strlen(bad), err, sizeof err) == STORE_EINVALID, "invalid model: 400 (%s)", err);
    CHECK(store_edge_save("[", 1, err, sizeof err) == STORE_EINVALID, "not JSON: 400");
    char *huge = edge_model(64, 2, 3);  // 128 points: over the limit
    store_edge_save(huge, strlen(huge), err, sizeof err);
    free(huge);
    store_edge_pin(&p);
    e = edge_now();
    CHECK(p.gen == 2 && e && !strcmp(e, want), "still generation 2, same text");
    free(e);
    store_edge_unpin(&p);
    CHECK(edge_keys("ed.") == (int)p.count, "and no records of the failed saves left");
    BALANCED("rejected edge saves");
    free(model);
    free(want);
    free(rec);
}

static void test_edge_too_large(void)
{
    flash_new(11);
    reboot();
    char *huge = edge_model(64, 2, 3), err[200];
    sbuf_t out = {0};
    cfg_result_t r = cfg_edge_text(&ENV, huge, strlen(huge), true, CFG_EDGE_MAX, sink, &out, err, sizeof err);
    store_err_t s = store_edge_save(huge, strlen(huge), err, sizeof err);
    // 128 points: over the 100-point limit, so cfg_edge_text rejects it; the store answers the same way.
    store_err_t want = r == CFG_OK ? STORE_OK : r == CFG_INVALID ? STORE_EINVALID : r == CFG_TOO_LARGE ? STORE_ETOOLARGE : STORE_ENOMEM;
    CHECK(s == want, "the store maps cfg_edge_text's result (%d -> %d, want %d)", r, s, want);
    free(out.p);
    free(huge);
}

static void test_pinning(void)
{
    flash_new(12);
    reboot();
    char err[200];
    char *m1 = edge_model(12, 4, 1), *w1 = edge_oracle(m1);
    store_edge_save(m1, strlen(m1), err, sizeof err);
    store_edge_pin_t a, b;
    store_edge_pin(&a);
    store_edge_pin(&b);  // two GETs of generation 1
    char *m2 = edge_model(12, 4, 2);
    CHECK(store_edge_save(m2, strlen(m2), err, sizeof err) == STORE_OK, "POST while two GETs read: saved");
    char *rec = malloc(STORE_EDGE_RECORD);
    size_t total = 0;
    for (uint32_t i = 0; i < a.count; i++)
        total += store_edge_record(&a, i, rec, STORE_EDGE_RECORD);
    CHECK(total == strlen(w1), "the pinned GET still reads all of generation 1");
    store_edge_unpin(&a);
    CHECK(store_edge_record(&b, 0, rec, STORE_EDGE_RECORD) > 0, "one GET left: generation 1 still there");
    char *m3 = edge_model(12, 4, 3);
    store_edge_save(m3, strlen(m3), err, sizeof err);  // generation 3: 2 is deleted at once, 1 still pinned
    CHECK(store_edge_record(&b, 0, rec, STORE_EDGE_RECORD) > 0, "a third save: generation 1 still pinned");
    store_edge_unpin(&b);
    CHECK(store_edge_record(&b, 0, rec, STORE_EDGE_RECORD) == 0, "the last GET gone: generation 1 deleted");
    store_edge_pin_t c;
    store_edge_pin(&c);
    CHECK(c.gen == 3 && edge_keys("ed.") == (int)c.count, "only generation 3's records left");
    store_edge_unpin(&c);
    BALANCED("pinning");
    // a GET pinned across a reboot: pins are not persistent, the boot cleanup keeps generation 3 only
    store_edge_pin(&c);
    reboot();
    CHECK(edge_keys("ed.") == (int)c.count, "after a reboot: generation 3 only");
    free(m1), free(w1), free(m2), free(m3), free(rec);
}

// A GET pinned while thirty saves fill the edge database again and again: FlashDB's garbage
// collection moves the pinned records, which must still read back whole (spec §12.1, Store Edge).
static void test_pin_through_gc(void)
{
    flash_new(14);
    reboot();
    char err[200];
    char *m1 = edge_model(14, 4, 1), *w1 = edge_oracle(m1);
    store_edge_save(m1, strlen(m1), err, sizeof err);
    store_edge_pin_t p;
    store_edge_pin(&p);
    long erases = 0;
    for (int i = 0; i < 30; i++) {
        char *m = edge_model(16, 4, 10 + i);
        long ops = s_w.ops;
        CHECK(store_edge_save(m, strlen(m), err, sizeof err) == STORE_OK, "save %d while pinned: %s", i, err);
        erases += s_w.ops - ops;
        free(m);
    }
    char *rec = malloc(STORE_EDGE_RECORD), *all = calloc(1, CFG_EDGE_MAX + 1);
    size_t total = 0;
    for (uint32_t i = 0; i < p.count; i++)
        total += store_edge_record(&p, i, all + total, STORE_EDGE_RECORD);
    CHECK(total == strlen(w1) && !memcmp(all, w1, total), "the pinned generation reads back whole after 30 saves");
    store_edge_unpin(&p);
    CHECK(store_edge_record(&p, 0, rec, STORE_EDGE_RECORD) == 0, "and is deleted when unpinned");
    store_edge_pin_t c;
    store_edge_pin(&c);
    store_edge_unpin(&c);
    CHECK(edge_keys("ed.") == (int)c.count, "only the current generation left");
    (void)erases;
    free(m1), free(w1), free(rec), free(all);
}

// No manifest but records (the manifest itself lost): the boot keeps them, since nothing proves
// they are orphans; the next successful save removes whatever it does not keep.
static void test_no_manifest_keeps_records(void)
{
    flash_new(15);
    reboot();
    raw_put("edge", "ed.3.0", "a record without a manifest");
    CHECK(edge_keys("ed.3.") == 1, "no manifest: the boot does not delete records");
    char err[200], *m = edge_model(4, 2, 1);
    CHECK(store_edge_save(m, strlen(m), err, sizeof err) == STORE_OK, "a save");
    CHECK(edge_keys("ed.3.") == 0 && edge_keys("ed.1.") == 1, "the save removes the stray record and keeps its own");
    free(m);
}

static void test_orphans(void)
{
    flash_new(13);
    reboot();
    char err[200];
    char *m = edge_model(8, 4, 1), *w = edge_oracle(m);
    store_edge_save(m, strlen(m), err, sizeof err);
    raw_put("edge", "ed.7.0", "left by a power cut before the manifest");
    raw_put("edge", "ed.0.3", "left by a power cut after the manifest");
    CHECK(edge_keys("ed.7.") == 0 && edge_keys("ed.0.") == 0, "orphans deleted at boot");
    char *e = edge_now();
    CHECK(e && !strcmp(e, w), "the committed model untouched");
    free(e), free(m), free(w);
}

// ---- power cuts ------------------------------------------------------------------------------

static uint8_t *s_snap;

static void snapshot(void) { memcpy(s_snap, s_spi.mem, 0x100000); }
static void restore(void) { memcpy(s_spi.mem, s_snap, 0x100000); }

// Saves `model` with a power cut at operation `cut` (and `cuts` - 1 more, `period` apart); returns
// false when it completed without one. Then reboots.
static bool save_cut(const char *model, long cut, int cuts, long period)
{
    s_w.ops = 0;
    s_w.cut_at = cut;
    s_w.cuts_left = cuts - 1;
    s_w.cut_period = period;
    char err[200];
    volatile bool was_cut = false;
    if (setjmp(s_jb))
        was_cut = true;
    else
        store_edge_save(model, strlen(model), err, sizeof err);
    // the reboot itself may take the remaining cuts (FlashDB recovers on mount)
    while (1) {
        if (setjmp(s_jb)) {
            was_cut = true;
            continue;
        }
        store_deinit();
        s_depth = 0;
        store_init(&ENV, &s_spi.nor);
        break;
    }
    s_w.cut_at = -1;
    return was_cut;
}

// Erases in a save of mb from the snapshot: >0 means it ran FlashDB's garbage collection.
static long erases_in_save(const char *mb)
{
    store_deinit();
    restore();
    reboot();
    long e0 = s_erases;
    char err[200];
    store_edge_save(mb, strlen(mb), err, sizeof err);
    return s_erases - e0;
}

static void test_power_cuts(void)
{
    s_snap = malloc(0x100000);
    flash_new(20);
    reboot();
    char err[200];
    char *ma = edge_model(14, 4, 1), *wa = edge_oracle(ma), *mb = edge_model(16, 4, 2), *wb = edge_oracle(mb);
    // Fill the edge database until the next save of mb has to collect garbage, then save ma.
    for (int i = 0; i < 40; i++) {
        char *m = edge_model(16, 4, 50 + i);
        store_edge_save(m, strlen(m), err, sizeof err);
        free(m);
        store_edge_save(ma, strlen(ma), err, sizeof err);
        store_deinit();
        snapshot();
        reboot();
        if (erases_in_save(mb) > 0)
            break;
        store_deinit();
        restore();
        reboot();
    }
    long gc = erases_in_save(mb);
    CHECK(gc > 0, "the measured save collects garbage (%ld sector erases)", gc);
    store_deinit();
    s_quiet = true;
    long total;
    {
        restore();
        reboot();
        s_w.ops = 0;
        store_edge_save(mb, strlen(mb), err, sizeof err);
        total = s_w.ops;
    }
    int bad = 0, newer = 0;
    if (getenv("STORE_TRACE")) {  // the uncut save, traced from the given operation
        store_deinit();
        restore();
        reboot();
        s_w.ops = 0;
        s_trace_from = atol(getenv("STORE_TRACE"));
        store_edge_save(mb, strlen(mb), err, sizeof err);
        s_trace_from = -1;
    }
    if (getenv("STORE_ONE_CUT")) {  // one cut, with FlashDB's messages on the reboot
        store_deinit();
        restore();
        reboot();
        s_quiet = false;
        printf("--- cut at %s, reboot:\n", getenv("STORE_ONE_CUT"));
        save_cut(mb, atol(getenv("STORE_ONE_CUT")), 1, 0);
        char *e = edge_now();
        printf("--- after: %s\n", e ? (!strcmp(e, wa) ? "old" : !strcmp(e, wb) ? "new" : e) : "(none)");
        free(e);
        s_quiet = true;
    }
    for (long cut = 0; cut < total; cut++) {
        store_deinit();
        restore();
        reboot();
        save_cut(mb, cut, 1, 0);
        char *e = edge_now();
        bool ok = e && (!strcmp(e, wa) || !strcmp(e, wb));
        newer += e && !strcmp(e, wb);
        store_edge_pin_t p;
        store_edge_pin(&p);
        store_edge_unpin(&p);
        int keys = edge_keys("ed.");
        ok = ok && keys == (int)p.count;
        if (!ok && bad++ < 5)
            printf("  cut at op %ld of %ld: text %s, %d records for %u\n", cut, total, e ? (!strcmp(e, wa) ? "old" : !strcmp(e, wb) ? "new" : "OTHER") : "none",
                   keys, (unsigned)p.count);
        free(e);
    }
    CHECK(bad == 0, "edge save: %d of %ld cut points end wrong", bad, total);
    printf("store: edge save of %zu bytes with garbage collection, %ld operations, cut at each: %d ended on the new model\n",
           strlen(wb), total, newer);

    uint32_t x = 777;
    bad = 0;
    for (int i = 0; i < 150; i++) {
        x = x * 1103515245u + 12345u;
        long cut = (long)((x >> 8) % (uint32_t)total), period = 1 + (long)((x >> 4) % 100);
        store_deinit();
        restore();
        reboot();
        save_cut(mb, cut, 3, period);
        char *e = edge_now();
        store_edge_pin_t p;
        store_edge_pin(&p);
        store_edge_unpin(&p);
        bool ok = e && (!strcmp(e, wa) || !strcmp(e, wb)) && edge_keys("ed.") == (int)p.count;
        // and the store still works: one more save completes
        char *mc = edge_model(6, 3, 9), *wc = edge_oracle(mc);
        ok = ok && store_edge_save(mc, strlen(mc), err, sizeof err) == STORE_OK;
        char *e2 = edge_now();
        ok = ok && e2 && !strcmp(e2, wc);
        if (!ok && bad++ < 5)
            printf("  3 cuts from op %ld every %ld: wrong (%s)\n", cut, period, err);
        free(e), free(e2), free(mc), free(wc);
    }
    CHECK(bad == 0, "edge save with three cuts: %d of 150 runs end wrong", bad);
    s_quiet = false;
    free(ma), free(wa), free(mb), free(wb), free(s_snap);
}

static void test_small_power_cuts(void)
{
    s_snap = malloc(0x100000);
    flash_new(21);
    reboot();
    char err[200];
    store_set_text("system", "{\"hostname\":\"before\"}", 21, err, sizeof err);
    store_deinit();
    snapshot();
    restore();
    reboot();
    s_w.ops = 0;
    store_set_text("system", "{\"hostname\":\"after\"}", 20, err, sizeof err);
    long total = s_w.ops;
    s_quiet = true;
    int bad = 0;
    for (long cut = 0; cut < total; cut++) {
        store_deinit();
        restore();
        reboot();
        s_w.ops = 0;
        s_w.cut_at = cut;
        if (!setjmp(s_jb))
            store_set_text("system", "{\"hostname\":\"after\"}", 20, err, sizeof err);
        s_w.cut_at = -1;
        reboot();
        char *g = store_get_json("system");
        bool ok = g && (strstr(g, "\"before\"") || strstr(g, "\"after\""));
        if (!ok && bad++ < 3)
            printf("  small save cut at %ld: %s\n", cut, g ? g : "(none)");
        free(g);
    }
    CHECK(bad == 0, "small config save: %d of %ld cut points end wrong", bad, total);
    s_quiet = false;
    free(s_snap);
}

// ---- log -------------------------------------------------------------------------------------

typedef struct {
    int n;
    char first[64], last[64];
    uint32_t last_boot;
    int64_t last_wall;
    int stop_at;
} logs_t;

static bool collect(const store_log_meta_t *m, const char *line, size_t len, void *ctx)
{
    logs_t *l = ctx;
    CHECK(s_depth == 0, "the lock is not held during the log callback");
    if (l->n == 0)
        snprintf(l->first, sizeof l->first, "%.*s", (int)len, line);
    snprintf(l->last, sizeof l->last, "%.*s", (int)len, line);
    l->last_boot = m->boot;
    l->last_wall = m->wall_ms;
    l->n++;
    return l->n != l->stop_at;
}

static void test_log(void)
{
    flash_new(30);
    reboot();
    char line[64];
    s_wall = 0;
    for (int i = 0; i < 100; i++) {
        snprintf(line, sizeof line, "line %d", i);
        store_log_append(line, strlen(line));
    }
    s_wall = 1760000000000;
    store_log_append("with a clock", 12);
    logs_t l = {0};
    store_log_iter(collect, &l);
    CHECK(l.n == 101 && !strcmp(l.first, "line 0") && !strcmp(l.last, "with a clock") && l.last_wall == 1760000000000,
          "101 lines, oldest first, in batches (%d)", l.n);
    logs_t s = {.stop_at = 40};
    store_log_iter(collect, &s);
    CHECK(s.n == 40, "the callback stops the iteration");
    reboot();
    store_log_append("after the reboot", 16);
    logs_t r = {0};
    store_log_iter(collect, &r);
    CHECK(r.n == 102 && !strcmp(r.last, "after the reboot") && r.last_boot == 2, "the sequence continues after a reboot");
    char longl[400];
    memset(longl, 'x', sizeof longl);
    store_log_append(longl, sizeof longl);  // cut at STORE_LOG_LINE_MAX
    s_quiet = true;
    for (int i = 0; i < 4000; i++)  // more than 384 KB: the oldest are overwritten, never an error
        store_log_append(longl, STORE_LOG_LINE_MAX);
    s_quiet = false;
    logs_t f = {0};
    store_log_iter(collect, &f);
    CHECK(f.n > 1000 && strcmp(f.first, "line 0"), "a full log rolls over (%d lines kept)", f.n);
    BALANCED("log");
    CHECK(store_factory_reset() == STORE_OK, "factory reset");
    logs_t k = {0};
    store_log_iter(collect, &k);
    CHECK(k.n == f.n, "factory reset keeps the log");
}

int main(void)
{
    // Power cuts longjmp out of the store while it holds heap memory (a real one clears the RAM),
    // so the Makefile runs them in a second pass, STORE_CUTS=1, without leak detection.
    if (getenv("STORE_CUTS")) {
        test_small_power_cuts();
        test_power_cuts();
    } else {
        test_first_boot();
        test_set_get();
        test_revalidate_at_boot();
        test_listeners_and_state();
        test_certs_and_factory_reset();
        test_edge_save_and_read();
        test_edge_too_large();
        test_pinning();
        test_pin_through_gc();
        test_no_manifest_keeps_records();
        test_orphans();
        test_log();
#if STORE_CACHE_SIZE < 5000
        test_cache_full();
#endif
    }
    store_deinit();
    nor_sim_free(&s_spi);
    printf("test_store: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
