// A TLS 1.2 client session over a connected, non-blocking lwIP socket (spec §7.3, Plan 08b): the
// ESP32's tlsconn.c, with the waits in lwip_select so a bridge task never spins. Host-tested over
// loopback against an mbedTLS server (test_tlsconn).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct tls_conn tls_conn_t;

/** TLS 1.2 client on fd (connected, non-blocking; the caller keeps owning it). Certificates from
 * store_cert_get(scope, "ca"|"cert"|"key") per verify (0 none, 1 CA, 2 CA plus our cert and key).
 * host: the dialled name or address, always set (SNI and the name check; an IP goes out as SNI too,
 * spec §7.3). The handshake waits in lwip_select for what mbedTLS wants, until timeout_ms. NULL on
 * any failure, logged; *verify_flags (may be NULL) then holds the certificate's verify flags, or 0. */
tls_conn_t *tls_open(int fd, const char *scope, const char *host, int verify, uint32_t timeout_ms, uint32_t *verify_flags);
/** One mbedtls_ssl_write attempt: >0, NET_AGAIN, NET_FAIL (tls_ret). After NET_AGAIN the caller must
 * call again with the same data, as mbedTLS requires (tx_progress does: frame + off, len - off). */
int tls_send_some(tls_conn_t *t, const uint8_t *data, size_t len);
/** One mbedtls_ssl_read: >0, 0 closed, NET_AGAIN, NET_FAIL. */
int tls_recv(tls_conn_t *t, uint8_t *buf, size_t max);
/** Bytes or a record already inside mbedTLS, which select() cannot see (mbedtls_ssl_check_pending). */
bool tls_pending(tls_conn_t *t);
/** close_notify (one try, non-blocking), then frees everything. NULL is fine. The socket stays open. */
void tls_close(tls_conn_t *t);
