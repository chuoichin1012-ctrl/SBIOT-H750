// The pure parts of TLS (spec §7.3, §10.2 F20, Plan 08b); see tls_core.h. The ESP32's tlsconn.c
// (verify_dates) and api.c (cert_post) are the baseline.
#include "tls_core.h"
#include <stdio.h>
#include <string.h>
#include "bridge_core.h"  // NET_AGAIN, NET_FAIL
#include "clock.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

bool tls_dates_skip(uint32_t *flags, bool clock_set)
{
    const uint32_t dates = MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE;
    if (clock_set || !(*flags & dates))
        return false;
    *flags &= ~dates;
    return true;
}

int tls_ret(int r)
{
    if (r > 0)
        return r;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        return NET_AGAIN;
    if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == MBEDTLS_ERR_SSL_CONN_EOF)
        return 0;
    return NET_FAIL;
}

// mbedTLS reads the certificates' dates through this (MBEDTLS_PLATFORM_GMTIME_R_ALT): no newlib time zone.
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf)
{
    civil_t c;
    clock_from_unix((int64_t)*tt, &c);
    memset(tm_buf, 0, sizeof *tm_buf);
    tm_buf->tm_year = c.year - 1900;
    tm_buf->tm_mon = c.mon - 1;
    tm_buf->tm_mday = c.day;
    tm_buf->tm_hour = c.hour;
    tm_buf->tm_min = c.min;
    tm_buf->tm_sec = c.sec;
    return tm_buf;
}

static int parse(bool key, const char *pem, size_t len, mbedtls_x509_crt *crt, mbedtls_pk_context *pk,
                 int (*rng)(void *, unsigned char *, size_t))
{
    // Both parsers take PEM only with its terminating NUL counted.
    return key ? mbedtls_pk_parse_key(pk, (const unsigned char *)pem, len + 1, NULL, 0, rng, NULL)
               : mbedtls_x509_crt_parse(crt, (const unsigned char *)pem, len + 1);
}

int cert_check(const char *kind, const char *pem, size_t len, const char *other, int64_t now_s,
               int (*rng)(void *, unsigned char *, size_t), cert_info_t *out)
{
    memset(out, 0, sizeof *out);
    out->expired = -1;
    out->pair = -1;
    bool is_key = !strcmp(kind, "key");
    mbedtls_x509_crt crt, ocrt;
    mbedtls_pk_context pk, opk;
    mbedtls_x509_crt_init(&crt);
    mbedtls_x509_crt_init(&ocrt);
    mbedtls_pk_init(&pk);
    mbedtls_pk_init(&opk);
    int r = -1;
    if (pem[len] != '\0' || parse(is_key, pem, len, &crt, &pk, rng) != 0)
        goto done;
    if (is_key) {
        snprintf(out->key_type, sizeof out->key_type, "%s", mbedtls_pk_get_name(&pk));
    } else {
        if (mbedtls_x509_dn_gets(out->subject, sizeof out->subject, &crt.subject) < 0)
            out->subject[0] = '\0';
        const mbedtls_x509_time *t = &crt.valid_to;
        snprintf(out->valid_to, sizeof out->valid_to, "%04u-%02u-%02u", (unsigned)t->year % 10000u, (unsigned)t->mon % 100u,
                 (unsigned)t->day % 100u);
        if (now_s > 0) {
            civil_t c = {t->year, t->mon, t->day, t->hour, t->min, t->sec};
            out->expired = clock_to_unix(&c) < now_s;
        }
    }
    if (strcmp(kind, "ca") != 0 && other && parse(!is_key, other, strlen(other), &ocrt, &opk, rng) == 0) {
        const mbedtls_pk_context *pub = is_key ? &ocrt.pk : &crt.pk, *prv = is_key ? &pk : &opk;
        out->pair = mbedtls_pk_check_pair(pub, prv, rng, NULL) == 0;
    }
    r = 0;
done:
    mbedtls_x509_crt_free(&crt);
    mbedtls_x509_crt_free(&ocrt);
    mbedtls_pk_free(&pk);
    mbedtls_pk_free(&opk);
    return r;
}
