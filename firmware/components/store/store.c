#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fal_nor.h"
#include "flashdb.h"
#include "store_port.h"

#define MAX_CONFIGS 16
#define MAX_LISTENERS 12
#define MAX_PINS 4

// Sector sizes: a KV value must fit in one sector, so 16 KB holds an 8 KB edge record with room
// to spare and keeps FlashDB's garbage collection fine-grained in each 256 KB database.
#define CFG_SECTOR (16 * 1024)
#define EDGE_SECTOR (16 * 1024)
#define CERT_SECTOR (16 * 1024)
#define LOG_SECTOR (16 * 1024)

static struct fdb_kvdb s_cfg_db, s_edge_db, s_cert_db;
static struct fdb_tsdb s_log_db;
static bool s_mounted, s_log_ok;
// edge: mounted, and its committed generation known. False after a mount that failed, a manifest
// that does not read back, or a save whose commit is not known: no model is served and no record
// deleted until the next boot decides.
static bool s_edge_ok, s_cert_ok;
// Read-only until the next boot: a read failed past the FAL's retries (fal_nor.h), so the flash
// refuses writes and what FlashDB read may be wrong. Saves are refused, edge is unavailable.
static bool s_ro;
static fdb_time_t s_log_seq;  // TSDB key: last used sequence number
static uint32_t s_boot;
static unsigned s_log_failures;  // lines the log TSDB did not take
static cfg_env_t s_env;

typedef struct __attribute__((packed)) {
    int64_t wall_ms;
    uint32_t boot;
    uint32_t uptime_ms;
} log_hdr_t;

// The text cache: every small config's compact JSON, back to back in one region.
static char s_pool[STORE_CACHE_SIZE];
static size_t s_used;
static struct {
    const char *name;
    size_t off, len;
} s_cache[MAX_CONFIGS];
static size_t s_ncache;

// edgeDevices: the committed generation, and the generations GETs still read.
static uint32_t s_gen, s_count;  // s_gen 0: no model saved
static struct {
    uint32_t gen, count;
    int readers;
    bool doomed;  // superseded: deleted when the last reader unpins
} s_pins[MAX_PINS];
static char s_chunk[STORE_EDGE_RECORD];  // the record being filled by store_edge_save

static struct {
    store_listener_t fn;
    void *ctx;
} s_listeners[MAX_LISTENERS];
static size_t s_nlisteners;

#define LOCK() store_port_lock()
#define UNLOCK() store_port_unlock()
#define EDGE "edgeDevices"

// ---- FlashDB helpers -------------------------------------------------------------------------

static bool kvdb_open(fdb_kvdb_t db, const char *part, uint32_t sector)
{
    fdb_kvdb_control(db, FDB_KVDB_CTRL_SET_SEC_SIZE, &sector);
    fdb_err_t e = fdb_kvdb_init(db, part, part, NULL, NULL);
    if (e != FDB_NO_ERR)
        store_port_log("KVDB %s init failed (%d)", part, e);
    return e == FDB_NO_ERR;
}

/** A KV value in a fresh NUL-terminated heap buffer, or NULL when absent. */
static char *kv_read(fdb_kvdb_t db, const char *key, size_t *len_out)
{
    struct fdb_kv kv;
    if (!fdb_kv_get_obj(db, key, &kv))
        return NULL;
    char *buf = malloc(kv.value_len + 1);
    if (!buf)
        return NULL;
    struct fdb_blob blob;
    size_t n = fdb_blob_read((fdb_db_t)db, fdb_kv_to_blob(&kv, fdb_blob_make(&blob, buf, kv.value_len)));
    buf[n] = '\0';
    if (len_out)
        *len_out = n;
    return buf;
}

static bool kv_write(fdb_kvdb_t db, const char *key, const void *data, size_t len)
{
    struct fdb_blob blob;
    fdb_err_t e = fdb_kv_set_blob(db, key, fdb_blob_make(&blob, data, len));
    if (e != FDB_NO_ERR)
        store_port_log("writing %s (%u bytes) failed: FlashDB error %d", key, (unsigned)len, e);
    return e == FDB_NO_ERR;
}

// ---- the text cache --------------------------------------------------------------------------

static int cache_find(const char *name)
{
    for (size_t i = 0; i < s_ncache; i++)
        if (!strcmp(s_cache[i].name, name))
            return (int)i;
    return -1;
}

