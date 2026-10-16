// The host tests' mbedTLS port (Plan 08b): the fake clock, random numbers, PEM fixtures (build/pki).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern int64_t g_test_utc_s;  // what tls_port_time answers; 0 while "not set"
int test_rng(void *ctx, unsigned char *out, size_t len);
/** The whole file, NUL-terminated (len without the NUL); NULL when missing. free() it. */
char *test_read_file(const char *path, size_t *len);
#define PKI "build/pki/"
