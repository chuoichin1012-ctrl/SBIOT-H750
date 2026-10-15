// Log capture (spec §5.3, §6.1, ported from the ESP32 syslog): every blog() line still goes to
// UART7 at once, and is also queued to the syslog task. That task keeps it in a 32 KB RAM ring
// in SRAM4 (/api/log, the LOG console) and writes warnings and errors to the persistent log
// (store TSDB, the only writer of it), at most 30 lines a minute.
#pragma once
#include <stddef.h>
#include "blog.h"
#include "now_us.h"

void syslog_init(void);  // in app_main, before the scheduler starts

/** Calls fn for each line in the RAM ring, oldest first (from a copy: the ring stays writable). */
typedef void (*syslog_line_fn)(const char *line, size_t len, void *ctx);
void syslog_ram_iter(syslog_line_fn fn, void *ctx);
/** Live tap (one at a time) for the LOG console; NULL to detach. Called from the syslog task. */
void syslog_set_tap(syslog_line_fn fn, void *ctx);

// "W (1234) tag: text": the letter decides whether the line is kept across restarts.
#define SLOG_I(tag, fmt, ...) blog("I (%lu) " tag ": " fmt "\r\n", (unsigned long)(now_us() / 1000u), ##__VA_ARGS__)
#define SLOG_W(tag, fmt, ...) blog("W (%lu) " tag ": " fmt "\r\n", (unsigned long)(now_us() / 1000u), ##__VA_ARGS__)
#define SLOG_E(tag, fmt, ...) blog("E (%lu) " tag ": " fmt "\r\n", (unsigned long)(now_us() / 1000u), ##__VA_ARGS__)