static bool cache_fits(const char *name, size_t len)
{
    int i = cache_find(name);
    return s_used - (i >= 0 ? s_cache[i].len : 0) + len <= sizeof s_pool;
}

/** Replaces (or adds) name's text; the caller checked cache_fits. */
static void cache_put(const char *name, const char *text, size_t len)
{
    int i = cache_find(name);
    if (i < 0) {
        i = (int)s_ncache++;
        s_cache[i].name = name;
    } else {  // close the gap of the old text
        size_t end = s_cache[i].off + s_cache[i].len;
        memmove(s_pool + s_cache[i].off, s_pool + end, s_used - end);
        for (size_t k = 0; k < s_ncache; k++)
            if (s_cache[k].off > s_cache[i].off)
                s_cache[k].off -= s_cache[i].len;
        s_used -= s_cache[i].len;
    }
    memcpy(s_pool + s_used, text, len);
    s_cache[i].off = s_used;
    s_cache[i].len = len;
    s_used += len;
}

/** Loads one small config from flash, re-validated, or its default. */
static void load(const char *name)
{
    char err[96] = "";
    cJSON *doc = NULL;
    char *raw = kv_read(&s_cfg_db, name, NULL);
    if (raw) {
        cJSON *parsed = cJSON_Parse(raw);
        // Re-validating fills keys added by newer firmware and drops removed ones.
        if (parsed)
            doc = cfg_load(&s_env, name, parsed, err, sizeof err);
        if (!doc)
            store_port_log("%s: stored copy unusable (%s), using defaults", name, err);
        cJSON_Delete(parsed);
        free(raw);
    }
    if (!doc)
        doc = cfg_default(&s_env, name);
    char *text = doc ? cJSON_PrintUnformatted(doc) : NULL;
    cJSON_Delete(doc);
    if (text && cache_fits(name, strlen(text)))
        cache_put(name, text, strlen(text));
    else
        store_port_log("%s: no memory for its text", name);
    cJSON_free(text);
}

static void load_all(void)
{
    s_used = s_ncache = 0;
    const char *names[MAX_CONFIGS];
    size_t n = cfg_names(&s_env, names, MAX_CONFIGS);
    for (size_t i = 0; i < n; i++)
        if (strcmp(names[i], EDGE))
            load(names[i]);
}

// ---- edgeDevices records -----------------------------------------------------------------------

static void record_key(char *key, size_t size, uint32_t gen, uint32_t i)
{
    snprintf(key, size, "ed.%lu.%lu", (unsigned long)gen, (unsigned long)i);
}

/** The generation of an "ed.<g>.<i>" key, or 0 when the name is not one. */
static uint32_t key_gen(const char *name)
{
    unsigned long g, i;
    char tail;
    return sscanf(name, "ed.%lu.%lu%c", &g, &i, &tail) == 2 ? (uint32_t)g : 0;
}

static int pin_find(uint32_t gen);

/** A record of generation g is kept: the committed one, or one a GET still reads. */
static bool kept(uint32_t g) { return g == s_gen || pin_find(g) >= 0; }

/** Deletes every record whose generation is `gen`, or (unkept) every record kept() does not keep.
 * At most 8 passes of 16: more records than the database can hold, so a flash whose deletes do
 * not take (an erase-protected or failing part) cannot keep it here, holding the lock. */
static void delete_records(uint32_t gen, bool unkept)
{
    for (int pass = 0; pass < 8; pass++) {  // collect, then delete: never delete under the iterator
        char doomed[16][FDB_KV_NAME_MAX];
        int n = 0;
        struct fdb_kv_iterator it;
        fdb_kv_iterator_init(&s_edge_db, &it);
        while (n < 16 && fdb_kv_iterate(&s_edge_db, &it)) {
            if (strncmp(it.curr_kv.name, "ed.", 3))
                continue;
            uint32_t g = key_gen(it.curr_kv.name);
            if (unkept ? !kept(g) : g == gen)
                snprintf(doomed[n++], FDB_KV_NAME_MAX, "%s", it.curr_kv.name);
        }
        for (int i = 0; i < n; i++)
            fdb_kv_del(&s_edge_db, doomed[i]);
        if (n < 16)
            return;
    }
    store_port_log("edge records left after 8 cleanup passes: the flash does not delete");
}

