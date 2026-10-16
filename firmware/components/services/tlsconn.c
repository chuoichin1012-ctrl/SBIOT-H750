// TLS client over a socket the caller already connected (spec §7.3, Plan 08b), so a fixed local port can
// be bound first. The ESP32's tlsconn.c is the baseline; here the socket is non-blocking (the bridge's,
// §5.6): the handshake waits in lwip_select for what mbedTLS wants, and reads and writes make one
// attempt each, the caller retrying (tx_progress) or waiting (the bridge's select, plus tls_pending).
#include "tlsconn.h"
#include <stdlib.h>
#include <string.h>
#include "bridge_core.h"  // net_classify, NET_AGAIN
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "tls_core.h"
#ifdef TLS_HOST_TEST
#include "tls_shim.h"
#else
#include "FreeRTOS.h"
#include "lwip/sockets.h"
#include "now_us.h"
#include "store.h"
#include "syslog.h"
#include "sysmgr.h"
#include "task.h"
#endif

struct tls_conn {
    int fd;
    const char *scope;
    bool warned;  // the no-clock warning, once per handshake
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt ca, cert;
    mbedtls_pk_context key;
};

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int r = lwip_send(((tls_conn_t *)ctx)->fd, buf, len, MSG_DONTWAIT);
    if (r > 0)
        return r;
    if (r == 0 || net_classify(errno) == NET_AGAIN)  // ENOMEM/ENOBUFS too: lwIP is short, retry (§5.6)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int r = lwip_recv(((tls_conn_t *)ctx)->fd, buf, len, MSG_DONTWAIT);
    if (r >= 0)
        return r;  // 0: the peer closed (CONN_EOF)
    return net_classify(errno) == NET_AGAIN ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
}

static int parse_pem(tls_conn_t *t, const char *kind, void *dst, bool key)
{
    char *pem = store_cert_get(t->scope, kind);
    if (!pem)
        return -1;
    size_t n = strlen(pem) + 1;  // PEM is parsed only with its NUL counted
    int r = key ? mbedtls_pk_parse_key(dst, (const unsigned char *)pem, n, NULL, 0, mbedtls_ctr_drbg_random, &t->drbg)
                : mbedtls_x509_crt_parse(dst, (const unsigned char *)pem, n);
    free(pem);
    return r;
}

// Certificate dates are checked against the gateway clock (MBEDTLS_HAVE_TIME_DATE). Before it is set
// only the two date flags are cleared, with a warning; the chain, the CA and the name are always checked.
static int verify_dates(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    (void)crt;
    (void)depth;
    tls_conn_t *t = ctx;
    if (tls_dates_skip(flags, sysmgr_clock_set()) && !t->warned) {
        t->warned = true;
        SLOG_W("tls", "%s: certificate dates not checked: the clock is not set yet", t->scope);
    }
    return 0;
}

// Waits until fd can be written (or read) or `us` passed. Never spins: a failed select sleeps a tick.
static void wait_fd(int fd, bool write, uint64_t us)
{
    fd_set s;
    FD_ZERO(&s);
    FD_SET(fd, &s);
    struct timeval tv = {.tv_sec = (long)(us / 1000000u), .tv_usec = (long)(us % 1000000u)};
    if (lwip_select(fd + 1, write ? NULL : &s, write ? &s : NULL, NULL, &tv) < 0)
        vTaskDelay(1);
}

// A failure's lines, at most one set a minute per scope: a dead or wrong peer is retried every few seconds.
static bool may_log(const char *scope)
{
    static uint64_t s_last[4];
    int k = !strcmp(scope, "port1") ? 0 : !strcmp(scope, "port2") ? 1 : !strcmp(scope, "mqtt") ? 2 : 3;
    uint64_t now = now_us();
    if (s_last[k] && now - s_last[k] < 60000000u)
        return false;
    s_last[k] = now ? now : 1;
    return true;
}

