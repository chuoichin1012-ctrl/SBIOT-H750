// What tlsconn.c takes from the board, on the host (Plan 08b, test_tlsconn): lwIP's socket names on
// POSIX, the clock, the log (warnings counted), the certificate store and the clock state (the test's).
#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define lwip_send(fd, buf, len, flags) send(fd, buf, len, (flags) | MSG_NOSIGNAL)
#define lwip_recv recv
#define lwip_select select
#define vTaskDelay(ticks) usleep(1000)

static inline uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

extern int g_tls_warnings;  // SLOG_W lines so far
#define SLOG_I(tag, fmt, ...) printf("  I " tag ": " fmt "\n", ##__VA_ARGS__)
#define SLOG_W(tag, fmt, ...) (g_tls_warnings++, printf("  W " tag ": " fmt "\n", ##__VA_ARGS__))
#define SLOG_E(tag, fmt, ...) printf("  E " tag ": " fmt "\n", ##__VA_ARGS__)

/** The test's store: a heap copy of the fixture for (scope, kind), or NULL. */
char *store_cert_get(const char *scope, const char *kind);
/** g_test_utc_s != 0. */
bool sysmgr_clock_set(void);