/** The highest generation of any record in flash (0: none). */
static uint32_t highest_record_gen(void)
{
    uint32_t top = 0;
    struct fdb_kv_iterator it;
    fdb_kv_iterator_init(&s_edge_db, &it);
    while (fdb_kv_iterate(&s_edge_db, &it)) {
        uint32_t g = key_gen(it.curr_kv.name);
        if (g > top)
            top = g;
    }
    return top;
}

static bool has_records(void)
{
    struct fdb_kv_iterator it;
    fdb_kv_iterator_init(&s_edge_db, &it);
    while (fdb_kv_iterate(&s_edge_db, &it))
        if (key_gen(it.curr_kv.name))
            return true;
    return false;
}

typedef enum { MANIFEST_NONE, MANIFEST_OK, MANIFEST_BAD } manifest_t;

/** The manifest in flash: absent, or the generation and count it names, or present but not
 * readable as one. A fixed buffer: no allocation can fail here. */
static manifest_t manifest_read(uint32_t *gen, uint32_t *count)
{
    struct fdb_kv kv;
    *gen = *count = 0;
    if (!fdb_kv_get_obj(&s_edge_db, EDGE, &kv))
        return MANIFEST_NONE;
    char m[32] = "";
    struct fdb_blob blob;
    size_t n = kv.value_len < sizeof m ? fdb_blob_read((fdb_db_t)&s_edge_db, fdb_kv_to_blob(&kv, fdb_blob_make(&blob, m, kv.value_len))) : 0;
    m[n < sizeof m ? n : sizeof m - 1] = '\0';
    unsigned long g, c;
    if (sscanf(m, "@%lu,%lu", &g, &c) != 2 || !g || !c)
        return MANIFEST_BAD;
    *gen = (uint32_t)g;
    *count = (uint32_t)c;
    return MANIFEST_OK;
}

/** Mounts the edge database and takes the committed generation from it; false: unknown. */
static bool edge_mount(manifest_t *state)
{
    s_gen = s_count = 0;
    *state = MANIFEST_BAD;
    if (!kvdb_open(&s_edge_db, "edge", EDGE_SECTOR))
        return false;
    *state = manifest_read(&s_gen, &s_count);
    return *state != MANIFEST_BAD;
}

static void edge_unknown(const char *why)
{
    s_edge_ok = false;
    store_port_log("edgeDevices unavailable until the next boot: %s", why);
}

/** True once a read has failed for good: the store stays read-only until the next boot. */
static bool read_only(void)
{
    if (!s_ro && fal_nor_read_failed()) {
        s_ro = true;
        store_port_log("a flash read failed: storage read-only until the next boot");
        if (s_edge_ok)
            edge_unknown("a flash read failed");
    }
    return s_ro;
}

/** Edge serves and saves only when its committed generation is known and nothing has misread. */
static bool edge_usable(void) { return !read_only() && s_edge_ok; }

static int pin_find(uint32_t gen)
{
    for (int i = 0; i < MAX_PINS; i++)
        if (s_pins[i].readers && s_pins[i].gen == gen)
            return i;
    return -1;
}

typedef struct {
    uint32_t gen, idx;
    size_t fill;
    bool failed;
} edge_writer_t;

static bool flush(edge_writer_t *w)
{
    char key[FDB_KV_NAME_MAX];
    record_key(key, sizeof key, w->gen, w->idx);
    if (!kv_write(&s_edge_db, key, s_chunk, w->fill)) {
        w->failed = true;
        return false;
    }
    w->idx++;
    w->fill = 0;
    return true;
}

static bool emit(void *ctx, const char *text, size_t len)
{
    edge_writer_t *w = ctx;
    while (len) {
        size_t n = sizeof s_chunk - w->fill < len ? sizeof s_chunk - w->fill : len;
        memcpy(s_chunk + w->fill, text, n);
        w->fill += n;
        text += n;
        len -= n;
        if (w->fill == sizeof s_chunk && !flush(w))
            return false;
    }
    return true;
}

// ---- init ------------------------------------------------------------------------------------

/** TSDB "time": a sequence number, so keys always grow across reboots. */
static fdb_time_t next_seq(void) { return ++s_log_seq; }

