// The pure parts of TLS (spec §7.3, §10.2 F20, Plan 08b), on mbedTLS, host-tested against the real
// library (test_tls_core): the no-clock date rule, the mapping of mbedTLS results to the bridge's,
// the certificate check of an upload, and mbedTLS's gmtime_r on clock_from_unix.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** The no-clock rule (spec §7.3): with the clock unset, clears EXPIRED and FUTURE from *flags.
 * True when it cleared any (the caller logs the warning). Other flags are never touched. */
bool tls_dates_skip(uint32_t *flags, bool clock_set);

/** An mbedtls_ssl_read/_write result: n > 0 as is; WANT_READ/WANT_WRITE -> NET_AGAIN;
 * 0, PEER_CLOSE_NOTIFY, CONN_EOF -> 0 (closed); anything else -> NET_FAIL. */
int tls_ret(int r);

typedef struct {
    char subject[160];
    char valid_to[11];  // "YYYY-MM-DD", certificates only
    int expired;        // -1 unknown (no clock, or a key), 0, 1
    char key_type[16];  // keys only: mbedtls_pk_get_name ("RSA", "EC")
    int pair;           // -1 not checked (a CA, or the other half missing or unreadable), 0, 1
} cert_info_t;

/** F20: parses pem (len bytes; pem[len] must be '\0') as kind "ca"|"cert"|"key". For cert and key,
 * other (the stored other half, NUL-terminated, or NULL) is checked against it (mbedtls_pk_check_pair).
 * now_s is the clock's UTC seconds, 0 while unset. 0, or -1 when pem does not parse as that kind. */
int cert_check(const char *kind, const char *pem, size_t len, const char *other, int64_t now_s,
               int (*rng)(void *, unsigned char *, size_t), cert_info_t *out);

/** The board's f_rng on the hardware RNG (tls_port.c), for cert_check in `web`. */
int tls_port_rng(void *ctx, unsigned char *out, size_t len);
