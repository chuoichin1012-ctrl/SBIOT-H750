// What the platform provides to components/store: a recursive lock around FlashDB and the cache
// (FreeRTOS on the board, nothing on the host), the clocks, and a diagnostic line.
#pragma once
#include <stdint.h>

void store_port_lock(void);    // recursive
void store_port_unlock(void);
int64_t store_port_wall_ms(void);    // Unix time in ms, 0 while the clock is not set
uint32_t store_port_uptime_ms(void);
void store_port_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