store_err_t store_init(const cfg_env_t *env, nor_t *flash)
{
    s_env = *env;
    fal_nor_set(flash);
    fal_nor_rearm();  // a new boot: the flash is writable until a read fails
    LOCK();
    if (!kvdb_open(&s_cfg_db, "cfg", CFG_SECTOR)) {
        UNLOCK();
        return STORE_EFLASH;  // no configs, no boot counter: the caller runs on defaults
    }
    s_mounted = true;
    s_cert_ok = kvdb_open(&s_cert_db, "cert", CERT_SECTOR);
    uint32_t sec = LOG_SECTOR;
    fdb_tsdb_control(&s_log_db, FDB_TSDB_CTRL_SET_SEC_SIZE, &sec);
    s_log_seq = 0;
    s_log_ok = fdb_tsdb_init(&s_log_db, "log", "log", next_seq, sizeof(log_hdr_t) + STORE_LOG_LINE_MAX, NULL) == FDB_NO_ERR;
    if (s_log_ok) {
        bool rollover = true;  // overwrite the oldest lines when full
        fdb_tsdb_control(&s_log_db, FDB_TSDB_CTRL_SET_ROLLOVER, &rollover);
        fdb_tsdb_control(&s_log_db, FDB_TSDB_CTRL_GET_LAST_TIME, &s_log_seq);  // continue the sequence
    } else {
        store_port_log("log TSDB init failed; persistent log disabled");
    }
    // One small write per boot: gives log lines a boot number when the clock is unset (spec §9.1).
    s_boot = 0;
    store_state_get("boot", &s_boot, sizeof s_boot);
    s_boot++;
    if (!read_only())
        store_state_set("boot", &s_boot, sizeof s_boot);

    manifest_t m;
    s_edge_ok = edge_mount(&m);
    if (!s_edge_ok)
        edge_unknown(m == MANIFEST_BAD ? "the manifest does not read back" : "the database does not mount");
    // Orphans of a save cut by a power loss (spec §9.4, step 4). Without a manifest nothing proves
    // a record is an orphan, so none is deleted; the next successful save removes them.
    else if (s_gen)
        delete_records(0, true);
    else if (has_records())
        store_port_log("edge records without a manifest: kept until the next save");
    load_all();
    read_only();  // a read that failed anywhere in the mount or the loads
    UNLOCK();
    store_port_log("%u configs loaded, edge generation %lu, boot #%lu", (unsigned)s_ncache, (unsigned long)s_gen,
                   (unsigned long)s_boot);
    return STORE_OK;
}

void store_deinit(void)
{
    LOCK();
    if (s_mounted) {
        fdb_kvdb_deinit(&s_cfg_db);
        fdb_kvdb_deinit(&s_edge_db);  // FlashDB ignores a database that is not mounted
        fdb_kvdb_deinit(&s_cert_db);
        if (s_log_ok)
            fdb_tsdb_deinit(&s_log_db);
    }
    memset(&s_cfg_db, 0, sizeof s_cfg_db);
    memset(&s_edge_db, 0, sizeof s_edge_db);
    memset(&s_cert_db, 0, sizeof s_cert_db);
    memset(&s_log_db, 0, sizeof s_log_db);
    s_mounted = s_log_ok = s_edge_ok = s_cert_ok = s_ro = false;
    fal_nor_rearm();
    s_used = s_ncache = s_nlisteners = 0;
    s_gen = s_count = 0;
    memset(s_pins, 0, sizeof s_pins);
    UNLOCK();
}

const cfg_env_t *store_env(void) { return &s_env; }

bool store_known(const char *name) { return name && cfg_known(&s_env, name); }

// ---- small configs ---------------------------------------------------------------------------

char *store_get_json(const char *name)
{
    if (!name)
        return NULL;
    LOCK();
    int i = cache_find(name);
    char *s = i >= 0 ? malloc(s_cache[i].len + 1) : NULL;
    if (s) {
        memcpy(s, s_pool + s_cache[i].off, s_cache[i].len);
        s[s_cache[i].len] = '\0';
    }
    UNLOCK();
    return s;
}

cJSON *store_get(const char *name)
{
    if (!name)
        return NULL;
    LOCK();
    int i = cache_find(name);
    cJSON *doc = i >= 0 ? cJSON_ParseWithLength(s_pool + s_cache[i].off, s_cache[i].len) : NULL;
    UNLOCK();
    return doc;
}

