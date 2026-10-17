// What the platform provides to components/store: a recursive lock around FlashDB and the cache
// (FreeRTOS on the board, nothing on the host), the clocks, and a diagnostic line.
#pragma once
#include <stdint.h>

void store_port_lock(void);    // recursive
void store_port_unlock(void);
int64_t store_port_wall_ms(void);    // Unix time in ms, 0 while the clock is not set
uint32_t store_port_uptime_ms(void);
void store_port_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/** A factory reset ended (or could not be done): the gateway restarts, after a short delay for a web reply. */
void store_port_restart(void);
/** A flash sector was erased: the calling task is making progress. A factory reset erases for 40 s and more
 * (160 sectors at up to 250 ms), longer than the web task's heartbeat timeout; the lock wait does not call it. */
void store_port_progress(void);