tls_conn_t *tls_open(int fd, const char *scope, const char *host, int verify, uint32_t timeout_ms, uint32_t *verify_flags)
{
    if (verify_flags)
        *verify_flags = 0;
    tls_conn_t *t = calloc(1, sizeof *t);
    if (!t) {
        SLOG_W("tls", "%s: no memory for a TLS session", scope);
        return NULL;
    }
    t->fd = fd;
    t->scope = scope;
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_ctr_drbg_init(&t->drbg);
    mbedtls_entropy_init(&t->entropy);
    mbedtls_x509_crt_init(&t->ca);
    mbedtls_x509_crt_init(&t->cert);
    mbedtls_pk_init(&t->key);

    bool started = false;  // a byte may have gone to the peer: close_notify on failure
    int r = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy, NULL, 0);
    if (!r)
        r = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (r)
        goto fail;
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
    mbedtls_ssl_conf_min_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_2);
    // tlsVerify: 0 none, 1 verify the server (CA), 2 also present our certificate.
    if (verify >= 1) {
        if (parse_pem(t, "ca", &t->ca, false) != 0) {
            SLOG_E("tls", "%s: server verification needs a CA certificate; upload one", scope);
            r = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
            goto fail;
        }
        mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, NULL);
        mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_verify(&t->conf, verify_dates, t);
    } else {
        mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    if (verify == 2) {
        if (parse_pem(t, "cert", &t->cert, false) != 0 || parse_pem(t, "key", &t->key, true) != 0) {
            SLOG_E("tls", "%s: mutual TLS needs a client certificate and key; upload both", scope);
            r = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
            goto fail;
        }
        mbedtls_ssl_conf_own_cert(&t->conf, &t->cert, &t->key);
    }
    // Always the dialled host (CVE-2025-27809): the name check, and SNI (an IP goes out too, spec §7.3).
    r = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (!r)
        r = mbedtls_ssl_set_hostname(&t->ssl, host);
    if (r)
        goto fail;
    mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);

    // One deadline for the whole handshake: a peer that stops answering cannot hold the task.
    started = true;
    uint64_t deadline = now_us() + (uint64_t)timeout_ms * 1000u;
    while ((r = mbedtls_ssl_handshake(&t->ssl)) != 0) {
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE)
            break;
        uint64_t now = now_us();
        if (now >= deadline) {
            r = MBEDTLS_ERR_SSL_TIMEOUT;
            break;
        }
        wait_fd(fd, r == MBEDTLS_ERR_SSL_WANT_WRITE, deadline - now);
    }
    if (r)
        goto fail;
    SLOG_I("tls", "%s: TLS session with %s (%s)", scope, host, mbedtls_ssl_get_ciphersuite(&t->ssl));
    return t;

fail:;
    uint32_t flags = started ? mbedtls_ssl_get_verify_result(&t->ssl) : 0;
    if (flags == (uint32_t)-1)
        flags = 0;
    if (verify_flags)
        *verify_flags = flags;
    if (may_log(scope)) {
        char msg[96];
        mbedtls_strerror(r, msg, sizeof msg);
        if (flags)
            SLOG_W("tls", "%s: certificate rejected (flags 0x%lx)", scope, (unsigned long)flags);
        SLOG_W("tls", "%s: TLS handshake with %s failed: %s", scope, host ? host : "?", msg);
    }
    if (!started)
        t->fd = -1;  // nothing was sent: no close_notify
    tls_close(t);
    return NULL;
}

int tls_send_some(tls_conn_t *t, const uint8_t *data, size_t len) { return tls_ret(mbedtls_ssl_write(&t->ssl, data, len)); }

int tls_recv(tls_conn_t *t, uint8_t *buf, size_t max) { return tls_ret(mbedtls_ssl_read(&t->ssl, buf, max)); }

bool tls_pending(tls_conn_t *t) { return mbedtls_ssl_check_pending(&t->ssl) != 0; }

void tls_close(tls_conn_t *t)
{
    if (!t)
        return;
    if (t->fd >= 0)
        mbedtls_ssl_close_notify(&t->ssl);  // one try: the socket is non-blocking
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_ctr_drbg_free(&t->drbg);
    mbedtls_entropy_free(&t->entropy);
    mbedtls_x509_crt_free(&t->ca);
    mbedtls_x509_crt_free(&t->cert);
    mbedtls_pk_free(&t->key);
    free(t);
}