void store_subscribe(store_listener_t fn, void *ctx)
{
    if (s_nlisteners < MAX_LISTENERS) {
        s_listeners[s_nlisteners].fn = fn;
        s_listeners[s_nlisteners].ctx = ctx;
        s_nlisteners++;
    }
}

static void notify(const char *name)
{
    for (size_t i = 0; i < s_nlisteners; i++)
        s_listeners[i].fn(name, s_listeners[i].ctx);
}

static store_err_t from_cfg(cfg_result_t r, char *err, size_t errlen)
{
    switch (r) {
    case CFG_OK:
        return STORE_OK;
    case CFG_INVALID:
        return STORE_EINVALID;
    case CFG_TOO_LARGE:
        return STORE_ETOOLARGE;
    case CFG_ABORTED:
        snprintf(err, errlen, "The settings could not be written to flash.");
        return STORE_EFLASH;
    default:
        snprintf(err, errlen, "The gateway does not have enough free memory to check these settings.");
        return STORE_ENOMEM;
    }
}

store_err_t store_set_text(const char *name, const char *text, size_t len, char *err, size_t errlen)
{
    err[0] = '\0';
    if (!store_known(name) || !strcmp(name, EDGE)) {
        snprintf(err, errlen, "Unknown settings \"%s\".", name ? name : "");
        return STORE_ENOTFOUND;
    }
    char *out = cJSON_malloc(CFG_SMALL_BUF);  // the caller's allocator: its arena scope, or the heap
    if (!out)
        return from_cfg(CFG_NO_MEMORY, err, errlen);
    LOCK();
    if (read_only()) {
        UNLOCK();
        cJSON_free(out);
        snprintf(err, errlen, "The settings storage is read-only after a flash error; restart the gateway.");
        return STORE_EFLASH;
    }
    int i = cache_find(name);
    cJSON *cur = i >= 0 ? cJSON_ParseWithLength(s_pool + s_cache[i].off, s_cache[i].len) : NULL;
    store_err_t e = cur ? from_cfg(cfg_validate_text(&s_env, name, text, len, cur, out, CFG_SMALL_BUF, err, errlen), err, errlen)
                        : from_cfg(CFG_NO_MEMORY, err, errlen);
    cJSON_Delete(cur);
    size_t n = e == STORE_OK ? strlen(out) : 0;
    if (e == STORE_OK && !cache_fits(name, n)) {
        snprintf(err, errlen, "The settings do not fit in the gateway's memory.");
        e = STORE_ETOOLARGE;
    }
    if (e == STORE_OK && !kv_write(&s_cfg_db, name, out, n)) {
        snprintf(err, errlen, "The settings could not be written to flash.");
        e = STORE_EFLASH;
    }
    if (e == STORE_OK)
        cache_put(name, out, n);
    UNLOCK();
    cJSON_free(out);
    if (e == STORE_OK) {
        store_port_log("%s saved", name);
        notify(name);
    }
    return e;
}

// ---- edgeDevices -------------------------------------------------------------------------------

