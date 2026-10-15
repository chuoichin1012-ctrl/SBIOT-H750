// UART7 log (spec §12.4: 115200 baud); each line is also handed to syslog (blog_tap).
// newlib-nano's printf has no %llu: print 64-bit values as 32-bit milliseconds.
#pragma once

void blog_init(void);  // before the scheduler starts; creates the mutex
void blog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stddef.h>
void blog_tap(const char *line, size_t len);  // weak: syslog takes every line printed
