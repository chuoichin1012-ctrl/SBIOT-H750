// Host tests for components/store on FlashDB over simulated NOR (spec §9.1, §9.4, §12.1 "Store
// Edge"): make -C stm32/firmware/test/host
// A reboot is store_deinit + store_init on the same flash; a power cut is a longjmp out of an erase
// or program, then that reboot. Every case checks that the store's lock is balanced afterwards.
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fal_nor.h"
#include "flashdb.h"
#include "nor_sim.h"
#include "store.h"
#include "store_port.h"

static int failures;
static bool s_failfast;  // STORE_FAILFAST=1 (mutants_ota.py): stop at the first wrong case

static void stop_if_failfast(void)
{
    if (s_failfast) {
        printf("FAIL (STORE_FAILFAST: stopped at the first wrong case)\ntest_store: FAILED\n");
        _exit(1);
    }
}

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            stop_if_failfast();                         \
        }                                               \
    } while (0)

static int s_alloc_fail_at = -1;  // cJSON's allocation number n fails (0 the first); -1 none
static unsigned s_alloc_failures;
__attribute__((unused)) static void *fail_malloc(size_t n)
{
    if (s_alloc_fail_at >= 0 && s_alloc_fail_at-- == 0) {
        s_alloc_failures++;
        return NULL;
    }
    return malloc(n);
}
// As the board's allocator does (json_alloc_failures in cfgmodel.c): failed allocations are counted.
unsigned json_alloc_failures(void) { return s_alloc_failures; }

static const cfg_env_t ENV ={.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};

// ---- the port ------------------------------------------------------------------------------

static int s_depth;  // lock depth: 0 between API calls
static int64_t s_wall;
static uint32_t s_uptime;
static bool s_quiet;

void store_port_lock(void) { s_depth++; }
void store_port_unlock(void) { s_depth--; }
int64_t store_port_wall_ms(void) { return s_wall; }
static int s_restarts;
void store_port_restart(void) { s_restarts++; }
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
static long s_fail_op = -1;     // that erase or program fails once with NOR_EIO, doing nothing
static int (*s_sim_read)(nor_t *, uint32_t, void *, uint32_t);
static uint32_t s_rfail_lo, s_rfail_hi;  // reads touching [lo, hi) are counted while s_rcounting;
static long s_rfail_at = -1, s_rcount;   // the one numbered s_rfail_at fails, and the next
static int s_rfail_times = 1;            // s_rfail_times - 1 attempts too (a retry meets them)
static bool s_rcounting = true;

static int failing_read(nor_t *n, uint32_t off, void *buf, uint32_t len)
{
    if (s_rfail_at >= 0 && s_rcounting && off < s_rfail_hi && off + len > s_rfail_lo) {
        long i = s_rcount++;
        if (i >= s_rfail_at && i < s_rfail_at + s_rfail_times) {
            memset(buf, 0xA5, len / 2);  // a transfer cut short: half the buffer is garbage
            return NOR_EIO;
        }
    }
    return s_sim_read(n, off, buf, len);
}

static int counting_erase(nor_t *n, uint32_t off)
{
    s_erases++;
    if (s_trace_from >= 0 && s_w.ops >= s_trace_from)
        printf("    op %ld: erase 0x%05x\n", s_w.ops, (unsigned)off);
    if (s_w.ops == s_fail_op) {
        s_fail_op = -1;
        s_w.ops++;
        return NOR_EIO;
    }
    return s_sim_erase(n, off);
}