store_err_t store_edge_save(const char *text, size_t len, char *err, size_t errlen)
{
    err[0] = '\0';
    LOCK();
    if (!edge_usable()) {
        UNLOCK();
        snprintf(err, errlen, "The Edge model storage is unavailable; restart the gateway.");
        return STORE_EFLASH;
    }
    // Above the committed generation and above every record in flash, so this save never has to
    // delete anything before it is validated (records of a lost or unreadable manifest survive it).
    uint32_t top = highest_record_gen();
    edge_writer_t w = {.gen = (top > s_gen ? top : s_gen) + 1};
    cfg_result_t r = cfg_edge_text(&s_env, text, len, true, CFG_EDGE_MAX, emit, &w, err, errlen);
    if (r == CFG_OK && w.fill && !flush(&w))
        r = CFG_ABORTED;
    char manifest[32];
    snprintf(manifest, sizeof manifest, "@%lu,%lu", (unsigned long)w.gen, (unsigned long)w.idx);
    uint32_t old = s_gen;
    bool wrote = r == CFG_ABORTED;  // a record write failed
    if (r == CFG_OK && !kv_write(&s_edge_db, EDGE, manifest, strlen(manifest))) {  // the commit point
        r = CFG_ABORTED;
        wrote = true;
    }
    if (wrote) {
        // After a failed write FlashDB's view in RAM (its cache) need not be the flash's: the new
        // manifest may be in place with the old one not deleted, or cached while it is not
        // valid. Mount the database again, as the next boot will, and take the committed
        // generation from there: what this boot serves is then what the next one finds.
        fdb_kvdb_deinit(&s_edge_db);
        memset(&s_edge_db, 0, sizeof s_edge_db);
        manifest_t m;
        bool known = edge_mount(&m);
        if (known && m == MANIFEST_OK && s_gen == w.gen && s_count == w.idx) {
            r = CFG_OK;  // the commit is in place after all
            store_port_log("edgeDevices: a write reported an error, yet generation %lu is committed", (unsigned long)w.gen);
        } else if (known && ((m == MANIFEST_OK && s_gen == old) || (m == MANIFEST_NONE && !old))) {
            delete_records(w.gen, false);  // the old state stands: this generation's records go
        } else {
            // A mount that failed, a manifest that does not read back, or another generation than
            // either: nothing proves what is committed. Delete nothing; the next boot decides.
            edge_unknown("a save's commit is not known");
        }
    }
    store_err_t e = from_cfg(r, err, errlen);
    if (e != STORE_OK) {
        if (!wrote)  // rejected before any write: only this generation's own records, if any
            delete_records(w.gen, false);
        UNLOCK();
        return e;
    }
    s_gen = w.gen;
    s_count = w.idx;
    int p = old ? pin_find(old) : -1;
    if (p >= 0)
        s_pins[p].doomed = true;  // a GET still reads it: deleted at its last unpin
    delete_records(0, true);      // the old generation unless pinned, and any stray record
    read_only();
    UNLOCK();
    store_port_log("edgeDevices saved: generation %lu, %lu records", (unsigned long)w.gen, (unsigned long)w.idx);
    notify(EDGE);
    return STORE_OK;
}

void store_edge_pin(store_edge_pin_t *p)
{
    LOCK();
    p->gen = s_gen;
    p->count = s_gen ? s_count : 1;
    if (!edge_usable()) {  // nothing is known to be committed: no model, not even the default
        p->gen = 0;
        p->count = 0;
        UNLOCK();
        return;
    }
    if (s_gen) {
        int i = pin_find(s_gen);
        for (int k = 0; i < 0 && k < MAX_PINS; k++)
            if (!s_pins[k].readers) {
                i = k;
                s_pins[k].gen = s_gen;
                s_pins[k].count = s_count;
                s_pins[k].doomed = false;
            }
        if (i >= 0)
            s_pins[i].readers++;
        else
            p->count = 0;  // MAX_PINS generations in use: the caller answers busy
    }
    UNLOCK();
}

void store_edge_unpin(const store_edge_pin_t *p)
{
    if (!p->gen || !p->count)
        return;
    LOCK();
    int i = pin_find(p->gen);
    if (i >= 0 && --s_pins[i].readers == 0 && s_pins[i].doomed)
        delete_records(p->gen, false);  // its slot is free now, so kept() no longer holds it
    UNLOCK();
}

static size_t default_edge(char *buf, size_t cap)
{
    cJSON *d = cfg_default(&s_env, EDGE);
    char *s = d ? cJSON_PrintUnformatted(d) : NULL;
    cJSON_Delete(d);
    size_t n = s ? strlen(s) : 0;
    if (n >= cap)
        n = 0;
    if (n)
        memcpy(buf, s, n + 1);
    cJSON_free(s);
    return n;
}

size_t store_edge_record(const store_edge_pin_t *p, uint32_t i, char *buf, size_t cap)
{
    if (i >= p->count)
        return 0;
    if (!p->gen)
        return default_edge(buf, cap);
    char key[FDB_KV_NAME_MAX];
    record_key(key, sizeof key, p->gen, i);
    struct fdb_kv kv;
    struct fdb_blob blob;
    LOCK();
    size_t n = 0;
    if (fdb_kv_get_obj(&s_edge_db, key, &kv) && kv.value_len <= cap)
        n = fdb_blob_read((fdb_db_t)&s_edge_db, fdb_kv_to_blob(&kv, fdb_blob_make(&blob, buf, kv.value_len)));
    if (read_only())
        n = 0;  // what was read may be wrong
    UNLOCK();
    return n;
}

