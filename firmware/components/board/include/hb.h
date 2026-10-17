// Heartbeats of the supervised tasks (Plan 11a task 0b, the user): each beats once a loop slice; the wdog task
// restarts the gateway when one is later than its timeout. Pure, FreeRTOS-free, host-tested.
// Concurrency: hb_add must be serialized by the caller (wdog_hb_add takes a critical section); a slot is filled,
// its name copied, before the count that publishes it is stored (release); beats and scans are atomic.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define HB_MAX 32       // above the 18 tasks that can run at once
#define HB_NAME_MAX 16  // with its NUL

/** A slot for `name` (copied), its first beat now; -1 when all HB_MAX are taken (never silent: the caller logs). */
int hb_add(const char *name, uint32_t timeout_ms, uint32_t now_ms);
/** hb_add with an owner (the task's handle): code working for its caller finds the caller's slot with hb_find. */
int hb_add_owned(const char *name, uint32_t timeout_ms, uint32_t now_ms, const void *owner);
/** The slot of `owner`, -1 when it has none (NULL never matches). */
int hb_find(const void *owner);
/** The task is alive (any id from hb_add; -1 does nothing). */
void hb_beat(int id, uint32_t now_ms);
/** The first slot whose last beat is `timeout_ms` or more old, else -1 (wrap-safe). */
int hb_expired(uint32_t now_ms);
/** How long the slot has not beaten, in ms. */
uint32_t hb_late_ms(int id, uint32_t now_ms);
/** A slot's name, NULL for an id that is not one. */
const char *hb_name(int id);

/** Why the supervisor restarted, kept across the IWDG1 reset in a .noinit section (Codex plan r4). */
typedef struct {
    uint32_t magic;
    char name[HB_NAME_MAX];
    uint32_t late_s;
    uint32_t crc;
} hb_reason_t;
void hb_reason_set(hb_reason_t *r, const char *name, uint32_t late_s);
/** True with the name and seconds late when `r` holds a reason (magic and CRC right; random RAM fails). */
bool hb_reason_get(const hb_reason_t *r, char name[HB_NAME_MAX], uint32_t *late_s);
void hb_reason_clear(hb_reason_t *r);
/** Whether a kept reason belongs to this reset (Codex plan r6: RCC_RSR's flags do not exclude each other; a
 *  power-on sets PINRSTF too): never after a power-on or brown-out; else an IWDG1 or a pin reset (the SGM706
 *  pulls NRST once R20 is fitted). */
bool hb_restart_was_ours(bool por, bool bor, bool iwdg, bool pin);