static int tracing_program(nor_t *n, uint32_t off, const void *buf, uint32_t len)
{
    if (s_trace_from >= 0 && s_w.ops >= s_trace_from) {
        const uint8_t *b = buf;
        printf("    op %ld: program 0x%05x len %u: %02x %02x %02x %02x\n", s_w.ops, (unsigned)off, (unsigned)len, b[0],
               len > 1 ? b[1] : 0, len > 2 ? b[2] : 0, len > 3 ? b[3] : 0);
    }
    if (s_w.ops == s_fail_op) {
        s_fail_op = -1;
        s_w.ops++;
        return NOR_EIO;
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
    s_sim_read = s_spi.nor.read;
    s_spi.nor.read = failing_read;
}

static void reboot(void)
{
    store_deinit();
    s_depth = 0;
    CHECK(store_init(&ENV, &s_spi.nor) == STORE_OK, "store_init");
    BALANCED("store_init");
}

// ---- helpers -------------------------------------------------------------------------------


/** What store_edge_save must store for `json`: the body as sent (Plan 09b ruling 3). */
static char *edge_oracle(const char *json)
{
    size_t n = strlen(json);
    char *c = malloc(n + 1);
    memcpy(c, json, n + 1);
    return c;
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

/** `m` (freed) padded with spaces before its last ']' to exactly n bytes: a body as large as the limit (Plan 09d). */
static char *padded(char *m, size_t n)
{
    size_t len = strlen(m);
    char *b = malloc(n + 1);
    memcpy(b, m, len - 1);
    memset(b + len - 1, ' ', n - len);
    b[n - 1] = ']', b[n] = '\0';
    free(m);
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

/** `key` in partition `part` as FlashDB holds it, with no default table (NULL when absent); then a reboot. */
static char *raw_get(const char *part, const char *key)
{
    store_deinit();
    struct fdb_kvdb db = {0};
    uint32_t sec = 16 * 1024;
    fdb_kvdb_control(&db, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec);
    char *out = NULL;
    if (fdb_kvdb_init(&db, part, part, NULL, NULL) == FDB_NO_ERR) {
        struct fdb_blob blob;
        static char buf[12288];
        size_t n = fdb_kv_get_blob(&db, key, fdb_blob_make(&blob, buf, sizeof buf - 1));
        if (n && blob.saved.len) {
            buf[n] = '\0';
            out = malloc(n + 1);
            memcpy(out, buf, n + 1);
        }
        fdb_kvdb_deinit(&db);
    }
    reboot();
    return out;
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
    // a complete body, as the UI sends it: the default with the hostname changed
    char *def = default_text("system"), in[1024];
    const char *tail = strstr(def, "\"bufferOffline\"");
    snprintf(in, sizeof in, "{\"hostname\":\"plant-7\",%s", tail);
    free(def);
    char err[200];
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
    CHECK(got && !strcmp(got, in), "system reads back byte for byte as sent (Plan 09b ruling 3)");
    free(got);
    reboot();
    got = store_get_json("system");
    CHECK(got && !strcmp(got, in), "and after a reboot");
    free(got);

    // Business rules are the UI's and the loader's now: valid JSON that breaks them is stored as sent.
    const char *odd[][2] = {{"port1", "{\"workMode\":3,\"listenPort\":70000}"},
                            {"system", "{\"username\":\"\",\"password\":\"\"}"},
                            {"port2", "{ \"workMode\" : 3 ,\n \"listenPort\" : 4000 }"}};
    for (size_t i = 0; i < 3; i++) {
        CHECK(store_set_text(odd[i][0], odd[i][1], strlen(odd[i][1]), err, sizeof err) == STORE_OK, "%s: business-invalid accepted (%s)",
              odd[i][0], err);
        got = store_get_json(odd[i][0]);
        cJSON *g = got ? cJSON_Parse(got) : NULL, *sent = cJSON_Parse(odd[i][1]);
        bool same = g && sent;
        for (const cJSON *k = sent ? sent->child : NULL; same && k; k = k->next)
            same = cJSON_Compare(k, cJSON_GetObjectItem(g, k->string), true);
        CHECK(same && cJSON_GetArraySize(g) > cJSON_GetArraySize(sent), "%s: every value as sent, the missing keys added (%s)",
              odd[i][0], got);
        cJSON_Delete(g), cJSON_Delete(sent);
        free(got);
    }
    reboot();
    got = store_get_json("port1");
    cJSON *g = got ? cJSON_Parse(got) : NULL;
    CHECK(g && cJSON_GetNumberValue(cJSON_GetObjectItem(g, "listenPort")) == 70000, "a business-invalid config reads back after a reboot too");
    cJSON_Delete(g);
    free(got);

    // What a save still checks: the name, the syntax, the root type, the size; each keeps the stored copy.
    char *kept = store_get_json("system");
    CHECK(store_set_text("system", "{", 1, err, sizeof err) == STORE_EINVALID && !strcmp(err, "The request body is not valid JSON."),
          "not JSON: 400 (%s)", err);
    CHECK(store_set_text("system", "{} x", 4, err, sizeof err) == STORE_EINVALID, "trailing bytes: 400");
    CHECK(store_set_text("system", "[]", 2, err, sizeof err) == STORE_EINVALID && !strcmp(err, "The settings must be a JSON object."),
          "an array for system: 400 (%s)", err);
    CHECK(store_set_text("mqttPublish", "{}", 2, err, sizeof err) == STORE_EINVALID && !strcmp(err, "The settings must be a JSON array."),
          "an object for mqttPublish: 400 (%s)", err);
    CHECK(store_set_text("mqttPublish", "[]", 2, err, sizeof err) == STORE_OK, "an array for mqttPublish (%s)", err);
    char *big = malloc(CFG_SMALL_MAX + 2);
    memset(big, ' ', CFG_SMALL_MAX + 1);
    big[0] = '{', big[CFG_SMALL_MAX] = '}', big[CFG_SMALL_MAX + 1] = '\0';
    CHECK(store_set_text("system", big, CFG_SMALL_MAX + 1, err, sizeof err) == STORE_ETOOLARGE, "10 241 bytes: 413");
    free(big);
    got = store_get_json("system");
    CHECK(got && kept && !strcmp(got, kept), "the stored copy is unchanged");
    free(got), free(kept);
    CHECK(store_set_text("nonsense", "{}", 2, err, sizeof err) == STORE_ENOTFOUND, "unknown name: 404");
    CHECK(store_set_text("edgeDevices", "[]", 2, err, sizeof err) == STORE_ENOTFOUND, "edgeDevices has its own entry point");
    BALANCED("rejected sets");

    cJSON *tree = store_get("system");
    const char *pw = tree ? cJSON_GetStringValue(cJSON_GetObjectItem(tree, "password")) : NULL;
    CHECK(pw && !*pw, "store_get runs the stored system as given, an empty password too (Plan 09b T3)");
    cJSON_Delete(tree);
    CHECK(store_set_text("system", in, strlen(in), err, sizeof err) == STORE_OK, "a valid system again (%s)", err);
    tree = store_get("system");
    CHECK(tree && cJSON_IsString(cJSON_GetObjectItem(tree, "hostname")) && cJSON_GetObjectItem(tree, "username"),
          "store_get parses the cached text and fills the missing keys");
    cJSON_Delete(tree);
}

static bool str_is(const cJSON *o, const char *k, const char *want)
{
    const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(o, k));
    return v && !strcmp(v, want);
}

static void test_revalidate_at_boot(void)
{
    flash_new(3);
    reboot();
    raw_put("cfg", "system", "{\"hostname\":\"old-fw\"}");  // written by an older firmware: keys missing
    char *got = store_get_json("system");
    cJSON *g = got ? cJSON_Parse(got) : NULL;
    CHECK(g && str_is(g, "hostname", "old-fw") &&
              str_is(g, "username", "admin") &&
              cJSON_GetNumberValue(cJSON_GetObjectItem(g, "webPort")) == 80,
          "GET serves the stored values, a missing key as its default (%s)", got);
    cJSON_Delete(g);
    free(got);
    // a body that lacks keys (a newer firmware's fields, an old script): the same for GET right after the save
    char err[200];
    const char *part = "{\"ipMode\":1,\"ip\":\"192.168.0.77\",\"priority\":2}";
    CHECK(store_set_text("network", part, strlen(part), err, sizeof err) == STORE_OK, "a network body without the LTE keys (%s)", err);
    got = store_get_json("network");
    g = got ? cJSON_Parse(got) : NULL;
    CHECK(g && str_is(g, "ip", "192.168.0.77") &&
              cJSON_GetNumberValue(cJSON_GetObjectItem(g, "priority")) == 2 &&
              cJSON_IsNumber(cJSON_GetObjectItem(g, "apnAuth")) && cJSON_GetNumberValue(cJSON_GetObjectItem(g, "apnAuth")) == 0 &&
              cJSON_IsString(cJSON_GetObjectItem(g, "apn")) && cJSON_IsNumber(cJSON_GetObjectItem(g, "cellDnsMode")),
          "GET after the save: apnAuth, apn, cellDnsMode from the defaults (%s)", got);
    cJSON_Delete(g);
    free(got);
    cJSON *doc = store_get("system");
    CHECK(doc && cJSON_GetObjectItem(doc, "username"), "store_get fills the missing keys for the runtime");
    cJSON_Delete(doc);
    raw_put("cfg", "network", "not json at all");
    char *want = default_text("network");
    got = store_get_json("network");
    CHECK(got && !strcmp(got, want), "a stored copy that is not JSON is served as the default, so the UI can open it");
    free(got);
    free(want);
}

// Codex (config-defaults review): with_defaults() out of memory at any of its allocations. A save either
// succeeds and GET has every key, or answers STORE_ENOMEM before writing; nothing leaks (ASan).
static void test_with_defaults_oom(void)
{
    flash_new(41);
    reboot();
    cJSON_Hooks hooks = {fail_malloc, free};
    cJSON_InitHooks(&hooks);
    const char *part = "{\"ipMode\":1,\"ip\":\"192.168.0.78\"}";
    int ok = 0, oom = 0;
    for (int n = 0; n < 2000; n++) {
        char err[200] = "";
        s_alloc_fail_at = n;
        store_err_t e = store_set_text("network", part, strlen(part), err, sizeof err);
        bool hit = s_alloc_fail_at < 0;
        s_alloc_fail_at = -1;
        char *got = store_get_json("network");
        if (e == STORE_OK) {
            ok++;
            CHECK(got && strstr(got, "\"apnAuth\"") && strstr(got, "192.168.0.78"), "allocation %d failed, saved: every key (%s)", n, got);
        } else {
            oom++;
            CHECK(e == STORE_ENOMEM, "allocation %d failed: STORE_ENOMEM, not %d (%s)", n, e, err);
        }
        free(got);
        if (!hit)
            break;  // no allocation numbered n: every one has failed once
    }
    cJSON_InitHooks(NULL);
    CHECK(ok > 0 && oom > 0, "the sweep saw failures (%d) and a save (%d)", oom, ok);
    BALANCED("with_defaults out of memory");
}

static int s_heard;
static char s_last[32];

static void listener(const char *name, void *ctx)
{
    s_heard++;
    snprintf(s_last, sizeof s_last, "%s", name);
    CHECK(ctx == &s_heard, "listener context");
}

// Every read of a state value failing three times in turn: the value or nothing, never garbage.
static void test_state_read_failures(void)
{
    flash_new(8);
    reboot();
    uint8_t v[2] = {1, 0};
    store_state_set("do", v, 2);
    s_rfail_lo = 0;
    s_rfail_hi = 0x40000;
    s_rfail_times = 3;
    s_rcount = 0;
    s_rfail_at = 1L << 40;
    uint8_t b[2];
    store_state_get("do", b, 2);
    long reads = s_rcount;
    int bad = 0;
    for (long k = 0; k < reads; k++) {
        reboot();
        s_rcount = 0;
        s_rfail_at = k;
        memset(b, 0x5A, 2);
        size_t n = store_state_get("do", b, 2);
        s_rfail_at = -1;
        if (!(n == 0 || (n == 2 && b[0] == 1 && b[1] == 0)) && bad++ < 3)
            printf("  state read %ld of %ld failing: %zu bytes %02x %02x\n", k, reads, n, b[0], b[1]);
    }
    s_rfail_times = 1;
    CHECK(bad == 0, "state: %d of %ld read failures hand out garbage", bad, reads);
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
    store_set_text("system", "{\"hostname\":", 12, err, sizeof err);
    CHECK(s_heard == 1, "a rejected set (not JSON) does not notify");

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
    const char *kinds[] = {"ca", "cert", "key"};
    for (int i = 0; i < 3; i++)
        CHECK(store_cert_set("port2", kinds[i], pem, strlen(pem)) == STORE_OK, "port2 %s accepted (spec §10.2)", kinds[i]);
    reboot();
    store_subscribe(listener, &s_heard);
    for (int i = 0; i < 3; i++) {
        char *c = store_cert_get("port2", kinds[i]);
        CHECK(c && !strcmp(c, pem), "port2 %s kept across a reboot", kinds[i]);
        free(c);
    }
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

// ---- Plan 09c T3: edgeDevices streamed into the records ---------------------------------------------

/** Feeds `m` to a session in chunks of `chunk` bytes, stopping after `stop` chunks (-1: all); the commit's result
 * when it went to the end, else what the last write gave. */
static store_err_t stream_save(const char *m, size_t len, size_t chunk, long stop, char *err, size_t errlen)
{
    store_err_t e = store_edge_begin(len, err, errlen);
    if (e != STORE_OK)
        return e;
    long k = 0;
    for (size_t o = 0; o < len; o += chunk, k++) {
        if (stop >= 0 && k == stop)
            return STORE_OK;  // the caller aborts or leaves it open
        e = store_edge_write(m + o, len - o < chunk ? len - o : chunk);
        if (e != STORE_OK)
            return e;
    }
    return store_edge_commit(err, errlen);
}

static void test_edge_stream(void)
{
    flash_new(41);
    reboot();
    char err[200], *m0 = edge_model(10, 3, 1), *m = padded(edge_model(100, 3, 2), 40000);
    size_t len = strlen(m);
    CHECK(store_edge_save(m0, strlen(m0), err, sizeof err) == STORE_OK, "a first model (%s)", err);
    CHECK(len > 2 * STORE_EDGE_RECORD, "the body spans three records (%zu bytes)", len);
    const size_t sizes[] = {1, 2, 3, 5, 7, 64, 1000, 4095, 4096, 4097, 8191, 8192, 8193, 9000};
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        CHECK(stream_save(m, len, sizes[i], -1, err, sizeof err) == STORE_OK, "chunks of %zu: committed (%s)", sizes[i], err);
        char *e = edge_now();
        store_edge_pin_t p;
        store_edge_pin(&p);
        store_edge_unpin(&p);
        CHECK(e && !strcmp(e, m) && p.count == (len + STORE_EDGE_RECORD - 1) / STORE_EDGE_RECORD && edge_keys("ed.") == (int)p.count,
              "chunks of %zu: the same text and records as one save (%u records)", sizes[i], (unsigned)p.count);
        free(e);
    }
    // An abort after any chunk: the committed model stays, and so does a GET pinned before the session.
    CHECK(store_edge_save(m0, strlen(m0), err, sizeof err) == STORE_OK, "back to the first model");
    for (long stop = 0; stop <= (long)(len / 1000); stop += 3) {
        store_edge_pin_t pin;
        store_edge_pin(&pin);
        stream_save(m, len, 1000, stop, err, sizeof err);
        store_edge_abort();
        char *e = edge_now(), buf[STORE_EDGE_RECORD];
        size_t got = 0;
        bool same = true;
        for (uint32_t r = 0; r < pin.count; r++) {
            size_t n = store_edge_record(&pin, r, buf, sizeof buf);
            same &= n && got + n <= strlen(m0) && !memcmp(buf, m0 + got, n);
            got += n;
        }
        store_edge_unpin(&pin);
        CHECK(e && !strcmp(e, m0) && same && got == strlen(m0) && edge_keys("ed.") == (int)pin.count,
              "abort after %ld chunks: the committed model, its pinned GET, and no record left of the session", stop);
        free(e);
    }
    // What a whole body must be: valid JSON, an array, all of it.
    char *bad = malloc(len + 1);
    memcpy(bad, m, len + 1);
    bad[len - 3000] = '#';  // in the last record
    char *e;
    CHECK(stream_save(bad, len, 1000, -1, err, sizeof err) == STORE_EINVALID && !strcmp(err, "The request body is not valid JSON."),
          "bad JSON in the last record: 400 (%s)", err);
    const char *obj = "{\"name\":\"x\"}";
    CHECK(stream_save(obj, strlen(obj), 4, -1, err, sizeof err) == STORE_EINVALID && !strcmp(err, "The settings must be a JSON array."),
          "an object: 400 (%s)", err);
    CHECK(store_edge_begin(len, err, sizeof err) == STORE_OK && store_edge_write(m, len - 1) == STORE_OK &&
              store_edge_commit(err, sizeof err) == STORE_EINVALID,
          "a body one byte short: nothing committed (%s)", err);
    e = edge_now();
    CHECK(e && !strcmp(e, m0) && edge_keys("ed.") == 1, "the first model stands, no record of the refused bodies");
    free(e);
    CHECK(store_edge_begin(96001, err, sizeof err) == STORE_ETOOLARGE, "96 001 bytes announced: 413");
    CHECK(store_edge_begin(96000, err, sizeof err) == STORE_OK, "96 000 bytes announced: a session");
    store_edge_abort();
    CHECK(store_edge_begin(len, err, sizeof err) == STORE_OK && store_edge_begin(len, err, sizeof err) == STORE_EBUSY,
          "a second session while one is open: 503");
    store_edge_abort();
    CHECK(stream_save(m, len, 4096, -1, err, sizeof err) == STORE_OK, "after the abort a new save works (%s)", err);
    free(bad), free(m), free(m0);
    BALANCED("edge stream");
}

// ---- Plan 09b T3: configs saved by the previous firmware ------------------------------------------

static bool count_dev(void *ctx, const cJSON *dev, int index) { (void)dev, (void)index; ((int *)ctx)[0]++; return true; }
static bool count_node(void *ctx, const cJSON *node, int dev) { (void)node, (void)dev; ((int *)ctx)[1]++; return true; }
static bool grow(void *ctx, const char *t, size_t n)
{
    char **b = ctx;
    size_t k = *b ? strlen(*b) : 0;
    *b = realloc(*b, k + n + 1);
    memcpy(*b + k, t, n);
    (*b)[k + n] = '\0';
    return true;
}

static void test_previous_firmware(void)
{
    flash_new(37);
    reboot();
    // What the previous save path stored: cfg_validate_text's normalised text, every field filled.
    char old[CFG_SMALL_BUF], err[200];
    cJSON *cur = cfg_default(&ENV, "mqtt");
    const char *body = "{\"enabled\":1,\"host\":\"broker.example\",\"port\":1884,\"keepAlive\":45}";
    CHECK(cfg_validate_text(&ENV, "mqtt", body, strlen(body), cur, old, sizeof old, err, sizeof err) == CFG_OK, "oracle: %s", err);
    cJSON_Delete(cur);
    raw_put("cfg", "mqtt", old);
    char *got = store_get_json("mqtt");
    CHECK(got && !strcmp(got, old), "GET returns the previous firmware's bytes");
    free(got);
    cJSON *doc = store_get("mqtt");
    char *printed = doc ? cJSON_PrintUnformatted(doc) : NULL;
    CHECK(printed && !strcmp(printed, old), "and the runtime gets every value as stored");
    free(printed), cJSON_Delete(doc);
    // Its Edge model: cfg_edge_text's normalised output, cut by byte into 8 KB records.
    char *m = edge_model(20, 5, 7), *norm = NULL;
    CHECK(cfg_edge_text(&ENV, m, strlen(m), true, CFG_EDGE_MAX, grow, &norm, err, sizeof err) == CFG_OK, "oracle: %s", err);
    CHECK(norm && store_edge_save(norm, strlen(norm), err, sizeof err) == STORE_OK, "stored (%s)", err);
    reboot();
    char *e = edge_now();
    CHECK(e && norm && !strcmp(e, norm), "GET of the model returns its bytes");
    int counts[2] = {0, 0};
    CHECK(e && cfg_edge_load(&ENV, e, strlen(e), count_dev, count_node, counts, err, sizeof err) == CFG_OK && counts[0] == 20 &&
              counts[1] == 100,
          "the boot loads it as before: %d devices, %d points (%s)", counts[0], counts[1], err);
    free(e), free(norm), free(m);
    BALANCED("previous firmware");
}

// ---- Plan 09b T1: the defaults in FlashDB, the factory reset ----------------------------------

static void test_defaults_stored(void)
{
    flash_new(31);
    reboot();
    const char *names[32];
    size_t n = cfg_names(&ENV, names, 32);
    for (size_t i = 0; i < n; i++) {
        if (!strcmp(names[i], "edgeDevices"))
            continue;
        char *raw = raw_get("cfg", names[i]);
        const char *want = cfg_default_text(&ENV, names[i]);
        CHECK(raw && want && !strcmp(raw, want), "%s: a blank flash is created with its default stored", names[i]);
        free(raw);
    }
    // An older DB that lacks a config (a newer firmware added it): read from the table, never written.
    flash_new(32);
    store_deinit();
    {
        struct fdb_kvdb db = {0};
        uint32_t sec = 16 * 1024;
        fdb_kvdb_control(&db, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec);
        fdb_kvdb_init(&db, "cfg", "cfg", NULL, NULL);
        struct fdb_blob blob;
        const char *sys = "{\"hostname\":\"old\"}";
        fdb_kv_set_blob(&db, "system", fdb_blob_make(&blob, sys, strlen(sys)));
        fdb_kvdb_deinit(&db);
    }
    reboot();
    char *got = store_get_json("mqtt"), *want = default_text("mqtt");
    CHECK(got && want && !strcmp(got, want), "a config missing from an older DB loads its default");
    free(got), free(want);
    char *raw = raw_get("cfg", "mqtt");
    CHECK(!raw, "and the default is not written to it");
    free(raw);
    BALANCED("defaults");
}

static long s_programs;
static int counting_program(nor_t *n, uint32_t off, const void *buf, uint32_t len)
{
    s_programs++;
    return tracing_program(n, off, buf, len);
}

static void test_reset_blocks_and_restarts(void)
{
    flash_new(33);
    reboot();
    char err[200];
    store_set_text("system", "{\"hostname\":\"before\"}", 21, err, sizeof err);
    s_restarts = 0;
    CHECK(store_factory_reset() == STORE_OK && s_restarts == 1, "a factory reset restarts the gateway once (%d)", s_restarts);
    s_spi.nor.program = counting_program;
    long e0 = s_erases;
    s_programs = 0;
    const char *pem = "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n";
    uint8_t mask = 1;
    CHECK(store_set_text("system", "{\"hostname\":\"after\"}", 20, err, sizeof err) != STORE_OK &&
              store_edge_save("[]", 2, err, sizeof err) != STORE_OK && store_cert_set("mqtt", "ca", pem, strlen(pem)) != STORE_OK &&
              store_state_set("do", &mask, 1) != STORE_OK,
          "after the reset began, every write is refused until the restart");
    CHECK(s_programs == 0 && s_erases == e0, "and nothing reaches the flash (%ld programs, %ld erases)", s_programs, s_erases - e0);
    s_spi.nor.program = tracing_program;
    reboot();
    char *got = store_get_json("system"), *want = default_text("system");
    CHECK(got && want && !strcmp(got, want), "after the restart: the defaults, writable again");
    free(got), free(want);
    CHECK(store_set_text("system", "{\"hostname\":\"after\"}", 20, err, sizeof err) == STORE_OK, "saves work again (%s)", err);
    BALANCED("reset blocks");
}

static void test_reset_read_back(void)
{
    flash_new(34);
    reboot();
    char err[200];
    store_set_text("system", "{\"hostname\":\"before\"}", 21, err, sizeof err);
    s_restarts = 0;
    sim_fail(&s_w, 0, 2, 0x00000, 0x40000, SIM_SILENT);  // the defaults are "written" and land nowhere
    s_quiet = true;
    store_err_t e = store_factory_reset();
    s_quiet = false;
    sim_fail(&s_w, 0, 0, 0, 0, 0);
    CHECK(e != STORE_OK && s_restarts == 1, "defaults that did not land: the read-back reports the reset incomplete, and it restarts");
    BALANCED("reset read-back");
}

static void test_reset_unmounted(void)
{
    flash_new(35);
    reboot();
    store_deinit();
    sim_fail(&s_w, 0, 2, 0x00000, 0x40000, SIM_READ_EIO);  // cfg cannot be read: the FAL locks the flash read-only
    s_quiet = true;
    store_init(&ENV, &s_spi.nor);
    s_spi.nor.program = counting_program;
    long e0 = s_erases;
    s_programs = 0;
    s_restarts = 0;
    store_err_t e = store_factory_reset();
    s_quiet = false;
    sim_fail(&s_w, 0, 0, 0, 0, 0);
    s_spi.nor.program = tracing_program;
    CHECK(e != STORE_OK && s_restarts == 1, "cfg not mounted: the reset is incomplete, and it restarts");
    CHECK(s_programs == 0 && s_erases == e0, "nothing written or erased while the FAL is locked (%ld, %ld)", s_programs, s_erases - e0);
    BALANCED("reset unmounted");
}

#if STORE_CACHE_SIZE < 5000
// Built with a 4800-byte cache (test_store_small): the defaults take most of it. A body replaces its
// config's text, so what has to fit is the free room plus that config's own stored text.
static void test_cache_full(void)
{
    flash_new(6);
    reboot();
    char *was = store_get_json("mqtt"), err[200];
    size_t own = strlen(was), used = 0;
    const char *names[32];
    size_t nn = cfg_names(&ENV, names, 32);
    for (size_t i = 0; i < nn; i++) {
        char *t = store_get_json(names[i]);
        used += t ? strlen(t) : 0;
        free(t);
    }
    size_t room = STORE_CACHE_SIZE - used;  // free bytes besides mqtt's own text
    char *in = malloc(own + room + 100), *a = malloc(own + room + 100);
    memset(a, 'a', own + room + 100);
    // the complete mqtt (as the UI sends it), its host grown by k bytes: own + k in all
    const char *h = strstr(was, "\"host\":\"") + 8;
#define MQTT_WITH(k) snprintf(in, own + room + 100, "%.*s%.*s%s", (int)(h - was), was, (int)(k), a, h)
    MQTT_WITH(room + 1);  // one byte over
    CHECK(store_set_text("mqtt", in, strlen(in), err, sizeof err) == STORE_ETOOLARGE &&
              !strcmp(err, "The settings do not fit in the gateway's memory."),
          "more than the room left: 413 (%s)", err);
    char *now = store_get_json("mqtt");
    CHECK(now && !strcmp(now, was), "the cached copy unchanged");
    free(now);
    reboot();
    now = store_get_json("mqtt");
    CHECK(now && !strcmp(now, was), "and nothing was written to flash");
    free(now);
    MQTT_WITH(room);  // exactly the room
#undef MQTT_WITH
    CHECK(store_set_text("mqtt", in, strlen(in), err, sizeof err) == STORE_OK, "exactly the room left: fits (%s)", err);
    BALANCED("cache full");
    free(in), free(a), free(was);
}
#endif

// ---- edgeDevices -----------------------------------------------------------------------------

static void test_factory_reset_fails(void)
{
    flash_new(7);
    reboot();
    char err[200], *m = edge_model(4, 2, 1), *w = edge_oracle(m);
    store_edge_save(m, strlen(m), err, sizeof err);
    store_set_text("system", "{\"hostname\":\"kept\"}", 19, err, sizeof err);
    sim_fail(&s_w, 0, 2, 0x3C000, 0x40000, NOR_EIO);  // the cfg reset fails partway: its last sector
    CHECK(store_factory_reset() == STORE_EFLASH, "factory reset with cfg failing: 500");
    sim_fail(&s_w, 0, 0, 0, 0, 0);
    char *before = edge_now(), *gb = store_get_json("system");
    reboot();
    char *after = edge_now(), *ga = store_get_json("system");
    CHECK((!before || (after && !strcmp(before, after))) && gb && ga && !strcmp(gb, ga),
          "what the store serves after the failed reset is what the next boot finds");
    free(before), free(after), free(gb), free(ga);

    // the edge reset failing partway (its last sector): the generation in RAM follows the flash
    store_edge_save(m, strlen(m), err, sizeof err);
    sim_fail(&s_w, 0, 2, 0x7C000, 0x80000, NOR_EIO);
    CHECK(store_factory_reset() == STORE_EFLASH, "factory reset with edge failing partway: 500");
    sim_fail(&s_w, 0, 0, 0, 0, 0);
    before = edge_now();
    reboot();
    after = edge_now();
    // The failed erase locked the flash (Plan 09b T2): the store serves no model rather than a wrong one.
    CHECK(after && (!before || !strcmp(before, after)), "the Edge model served, if any, is the one the next boot finds (%.12s / %.12s)",
          before ? before : "-", after ? after : "-");
    free(before), free(after), free(m), free(w);
}

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
    CHECK(e && !strcmp(e, want), "the stored text is the body as sent (%zu bytes)", strlen(want));
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

    // rejected bodies leave the committed model alone; the rules are the loader's (Plan 09b ruling 3)
    CHECK(store_edge_save("[", 1, err, sizeof err) == STORE_EINVALID, "not JSON: 400");
    CHECK(store_edge_save("{}", 2, err, sizeof err) == STORE_EINVALID && !strcmp(err, "The settings must be a JSON array."),
          "not an array: 400 (%s)", err);
    char *huge = malloc(96001 + 1);
    memset(huge, ' ', 96001);
    huge[0] = '[', huge[96000] = ']', huge[96001] = '\0';
    CHECK(store_edge_save(huge, 96001, err, sizeof err) == STORE_ETOOLARGE, "96 001 bytes: 413");
    free(huge);
    store_edge_pin(&p);
    e = edge_now();
    CHECK(p.gen == 2 && e && !strcmp(e, want), "still generation 2, same text");
    free(e);
    store_edge_unpin(&p);
    CHECK(edge_keys("ed.") == (int)p.count, "and no records of the failed saves left");
    BALANCED("rejected edge saves");
    // a business-invalid model (slave id 0) is stored as sent: the loader decides at boot
    const char *odd = "[{\"name\":\"x\",\"source\":\"uart\",\"uart\":1,\"protocol\":\"modbus\",\"addr\":\"0\"}]";
    CHECK(store_edge_save(odd, strlen(odd), err, sizeof err) == STORE_OK, "a business-invalid model is saved (%s)", err);
    e = edge_now();
    CHECK(e && !strcmp(e, odd), "and reads back as sent");
    free(e);
    free(model);
    free(want);
    free(rec);
}

static void test_edge_too_large(void)
{
    // Exactly 96 000 bytes (Plan 09d), a 2-byte UTF-8 letter cut by every record boundary: stored in 6 records
    // and read back as sent.
    const size_t max = 96000;
    flash_new(11);
    reboot();
    char *m = malloc(max + 1), err[200];
    size_t o = 0;
    o += (size_t)sprintf(m, "[{\"name\":\"");
    while (o < max - 3) {
        if (o % STORE_EDGE_RECORD == STORE_EDGE_RECORD - 1)
            m[o++] = (char)0xC3, m[o++] = (char)0xA9;  // "é" across the boundary
        else
            m[o++] = 'a';
    }
    while (o < max - 3)
        m[o++] = 'a';
    memcpy(m + o, "\"}]", 3), o += 3;
    m[o] = '\0';
    CHECK(o == max, "the body is 96 000 bytes (%zu)", o);
    CHECK(store_edge_save(m, o, err, sizeof err) == STORE_OK, "a 96 000-byte body: saved (%s)", err);
    store_edge_pin_t p;
    store_edge_pin(&p);
    store_edge_unpin(&p);
    CHECK(p.count == 6 && edge_keys("ed.") == 6, "in 6 records (%u)", (unsigned)p.count);
    char *e = edge_now();
    CHECK(e && !strcmp(e, m), "read back byte for byte, UTF-8 cut across records");
    free(e);
    reboot();
    e = edge_now();
    CHECK(e && !strcmp(e, m), "and after a reboot");
    free(e), free(m);
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
    CHECK(edge_keys("ed.3.") == 0 && edge_keys("ed.4.") == 1, "the save (generation 4, above the stray) removes the stray and keeps its own");
    free(m);
}

// The manifest lost behind the store's back (or unreadable): a rejected POST must not touch the
// records, and the next generation is numbered above every record in flash.
static void test_lost_manifest(void)
{
    flash_new(16);
    reboot();
    char err[200], *m = edge_model(4, 2, 1);
    store_edge_save(m, strlen(m), err, sizeof err);
    store_deinit();
    struct fdb_kvdb db = {0};
    uint32_t sec = 16 * 1024;
    fdb_kvdb_control(&db, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec);
    fdb_kvdb_init(&db, "edge", "edge", NULL, NULL);
    fdb_kv_del(&db, "edgeDevices");
    fdb_kvdb_deinit(&db);
    reboot();
    int had = edge_keys("ed.1.");
    CHECK(had == 1, "generation 1's record still there without its manifest (%d)", had);
    CHECK(store_edge_save("[", 1, err, sizeof err) == STORE_EINVALID && edge_keys("ed.1.") == 1, "a rejected POST leaves it");
    char *m2 = edge_model(4, 2, 2);
    CHECK(store_edge_save(m2, strlen(m2), err, sizeof err) == STORE_OK, "a valid POST");
    store_edge_pin_t p;
    store_edge_pin(&p);
    store_edge_unpin(&p);
    CHECK(p.gen == 2 && edge_keys("ed.1.") == 0, "is generation 2, above the records found (%u)", (unsigned)p.gen);
    free(m), free(m2);
}

static void on_alarm(int sig)
{
    (void)sig;
    printf("FAIL: the store hung (a loop that never ends while it holds the lock)\ntest_store: FAILED\n");
    _exit(1);
}

// Deletes and status writes that report success and change nothing (SIM_SILENT): every loop of
// the store and of FlashDB's recovery must still end.
static void test_no_endless_loops(void)
{
    signal(SIGALRM, on_alarm);
    flash_new(17);
    reboot();
    char err[200], *m = edge_model(4, 2, 1);
    store_edge_save(m, strlen(m), err, sizeof err);
    store_deinit();  // twenty stray records, written in one go: more than one cleanup pass
    struct fdb_kvdb db = {0};
    uint32_t sec = 16 * 1024;
    fdb_kvdb_control(&db, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec);
    fdb_kvdb_init(&db, "edge", "edge", NULL, NULL);
    for (int i = 0; i < 20; i++) {
        char key[24];
        struct fdb_blob blob;
        snprintf(key, sizeof key, "ed.9.%d", i);
        fdb_kv_set_blob(&db, key, fdb_blob_make(&blob, "stray", 5));
    }
    fdb_kvdb_deinit(&db);
    sim_fail(&s_w, 0, 2, 0x40000, 0x80000, SIM_SILENT);
    alarm(20);
    reboot();  // the boot cleanup cannot delete them
    char *m2 = edge_model(4, 2, 2);
    store_edge_save(m2, strlen(m2), err, sizeof err);  // nor can the save's cleanup
    alarm(0);
    sim_fail(&s_w, 0, 0, 0, 0, 0);
    CHECK(1, "returned");

    // A KV left half-written by a cut, then status writes that silently fail: FlashDB's
    // recovery scan (patched to rescan) must give up after a bound, not loop.
    flash_new(18);
    reboot();
    store_edge_save(m, strlen(m), err, sizeof err);
    store_deinit();
    s_w.ops = 0;
    s_w.cut_at = 10;  // inside the data of the first record: a KV left PRE_WRITE
    if (!setjmp(s_jb)) {
        reboot();
        store_edge_save(m2, strlen(m2), err, sizeof err);
    }
    s_w.cut_at = -1;
    sim_fail(&s_w, 0, 2, 0x40000, 0x80000, SIM_SILENT);
    alarm(20);
    reboot();
    alarm(0);
    // the recovery could not finish: the edge database is unusable, never an empty model
    store_edge_pin_t pp;
    store_edge_pin(&pp);
    char *e = edge_now();
    CHECK(pp.count == 0 && e == NULL, "recovery unfinished: no model is served, not even the default");
    CHECK(store_edge_save(m2, strlen(m2), err, sizeof err) == STORE_EFLASH, "and a save is refused (500)");
    free(e);
    sim_fail(&s_w, 0, 0, 0, 0, 0);
    reboot();
    e = edge_now();
    CHECK(e && strcmp(e, "[]"), "the flash readable again: the model is back (%.20s)", e ? e : "(none)");
    free(e);
    free(m), free(m2);
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

/** A generation saved by Plan 09c's firmware, in 8 KB records: read back the same; the next save rewrites it in
 * 16 000-byte records (Plan 09d ruling 1). */
static void test_old_records(void)
{
    flash_new(43);
    reboot();
    char err[200], *m = padded(edge_model(10, 4, 3), 20000), part[8193];
    for (int i = 0; i < 3; i++) {
        size_t n = strlen(m + i * 8192) < 8192 ? strlen(m + i * 8192) : 8192;
        memcpy(part, m + i * 8192, n), part[n] = '\0';
        char key[] = "ed.1.0";
        key[5] = (char)('0' + i);
        raw_put("edge", key, part);
    }
    raw_put("edge", "edgeDevices", "@1,3");
    char *e = edge_now();
    CHECK(e && !strcmp(e, m), "8 KB records of an earlier firmware: read back the same");
    free(e);
    char *m2 = padded(edge_model(10, 4, 4), 20000);
    CHECK(store_edge_save(m2, strlen(m2), err, sizeof err) == STORE_OK, "the next save (%s)", err);
    store_edge_pin_t p;
    store_edge_pin(&p);
    store_edge_unpin(&p);
    e = edge_now();
    CHECK(e && !strcmp(e, m2) && p.gen == 2 && p.count == 2 && edge_keys("ed.1.") == 0 && edge_keys("ed.2.") == 2,
          "rewritten in 16 000-byte records: generation %u, %u records", (unsigned)p.gen, (unsigned)p.count);
    free(e), free(m), free(m2);
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

// big: the measured save is a 96 000-byte body (Plan 09d), 6 records over the committed one.
static void power_cuts(bool big)
{
    s_snap = malloc(0x100000);
    flash_new(big ? 25 : 20);
    reboot();
    char err[200];
    char *ma = edge_model(14, 4, 1), *wa = edge_oracle(ma), *mb = edge_model(16, 4, 2);
    if (big)
        mb = padded(mb, 96000);
    char *wb = edge_oracle(mb);
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
        if (!ok && (stop_if_failfast(), bad++ < 5))
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
        if (!ok && (stop_if_failfast(), bad++ < 5))
            printf("  3 cuts from op %ld every %ld: wrong (%s)\n", cut, period, err);
        free(e), free(e2), free(mc), free(wc);
    }
    CHECK(bad == 0, "edge save with three cuts: %d of 150 runs end wrong", bad);
    s_quiet = false;
    free(ma), free(wa), free(mb), free(wb), free(s_snap);
}

static void test_power_cuts(void)
{
    power_cuts(false);
    power_cuts(true);
}

// A write failing once (NOR_EIO, nothing written) at every operation of an edge save: whatever
// the save answers, the model it leaves in RAM is the one the reboot finds, and it is exactly the
// old or the new one (a commit that FlashDB reports as failed must not lose the new records).
static void write_failures_from(bool empty, bool big);

static void test_write_failures(void)
{
    write_failures_from(false, false);
    write_failures_from(true, false);
    write_failures_from(false, true);
}

// empty: no model saved before the measured save (old generation 0, the default text).
// big: the measured save is a 96 000-byte body (Plan 09d).
static void write_failures_from(bool empty, bool big)
{
    s_snap = malloc(0x100000);
    flash_new(empty ? 24 : big ? 26 : 22);
    reboot();
    char err[200];
    char *ma = edge_model(14, 4, 1), *mb = edge_model(16, 4, 2);
    if (big)
        mb = padded(mb, 96000);
    char *wb = edge_oracle(mb);
    char *wa = empty ? default_text("edgeDevices") : edge_oracle(ma);
    for (int i = 0; !empty && i < 40; i++) {  // as in test_power_cuts: the measured save collects garbage
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
    if (empty) {
        store_deinit();
        snapshot();
    }
    store_deinit();
    restore();
    reboot();
    s_w.ops = 0;
    store_edge_save(mb, strlen(mb), err, sizeof err);
    long total = s_w.ops;
    int bad = 0;
    s_quiet = true;
    for (long op = 0; op < total; op++) {
        store_deinit();
        restore();
        reboot();
        s_w.ops = 0;
        s_fail_op = op;
        store_err_t r = store_edge_save(mb, strlen(mb), err, sizeof err);
        s_fail_op = -1;
        char *before = edge_now();  // NULL: the store says it cannot tell (until a reboot)
        store_edge_pin_t pp;
        store_edge_pin(&pp);
        store_edge_unpin(&pp);
        // a failed save whose outcome is known leaves no record but the committed generation's
        int keys = edge_keys("ed.");  // counted before the boot cleanup could hide a stray
        bool clean = !before || r == STORE_OK || keys == (int)(pp.gen ? pp.count : 0);
        if (!clean && bad < 5)
            printf("  write fails at op %ld: %d records left for generation %u (%u records)\n", op, keys, (unsigned)pp.gen,
                   (unsigned)pp.count);
        reboot();
        char *after = edge_now();
        bool ok = after && (!before || !strcmp(before, after)) && (!strcmp(after, wa) || !strcmp(after, wb)) &&
                  (r != STORE_OK || !strcmp(after, wb)) && clean;
        if (!ok && (stop_if_failfast(), bad++ < 5))
            printf("  write fails at op %ld of %ld: save %d, before %s, after %s\n", op, total, r,
                   before ? (!strcmp(before, wa) ? "old" : !strcmp(before, wb) ? "new" : "OTHER") : "none",
                   after ? (!strcmp(after, wa) ? "old" : !strcmp(after, wb) ? "new" : "OTHER") : "none");
        free(before), free(after);
    }
    s_quiet = false;
    CHECK(bad == 0, "edge save%s: %d of %ld single write failures end inconsistent",
          empty ? " (no model before)" : big ? " (96 000 bytes)" : "", bad, total);
    free(ma), free(wa), free(mb), free(wb), free(s_snap);
}

// FlashDB itself: a KV written once ("keep") while another is rewritten until the garbage
// collector has to move "keep". A write failing once at any operation of that churn must never
// lose "keep": a failed move used to be logged, and its sector erased all the same.
static bool keep_intact(void)
{
    struct fdb_kvdb db = {0};
    uint32_t sec = 16 * 1024;
    fdb_kvdb_control(&db, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec);
    if (fdb_kvdb_init(&db, "cert", "cert", NULL, NULL) != FDB_NO_ERR)
        return false;
    char v[64] = "";
    struct fdb_blob blob;
    size_t n = fdb_kv_get_blob(&db, "keep", fdb_blob_make(&blob, v, sizeof v - 1));
    fdb_kvdb_deinit(&db);
    return n == 17 && !memcmp(v, "a value to keep!!", 17);
}

static bool s_count_mount = true;  // false: reads of the mount itself are not counted

static long churn(int writes)
{
    struct fdb_kvdb db = {0};
    uint32_t sec = 16 * 1024;
    fdb_kvdb_control(&db, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec);
    s_rcounting = s_count_mount;
    fdb_kvdb_init(&db, "cert", "cert", NULL, NULL);
    s_rcounting = true;
    static char big[6000];
    long moves = 0;
    for (int i = 0; i < writes; i++) {
        memset(big, 'a' + i % 26, sizeof big);
        struct fdb_blob blob;
        long e0 = s_erases;
        fdb_kv_set_blob(&db, "churn", fdb_blob_make(&blob, big, sizeof big));
        moves += s_erases - e0;
    }
    fdb_kvdb_deinit(&db);
    return moves;
}

static void test_gc_write_failures(void)
{
    s_snap = malloc(0x100000);
    flash_new(23);
    store_deinit();
    struct fdb_kvdb db = {0};
    uint32_t sec = 16 * 1024;
    fdb_kvdb_control(&db, FDB_KVDB_CTRL_SET_SEC_SIZE, &sec);
    fdb_kvdb_init(&db, "cert", "cert", NULL, NULL);
    struct fdb_blob blob;
    fdb_kv_set_blob(&db, "keep", fdb_blob_make(&blob, "a value to keep!!", 17));
    fdb_kvdb_deinit(&db);
    snapshot();
    s_w.ops = 0;
    long erases = churn(40);
    long total = s_w.ops;
    CHECK(erases > 0 && keep_intact(), "the churn collects garbage (%ld erases) and keeps the KV", erases);
    if (getenv("STORE_GC_DEBUG")) {  // one failing op, traced, with FlashDB's messages
        long op = atol(getenv("STORE_GC_DEBUG"));
        restore();
        s_w.ops = 0;
        s_fail_op = op;
        s_trace_from = op - 6;
        churn(40);
        s_trace_from = -1;
        printf("--- keep intact after: %d\n", keep_intact());
    }
    int bad = 0;
    s_quiet = true;
    for (long op = 0; op < total; op++) {
        restore();
        s_w.ops = 0;
        s_fail_op = op;
        churn(40);
        s_fail_op = -1;
        if (!keep_intact() && (stop_if_failfast(), bad++ < 5))
            printf("  FlashDB: a write failing at op %ld of %ld lost the KV\n", op, total);
    }
    s_quiet = false;
    CHECK(bad == 0, "FlashDB garbage collection: %d of %ld single write failures lose a live KV", bad, total);

    // Reads of the sector holding "keep" failing: (a) once, the k-th of them, the mount's included
    // (a transient fault: the FAL retries); (b) three times in a row after the mount (a fault that
    // outlasts the retries), the garbage collector's moves among them.
    s_rfail_lo = 0x80000;
    s_rfail_hi = 0x84000;
    for (int pass = 0; pass < 3; pass++) {
        s_rfail_times = pass ? 3 : 1;
        s_count_mount = pass != 1;
        restore();
        s_rcount = 0;
        s_rfail_at = 1L << 40;  // count only
        churn(40);
        long reads = s_rcount;
        if (pass == 1 && getenv("STORE_GC_RDEBUG")) {
            restore();
            s_rcount = 0;
            s_rfail_at = atol(getenv("STORE_GC_RDEBUG"));
            printf("--- read %ld fails 3 times:\n", s_rfail_at);
            s_trace_from = 0;
            churn(40);
            s_trace_from = -1;
            s_rfail_at = -1;
            printf("--- keep intact: %d\n", keep_intact());
        }
        int rbad = 0;
        s_quiet = true;
        for (long k = 0; k < reads; k++) {
            restore();
            fal_nor_rearm();
            s_rcount = 0;
            s_rfail_at = k;
            churn(40);
            s_rfail_at = -1;
            fal_nor_rearm();  // the read fault is gone: what is left on the flash?
            if (!keep_intact() && (stop_if_failfast(), rbad++ < 5))
                printf("  FlashDB: read %ld of %ld failing %d time(s) lost the KV\n", k, reads, s_rfail_times);
        }
        s_quiet = false;
        CHECK(rbad == 0, "FlashDB: %d of %ld read failures (%s) lose a live KV", rbad, reads,
              pass == 0 ? "once, anywhere" : pass == 1 ? "three in a row, after the mount" : "three in a row, anywhere");
    }
    s_rfail_times = 1;
    s_count_mount = true;
    printf("store: FlashDB churn of %ld operations with garbage collection, a write failing at each\n", total);
    free(s_snap);
}

// Reads failing three times in a row (past the FAL's retries) at the k-th read of a boot: the
// flash turns read-only, so whatever the store makes of what it read, nothing is lost: the next
// boot with the fault gone finds the configs and the Edge model as they were.
static void test_boot_read_failures(void)
{
    s_snap = malloc(0x100000);
    flash_new(25);
    reboot();
    char err[200], *m = edge_model(14, 4, 1), *w = edge_oracle(m);
    store_set_text("system", "{\"hostname\":\"read-test\"}", 24, err, sizeof err);
    store_edge_save(m, strlen(m), err, sizeof err);
    char *sys = store_get_json("system");
    store_deinit();
    snapshot();
    s_rfail_lo = 0;
    s_rfail_hi = 0x100000;
    s_rfail_times = 3;
    restore();
    s_rcount = 0;
    s_rfail_at = 1L << 40;
    reboot();
    long reads = s_rcount;
    s_rfail_at = -1;
    int bad = 0;
    s_quiet = true;
    for (long k = 0; k < reads; k += 7) {
        store_deinit();
        restore();
        s_rcount = 0;
        s_rfail_at = k;
        store_init(&ENV, &s_spi.nor);
        s_depth = 0;
        char *e1 = edge_now();
        store_edge_pin_t p1;
        store_edge_pin(&p1);
        store_edge_unpin(&p1);
        bool refused = store_set_text("system", "{\"hostname\":\"x\"}", 16, err, sizeof err) != STORE_OK;
        // read-only (the save refused) serves no model at all; otherwise the model is whole
        bool ro = refused ? (!e1 && p1.count == 0) : (e1 && !strcmp(e1, w));
        free(e1);
        s_rfail_at = -1;
        reboot();
        char *e = edge_now(), *g = store_get_json("system");
        bool ok = e && !strcmp(e, w) && g && (!strcmp(g, sys) || strstr(g, "\"x\"")) && ro;
        if (!ok && (stop_if_failfast(), bad++ < 5))
            printf("  boot read %ld of %ld failing three times: %s\n", k, reads, !e ? "no model" : strcmp(e, w) ? "model changed" : "config lost");
        free(e), free(g);
    }
    s_quiet = false;
    s_rfail_times = 1;
    CHECK(bad == 0, "boot: %d of %ld read failures (three in a row) lose data", bad, (reads + 6) / 7);
    // one failing read, the FAL's retry absorbs it: the store stays writable
    int ro = 0;
    for (long k = 0; k < reads; k += 97) {
        store_deinit();
        restore();
        s_rcount = 0;
        s_rfail_at = k;
        store_init(&ENV, &s_spi.nor);
        s_depth = 0;
        s_rfail_at = -1;
        ro += store_set_text("system", "{\"hostname\":\"y\"}", 16, err, sizeof err) != STORE_OK;
    }
    CHECK(ro == 0, "a single failing read at boot: still writable (%d read-only)", ro);

    // the same on a flash with no Edge model: read-only must not serve the default either
    flash_new(27);
    reboot();
    store_deinit();
    snapshot();
    s_rfail_times = 3;
    restore();
    s_rcount = 0;
    s_rfail_at = 1L << 40;
    reboot();
    reads = s_rcount;
    s_rfail_at = -1;
    bad = 0;
    for (long k = 0; k < reads; k += 3) {
        store_deinit();
        restore();
        s_rcount = 0;
        s_rfail_at = k;
        store_init(&ENV, &s_spi.nor);
        s_depth = 0;
        char *e1 = edge_now();
        bool refused = store_set_text("system", "{\"hostname\":\"x\"}", 16, err, sizeof err) != STORE_OK;
        s_rfail_at = -1;
        if (refused && e1 && bad++ < 3)
            printf("  no model, boot read %ld failing: read-only yet serving %s\n", k, e1);
        free(e1);
    }
    s_rfail_times = 1;
    CHECK(bad == 0, "read-only after a boot read failure: no model served, not even the default (%d)", bad);
    free(m), free(w), free(sys), free(s_snap);
}

// The same during an edge save, the remount after a failed write included: reads failing three
// times in a row at the k-th read of the save. Nothing is lost, and nothing half saved is served.
static void test_save_read_failures(void)
{
    s_snap = malloc(0x100000);
    flash_new(26);
    reboot();
    char err[200], *ma = edge_model(14, 4, 1), *wa = edge_oracle(ma), *mb = edge_model(16, 4, 2), *wb = edge_oracle(mb);
    store_edge_save(ma, strlen(ma), err, sizeof err);
    store_deinit();
    snapshot();
    s_rfail_lo = 0;
    s_rfail_hi = 0x100000;
    s_rfail_times = 3;
    restore();
    reboot();
    s_rcount = 0;
    s_rfail_at = 1L << 40;
    store_edge_save(mb, strlen(mb), err, sizeof err);
    long reads = s_rcount;
    s_rfail_at = -1;
    int bad = 0;
    s_quiet = true;
    for (long k = 0; k < reads; k += 5) {
        store_deinit();
        restore();
        reboot();
        s_rcount = 0;
        s_rfail_at = k;
        store_err_t r = store_edge_save(mb, strlen(mb), err, sizeof err);
        char *before = edge_now();
        s_rfail_at = -1;
        reboot();
        char *after = edge_now();
        bool ok = after && (!strcmp(after, wa) || !strcmp(after, wb)) && (!before || !strcmp(before, after)) &&
                  (r != STORE_OK || !strcmp(after, wb));
        if (!ok && (stop_if_failfast(), bad++ < 5))
            printf("  save read %ld of %ld failing three times: save %d, after %s\n", k, reads, r,
                   !after ? "none" : !strcmp(after, wa) ? "old" : !strcmp(after, wb) ? "new" : "OTHER");
        free(before), free(after);
    }
    s_quiet = false;
    s_rfail_times = 1;
    CHECK(bad == 0, "edge save: %d of %ld read failures (three in a row) end wrong", bad, (reads + 4) / 5);
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
        if (!ok && (stop_if_failfast(), bad++ < 3))
            printf("  small save cut at %ld: %s\n", cut, g ? g : "(none)");
        free(g);
    }
    CHECK(bad == 0, "small config save: %d of %ld cut points end wrong", bad, total);
    s_quiet = false;
    free(s_snap);
}

/** A power cut in the middle of a streamed body over 8 KB, then a reboot: the next save works (Codex 09c review). */
static void test_stream_cut_reboot(void)
{
    flash_new(42);
    reboot();
    char err[200], *m = edge_model(100, 3, 4);
    size_t len = strlen(m);
    long cuts = 0, bad = 0;
    s_quiet = true;
    for (long cut = 1; cut < 40; cut += 3) {
        s_w.ops = 0;
        s_w.cut_at = cut;
        volatile bool was_cut = false;
        if (!setjmp(s_jb)) {
            store_edge_begin(len, err, sizeof err);
            for (size_t o = 0; o < len; o += 2000) store_edge_write(m + o, len - o < 2000 ? len - o : 2000);
            store_edge_abort();
        } else {
            was_cut = true;
        }
        s_w.cut_at = -1;
        cuts += was_cut;
        reboot();
        store_err_t e = store_edge_save(m, len, err, sizeof err);
        if (e != STORE_OK && bad++ < 3)
            printf("  cut at op %ld mid-body, then a reboot: the next save gives %d (%s)\n", cut, e, err);
    }
    s_quiet = false;
    CHECK(cuts > 0 && bad == 0, "a cut mid-body over 8 KB, a reboot, a save: %ld of %ld cuts end refused", bad, cuts);
    free(m);
}

static void test_reset_power_cuts(void)
{
    s_snap = malloc(0x100000);
    flash_new(36);
    reboot();
    char err[200], *m = edge_model(3, 2, 1);
    store_set_text("system", "{\"hostname\":\"before\"}", 21, err, sizeof err);
    store_set_text("mqtt", "{\"host\":\"old.example\"}", 24, err, sizeof err);
    store_edge_save(m, strlen(m), err, sizeof err);
    const char *pem = "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n";
    store_cert_set("mqtt", "ca", pem, strlen(pem));
    char *old_sys = store_get_json("system"), *old_mq = store_get_json("mqtt"), *old_edge = edge_now();
    char *def_sys = default_text("system"), *def_mq = default_text("mqtt"), *def_edge = default_text("edgeDevices");
    store_deinit();
    snapshot();
    restore();
    reboot();
    s_w.ops = 0;
    store_factory_reset();
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
            store_factory_reset();
        s_w.cut_at = -1;
        reboot();
        char *gs = store_get_json("system"), *gm = store_get_json("mqtt"), *ge = edge_now();
        bool ok = gs && gm && ge && (!strcmp(gs, old_sys) || !strcmp(gs, def_sys)) && (!strcmp(gm, old_mq) || !strcmp(gm, def_mq)) &&
                  (!strcmp(ge, old_edge) || !strcmp(ge, def_edge));
        store_factory_reset();  // the user presses Reload again
        reboot();
        char *s2 = store_get_json("system"), *e2 = edge_now(), *c2 = store_cert_get("mqtt", "ca");
        ok = ok && s2 && !strcmp(s2, def_sys) && e2 && !strcmp(e2, def_edge) && !c2;
        if (!ok && (stop_if_failfast(), bad++ < 3))
            printf("  reset cut at %ld: system %.40s mqtt %.40s edge %.20s\n", cut, gs ? gs : "-", gm ? gm : "-", ge ? ge : "-");
        free(gs), free(gm), free(ge), free(s2), free(e2), free(c2);
    }
    CHECK(bad == 0, "factory reset: %d of %ld cut points (writes and erases) end torn, or not clean after a second reset", bad, total);
    s_quiet = false;
    free(old_sys), free(old_mq), free(old_edge), free(def_sys), free(def_mq), free(def_edge), free(m);
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
    setvbuf(stdout, NULL, _IONBF, 0);
    s_failfast = getenv("STORE_FAILFAST") != NULL;  // a hang is reported by the alarm with _exit: nothing buffered may be lost
    // Power cuts longjmp out of the store while it holds heap memory (a real one clears the RAM),
    // so the Makefile runs them in a second pass, STORE_CUTS=1, without leak detection.
    if (getenv("STORE_CUTS")) {
        test_small_power_cuts();
        test_reset_power_cuts();
        test_stream_cut_reboot();
        test_power_cuts();
        test_write_failures();
        test_gc_write_failures();
        test_boot_read_failures();
        test_save_read_failures();
    } else {
        test_first_boot();
        test_set_get();
        test_revalidate_at_boot();
    test_with_defaults_oom();
        test_listeners_and_state();
        test_state_read_failures();
        test_certs_and_factory_reset();
        test_factory_reset_fails();
        test_edge_stream();
        test_previous_firmware();
        test_defaults_stored();
        test_reset_blocks_and_restarts();
        test_reset_read_back();
        test_reset_unmounted();
        test_edge_save_and_read();
        test_edge_too_large();
        test_pinning();
        test_pin_through_gc();
        test_no_manifest_keeps_records();
        test_lost_manifest();
        test_no_endless_loops();
        test_orphans();
        test_old_records();
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
