// Persistent storage on FlashDB over the SPI4 flash (spec §5.2, §5.5, §9.1, §9.4), ported from
// the ESP32 store. Small configs are kept in RAM as compact JSON text (not as cJSON trees);
// edgeDevices is never cached: it lives in 8 KB records "ed.<g>.<i>" of generation g, committed
// by the manifest "edgeDevices" = "@<g>,<count>". Plain C: the platform supplies store_port.h.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "cfgmodel.h"
#include "nor.h"

typedef enum {
    STORE_OK = 0,
    STORE_ENOTFOUND,  // 404: unknown settings name
    STORE_EINVALID,   // 400: the document breaks the schema or is not JSON (err says why)
    STORE_ETOOLARGE,  // 413: the normalised text exceeds its limit, or the text cache is full
    STORE_ENOMEM,     // 500: an allocation failed
    STORE_EFLASH,     // 500: FlashDB could not write
} store_err_t;

#define STORE_EDGE_RECORD 8192u  // bytes of normalised text per edgeDevices record
#ifndef STORE_CACHE_SIZE
#define STORE_CACHE_SIZE 20480u  // RAM for the compact text of every small config (spec §5.3)
#endif

/**
 * Mounts the four databases on `flash` (FlashDB's 1 MB at offset 0, spec §5.2), deletes edge
 * records of no committed generation, bumps the boot counter, and loads every small config,
 * re-validated (keys added by newer firmware filled in), falling back to its default.
 */
store_err_t store_init(const cfg_env_t *env, nor_t *flash);  // STORE_EFLASH: no cfg, or read-only (defaults)
/** Unmounts (host tests: a power cut, then store_init again on the same flash). */
void store_deinit(void);

const cfg_env_t *store_env(void);
bool store_known(const char *name);

/** Compact text of a small config, a heap copy the caller frees; NULL for edgeDevices or unknown names. */
char *store_get_json(const char *name);
/** The same parsed, with the allocator of the calling context (arena scope or heap). */
cJSON *store_get(const char *name);
/** Validates `text` (cfg_validate_text), writes it, updates the cache, notifies. Not for edgeDevices. */
store_err_t store_set_text(const char *name, const char *text, size_t len, char *err, size_t errlen);

/**
 * edgeDevices from JSON text: validated node by node (cfg_edge_text, strict), written to the
 * records of generation g = current + 1, committed by the manifest, then the old generation is
 * deleted, or kept until the last GET reading it unpins it. On any error the committed model
 * stays as it was.
 */
store_err_t store_edge_save(const char *text, size_t len, char *err, size_t errlen);

/** A GET's hold on one generation (spec §9.4): its records stay until store_edge_unpin. */
typedef struct {
    uint32_t gen;    // 0: no model saved yet; record 0 is then the default text
    uint32_t count;  // records
} store_edge_pin_t;

void store_edge_pin(store_edge_pin_t *p);
/** Record i of a pinned generation into buf (cap >= STORE_EDGE_RECORD); its length, 0 on error. */
size_t store_edge_record(const store_edge_pin_t *p, uint32_t i, char *buf, size_t cap);
void store_edge_unpin(const store_edge_pin_t *p);
/** The whole committed text into buf (NUL-terminated, cap > CFG_EDGE_MAX), for the boot; 0 on error. */
size_t store_edge_text(char *buf, size_t cap);

/** Called after a config changed, from the saving task; keep it short. */
typedef void (*store_listener_t)(const char *name, void *ctx);
void store_subscribe(store_listener_t fn, void *ctx);

/** Restores factory settings: configs, edgeDevices and certificates; the log is kept. Every write is
 * refused from its start, the defaults are read back, and the gateway restarts (store_port_restart),
 * whatever the outcome; STORE_EFLASH when a database could not be reset (Plan 09b ruling 2). */
store_err_t store_factory_reset(void);

// ---- Small runtime state kept across restarts (DO outputs, the boot counter) ----
store_err_t store_state_set(const char *key, const void *data, size_t len);
/** Copies up to `len` bytes; returns the stored length, or 0 when absent. */
size_t store_state_get(const char *key, void *data, size_t len);

// ---- TLS material: scope "mqtt" | "port1"; kind "ca" | "cert" | "key" ----
#define STORE_CERT_MAX 12288
store_err_t store_cert_set(const char *scope, const char *kind, const void *pem, size_t len);
/** NUL-terminated PEM (heap, caller frees), or NULL when none was uploaded. */
char *store_cert_get(const char *scope, const char *kind);

// ---- Persistent log: TSDB keyed by a sequence number that only grows ----
#define STORE_LOG_LINE_MAX 200

typedef struct {
    int64_t wall_ms;     // Unix time in ms, or 0 when the clock was not set
    uint32_t boot;       // boot counter, from 1
    uint32_t uptime_ms;  // since that boot
} store_log_meta_t;

void store_log_append(const char *line, size_t len);
/** Calls fn for each stored line, oldest first; return false to stop. The lock is not held during fn. */
typedef bool (*store_log_fn)(const store_log_meta_t *meta, const char *line, size_t len, void *ctx);
void store_log_iter(store_log_fn fn, void *ctx);
uint32_t store_boot_count(void);
unsigned store_log_failures(void);  // appends refused (read-only, or a FlashDB error)
bool store_log_ok(void);            // the log TSDB is mounted
