// Configuration model shared by the web UI and the firmware.
//
// Each config document (`/api/config/<name>`) has a schema mirroring the web
// UI's FieldDefs (src/pages/*.tsx) and a default value. This module is plain
// C over cJSON with no ESP-IDF dependency, so it is unit-tested on the host
// (firmware/test/host). Keep it in step with the web field definitions.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Runtime facts the schema depends on (from the board). */
typedef struct {
    int uart_count;  /**< user serial ports: 1 or 2 */
    int do_count;
    int di_count;
    int ai_count;
} cfg_env_t;

/** True when `name` is a config this firmware stores. */
bool cfg_known(const cfg_env_t *env, const char *name);

/** Lists every config name, for factory reset and boot-time loading. */
size_t cfg_names(const cfg_env_t *env, const char **out, size_t max);

/** New default document for `name` (caller deletes), or NULL if unknown. */
cJSON *cfg_default(const cfg_env_t *env, const char *name);
/** The same default as its compact text, static for the whole run (FlashDB's default KV table, Plan 09b ruling 1). */
const char *cfg_default_text(const cfg_env_t *env, const char *name);
/** True when the config is a JSON array (topics, events, edgeDevices), false for an object. */
bool cfg_is_list(const cfg_env_t *env, const char *name);
/**
 * The listen ports across configs (Plan 08b T0): a port whose bridge is a TCP server (workMode 3) may not
 * listen where the other port's TCP server does, nor on system.webPort or system.websocketPort. Any of
 * the three may be NULL (not configured). False with a message for the user when two clash.
 */
bool cfg_listen_ok(const cJSON *port1, const cJSON *port2, const cJSON *system, char *err, size_t errlen);

/**
 * Validates `in` against the schema of `name` and returns a normalised copy:
 * missing keys come from `current` (or the defaults), unknown keys are
 * dropped, booleans/numbers sent as strings are coerced. On error returns NULL
 * and writes a user-facing message (English, one sentence) into `err`.
 */
cJSON *cfg_validate(const cfg_env_t *env, const char *name, const cJSON *in, const cJSON *current, char *err,
                    size_t errlen);

/**
 * cfg_validate for a copy read back from flash: the save-time rules that a
 * model stored by older firmware might break (Edge address, span and mapping
 * checks) are not applied, so an upgrade never discards a working model.
 */
cJSON *cfg_load(const cfg_env_t *env, const char *name, const cJSON *in, char *err, size_t errlen);

// ---- Text in, text out (STM32: bounded memory, spec §9.3-9.4) -------------------------

typedef enum {
    CFG_OK = 0,
    CFG_INVALID,    /**< the document breaks the schema or is not JSON: `err` says why (HTTP 400) */
    CFG_TOO_LARGE,  /**< the normalised text would exceed `max_out` (HTTP 413) */
    CFG_NO_MEMORY,  /**< an allocation failed (HTTP 500) */
    CFG_ABORTED,    /**< the emit callback returned false, e.g. a flash write failed */
} cfg_result_t;

/** Exact limits of the accepted normalised text, in bytes (spec §9.3, §9.4). */
#define CFG_SMALL_MAX 10240
#define CFG_EDGE_MAX 96000  // the edgeDevices body, 6 records (Plan 09d)
/** cJSON_PrintPreallocated needs 5 bytes beyond the text (cJSON.h); size buffers for cfg_validate_text with this. */
#define CFG_SMALL_BUF (CFG_SMALL_MAX + 6)

/**
 * Allocations through the cJSON hooks that returned NULL so far. The JSON allocator
 * (components/arena) provides it; a weak default here returns 0. cJSON and the
 * tree-based validator ignore some failed allocations (a field silently keeps its
 * default), so the text entry points below compare this count before and after,
 * and any failure turns the result into CFG_NO_MEMORY.
 */
unsigned json_alloc_failures(void);

/**
 * One config other than edgeDevices, from JSON text to normalised JSON text in
 * `out` (NUL-terminated; a buffer of CFG_SMALL_BUF bytes). Text longer than
 * CFG_SMALL_MAX bytes is CFG_TOO_LARGE.
 * Same rules and messages as cfg_validate. The input tree is freed before the
 * output is printed, and the output is printed into `out` without reallocation,
 * which keeps the arena peak low.
 */
cfg_result_t cfg_validate_text(const cfg_env_t *env, const char *name, const char *text, size_t len,
                               const cJSON *current, char *out, size_t out_size, char *err, size_t errlen);

/** Receives the normalised edgeDevices text in order; returns false to stop. */
typedef bool (*cfg_emit_fn)(void *ctx, const char *text, size_t len);

/**
 * edgeDevices from JSON text, one device header and one node at a time, never
 * building the whole tree. With `strict` it applies cfg_validate's save-time
 * rules, without them cfg_load's. For any valid JSON the emitted text and the
 * first error message are byte-identical to cfg_validate / cfg_load followed
 * by cJSON_PrintUnformatted. Emitted text is only meaningful when the result
 * is CFG_OK: a caller writes it to a new generation and commits it only then.
 */
cfg_result_t cfg_edge_text(const cfg_env_t *env, const char *text, size_t len, bool strict, size_t max_out,
                           cfg_emit_fn emit, void *ctx, char *err, size_t errlen);

/** cfg_edge_load's visitors: a normalised device header (without its nodes) and each normalised node,
 * with the device's index. Copy what is needed; false stops the walk (CFG_NO_MEMORY). */
typedef bool (*cfg_edge_dev_fn)(void *ctx, const cJSON *dev, int index);
typedef bool (*cfg_edge_node_fn)(void *ctx, const cJSON *node, int dev);

/**
 * edgeDevices as the boot loads it (Plan 08c, spec §9.4): cfg_load's rules (phases 1 and 2, lenient),
 * one header and one node at a time through the visitors, the same walk as cfg_edge_text (which it
 * leaves unchanged). Nothing is emitted. A failed allocation is CFG_NO_MEMORY, never a partial visit
 * reported as CFG_OK.
 */
cfg_result_t cfg_edge_load(const cfg_env_t *env, const char *text, size_t len, cfg_edge_dev_fn on_dev, cfg_edge_node_fn on_node,
                           void *ctx, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif
