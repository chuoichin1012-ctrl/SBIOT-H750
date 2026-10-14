// Plain UART7 log for bring-up (spec §12.4: 115200 baud). Plan 05 puts syslog in front of it.
// newlib-nano's printf has no %llu: print 64-bit values as 32-bit milliseconds.
#pragma once

void blog_init(void);  // before the scheduler starts; creates the mutex
void blog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
