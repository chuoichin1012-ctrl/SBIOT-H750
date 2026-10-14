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

#ifdef __cplusplus
}
#endif