size_t store_edge_text(char *buf, size_t cap)
{
    store_edge_pin_t p;
    store_edge_pin(&p);
    size_t total = 0;
    for (uint32_t i = 0; i < p.count; i++) {
        if (cap - total <= STORE_EDGE_RECORD && i + 1 < p.count) {
            total = 0;  // cannot hold the next full record and a NUL
            break;
        }
        size_t n = store_edge_record(&p, i, buf + total, cap - total - 1);
        if (!n) {
            total = 0;
            break;
        }
        total += n;
    }
    store_edge_unpin(&p);
    if (total)
        buf[total] = '\0';
    return total;
}

// ---- factory reset, state, certificates ---------------------------------------------------------

store_err_t store_factory_reset(void)
{
    LOCK();
    if (read_only()) {
        UNLOCK();
        return STORE_EFLASH;
    }
    // Each database on its own. The configs are reloaded below from the flash as it now is, and
    // certificates are always read from it; only edge keeps its generation in RAM, so a failed
    // edge reset is mounted again: what this boot serves is what the next boot finds.
    bool cfg = fdb_kv_set_default(&s_cfg_db) == FDB_NO_ERR;
    bool edge = s_edge_ok && fdb_kv_set_default(&s_edge_db) == FDB_NO_ERR;
    bool cert = s_cert_ok && fdb_kv_set_default(&s_cert_db) == FDB_NO_ERR;
    if (edge) {
        s_gen = s_count = 0;
        memset(s_pins, 0, sizeof s_pins);
    } else if (s_edge_ok) {
        fdb_kvdb_deinit(&s_edge_db);
        memset(&s_edge_db, 0, sizeof s_edge_db);
        manifest_t m;
        memset(s_pins, 0, sizeof s_pins);
        if (!edge_mount(&m))
            edge_unknown("a factory reset failed");
    }
    if (!s_ro)
        store_state_set("boot", &s_boot, sizeof s_boot);  // the boot numbering of the log goes on
    load_all();  // from the flash as it now is
    read_only();
    UNLOCK();
    store_err_t e = cfg && edge && cert ? STORE_OK : STORE_EFLASH;
    store_port_log(e == STORE_OK ? "factory settings restored" : "factory reset incomplete");
    return e;
}

store_err_t store_state_set(const char *key, const void *data, size_t len)
{
    char k[FDB_KV_NAME_MAX];
    snprintf(k, sizeof k, "st.%s", key);
    LOCK();
    bool ok = !read_only() && kv_write(&s_cfg_db, k, data, len);
    UNLOCK();
    return ok ? STORE_OK : STORE_EFLASH;
}

size_t store_state_get(const char *key, void *data, size_t len)
{
    char k[FDB_KV_NAME_MAX];
    snprintf(k, sizeof k, "st.%s", key);
    struct fdb_blob blob;
    LOCK();
    size_t n = fdb_kv_get_blob(&s_cfg_db, k, fdb_blob_make(&blob, data, len));
    if (read_only())
        n = 0;  // what was read may be wrong: no state rather than a wrong one
    UNLOCK();
    return n;
}

static bool cert_key(const char *scope, const char *kind, char *out, size_t len)
{
    static const char *const scopes[] = {"mqtt", "port1", "port2"};  // port2 has TLS on the STM32 (spec §10.2)
    static const char *const kinds[] = {"ca", "cert", "key"};
    bool sok = false, kok = false;
    for (size_t i = 0; i < 3; i++)
        sok |= scope && strcmp(scope, scopes[i]) == 0;
    for (size_t i = 0; i < 3; i++)
        kok |= kind && strcmp(kind, kinds[i]) == 0;
    if (!sok || !kok)
        return false;
    snprintf(out, len, "%s.%s", scope, kind);
    return true;
}

store_err_t store_cert_set(const char *scope, const char *kind, const void *pem, size_t len)
{
    char key[24];
    if (!cert_key(scope, kind, key, sizeof key) || len == 0 || len > STORE_CERT_MAX)
        return STORE_EINVALID;
    if (!s_cert_ok)
        return STORE_EFLASH;
    LOCK();
    bool ok = !read_only() && kv_write(&s_cert_db, key, pem, len);
    UNLOCK();
    if (ok)
        notify(scope);  // the owning module reconnects with new material
    return ok ? STORE_OK : STORE_EFLASH;
}

char *store_cert_get(const char *scope, const char *kind)
{
    char key[24];
    if (!cert_key(scope, kind, key, sizeof key) || !s_cert_ok)
        return NULL;
    LOCK();
    char *pem = kv_read(&s_cert_db, key, NULL);
    if (pem && read_only()) {  // what was read may be wrong
        free(pem);
        pem = NULL;
    }
    UNLOCK();
    return pem;
}

// ---- log -------------------------------------------------------------------------------------

uint32_t store_boot_count(void) { return s_boot; }
unsigned store_log_failures(void) { return s_log_failures; }
bool store_log_ok(void) { return s_log_ok; }

void store_log_append(const char *line, size_t len)
{
    if (!s_log_ok || len == 0)
        return;
    if (len > STORE_LOG_LINE_MAX)
        len = STORE_LOG_LINE_MAX;
    uint8_t rec[sizeof(log_hdr_t) + STORE_LOG_LINE_MAX];
    log_hdr_t h = {.wall_ms = store_port_wall_ms(), .boot = s_boot, .uptime_ms = store_port_uptime_ms()};
    memcpy(rec, &h, sizeof h);
    memcpy(rec + sizeof h, line, len);
    struct fdb_blob blob;
    LOCK();
    if (read_only() || fdb_tsl_append(&s_log_db, fdb_blob_make(&blob, rec, sizeof h + len)) != FDB_NO_ERR)
        s_log_failures++;
    UNLOCK();
}

// The log is read in batches: the lock is held only while a batch is copied out of flash, never
// while the caller sends it (an HTTP client can be slow, and the same lock guards every save).
#define LOG_BATCH 32

typedef struct {
    store_log_meta_t meta;
    uint16_t len;
    char text[STORE_LOG_LINE_MAX + 1];
} log_line_t;

typedef struct {
    log_line_t lines[LOG_BATCH];
    int n;
    fdb_time_t last;  // sequence number of the last record copied
    uint8_t rec[sizeof(log_hdr_t) + STORE_LOG_LINE_MAX + 1];
} log_batch_t;

static bool batch_cb(fdb_tsl_t tsl, void *arg)
{
    log_batch_t *b = arg;
    b->last = tsl->time;
    struct fdb_blob blob;
    size_t n = tsl->log_len > sizeof(log_hdr_t) + STORE_LOG_LINE_MAX ? sizeof(log_hdr_t) + STORE_LOG_LINE_MAX : tsl->log_len;
    n = fdb_blob_read((fdb_db_t)&s_log_db, fdb_tsl_to_blob(tsl, fdb_blob_make(&blob, b->rec, n)));
    if (n < sizeof(log_hdr_t))
        return false;  // not a record of this format; skip it
    log_hdr_t h;
    memcpy(&h, b->rec, sizeof h);
    log_line_t *l = &b->lines[b->n++];
    l->meta = (store_log_meta_t){.wall_ms = h.wall_ms, .boot = h.boot, .uptime_ms = h.uptime_ms};
    l->len = (uint16_t)(n - sizeof h);
    memcpy(l->text, b->rec + sizeof h, l->len);
    l->text[l->len] = '\0';
    return b->n == LOG_BATCH;  // FlashDB stops when the callback returns true
}

void store_log_iter(store_log_fn fn, void *ctx)
{
    if (!s_log_ok)
        return;
    log_batch_t *b = malloc(sizeof *b);
    if (!b)
        return;
    fdb_time_t from = 0;  // sequence numbers start at 1
    for (;;) {
        b->n = 0;
        LOCK();
        fdb_time_t to = s_log_seq;
        if (from <= to)
            fdb_tsl_iter_by_time(&s_log_db, from, to, batch_cb, b);
        UNLOCK();
        bool more = b->n == LOG_BATCH;
        for (int i = 0; i < b->n; i++) {
            if (!fn(&b->lines[i].meta, b->lines[i].text, b->lines[i].len, ctx)) {
                more = false;
                break;
            }
        }
        if (!more)
            break;
        from = b->last + 1;
    }
    free(b);
}
