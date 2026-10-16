// Host tests for tls_core (spec §7.3, §10.2 F20, Plan 08b) against the real mbedTLS 3.6:
// make -C stm32/firmware/test/host (the fixtures come from mkpki.py: valid 2026-01-01 to 2026-01-03).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bridge_core.h"
#include "clock.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"
#include "tls_core.h"
#include "tls_test_port.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static void test_dates(void)
{
    const uint32_t EXP = MBEDTLS_X509_BADCERT_EXPIRED, FUT = MBEDTLS_X509_BADCERT_FUTURE,
                   NT = MBEDTLS_X509_BADCERT_NOT_TRUSTED, CN = MBEDTLS_X509_BADCERT_CN_MISMATCH;
    uint32_t f = EXP | FUT | NT;
    CHECK(tls_dates_skip(&f, false) && f == NT, "no clock: only the date flags go (0x%x)", (unsigned)f);
    f = EXP | FUT | NT;
    CHECK(!tls_dates_skip(&f, true) && f == (EXP | FUT | NT), "clock set: nothing goes (0x%x)", (unsigned)f);
    f = CN;
    CHECK(!tls_dates_skip(&f, false) && f == CN, "a name mismatch is never cleared (0x%x)", (unsigned)f);
    f = FUT;
    CHECK(tls_dates_skip(&f, false) && f == 0, "not yet valid, no clock: cleared");
}

static void test_ret(void)
{
    CHECK(tls_ret(5) == 5, "bytes as they are");
    CHECK(tls_ret(MBEDTLS_ERR_SSL_WANT_READ) == NET_AGAIN, "WANT_READ: again");
    CHECK(tls_ret(MBEDTLS_ERR_SSL_WANT_WRITE) == NET_AGAIN, "WANT_WRITE: again");
    CHECK(tls_ret(0) == 0, "0: closed");
    CHECK(tls_ret(MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) == 0, "close_notify: closed");
    CHECK(tls_ret(MBEDTLS_ERR_SSL_CONN_EOF) == 0, "EOF: closed");
    CHECK(tls_ret(MBEDTLS_ERR_NET_RECV_FAILED) == NET_FAIL, "a socket error: lost");
    CHECK(tls_ret(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) == NET_FAIL, "anything else: lost");
}

static int64_t utc(const char *s)
{
    civil_t c;
    clock_parse(s, &c);
    return clock_to_unix(&c);
}

static int check_file(const char *kind, const char *path, const char *other_path, int64_t now, cert_info_t *info)
{
    size_t len;
    char *pem = test_read_file(path, &len), *other = other_path ? test_read_file(other_path, NULL) : NULL;
    if (!pem) {
        printf("FAIL missing fixture %s (run mkpki.py)\n", path);
        failures++;
        return -2;
    }
    int r = cert_check(kind, pem, len, other, now, test_rng, info);
    free(pem);
    free(other);
    return r;
}

static void test_cert_check(void)
{
    cert_info_t i;
    CHECK(check_file("ca", PKI "ca.pem", NULL, 0, &i) == 0, "a CA parses");
    CHECK(strstr(i.subject, "CN=SBIOT Test CA") != NULL, "its subject (%s)", i.subject);
    CHECK(strlen(i.valid_to) == 10 && !strncmp(i.valid_to, "2035-", 5), "its validTo (%s)", i.valid_to);
    CHECK(i.expired == -1 && i.pair == -1, "no clock: expired unknown; a CA has no pair");

    CHECK(check_file("cert", PKI "server.pem", NULL, utc("2026-01-02 00:00:00"), &i) == 0 && i.expired == 0,
          "valid on 2026-01-02 (%d)", i.expired);
    CHECK(!strcmp(i.valid_to, "2026-01-03"), "validTo (%s)", i.valid_to);
    CHECK(check_file("cert", PKI "server.pem", NULL, utc("2026-02-01 00:00:00"), &i) == 0 && i.expired == 1,
          "expired on 2026-02-01 (%d)", i.expired);
    CHECK(i.pair == -1, "no stored key: the pair is not checked");

    const char damaged[] = "-----BEGIN CERTIFICATE-----\nnot base64 at all\n-----END CERTIFICATE-----\n";
    CHECK(cert_check("ca", damaged, sizeof damaged - 1, NULL, 0, test_rng, &i) == -1, "a damaged certificate");
    CHECK(check_file("key", PKI "ca.pem", NULL, 0, &i) == -1, "a certificate given as a key");
    CHECK(check_file("cert", PKI "client.key", NULL, 0, &i) == -1, "a key given as a certificate");
    CHECK(check_file("key", PKI "enc.key", NULL, 0, &i) == -1, "an encrypted key");

    CHECK(check_file("key", PKI "client.key", PKI "client.pem", 0, &i) == 0, "a key parses");
    CHECK(!strcmp(i.key_type, "RSA") && i.pair == 1, "RSA, and it matches its certificate (%s, %d)", i.key_type, i.pair);
    CHECK(i.subject[0] == 0 && i.valid_to[0] == 0 && i.expired == -1, "a key has no subject or dates");
    CHECK(check_file("key", PKI "server.key", PKI "client.pem", 0, &i) == 0 && i.pair == 0, "another key: no match");
    CHECK(check_file("key", PKI "ec.key", NULL, 0, &i) == 0 && !strcmp(i.key_type, "EC") && i.pair == -1,
          "an EC key, no certificate stored (%s, %d)", i.key_type, i.pair);
    CHECK(check_file("cert", PKI "client.pem", PKI "client.key", 0, &i) == 0 && i.pair == 1,
          "a certificate checked against the stored key");
    CHECK(check_file("cert", PKI "client.pem", PKI "ca.pem", 0, &i) == 0 && i.pair == -1,
          "a stored key that does not parse: not checked");
}

static void test_gmtime(void)
{
    static const char *const DAYS[] = {"2024-02-29 23:59:59", "2099-12-31 00:00:00", "2026-10-16 12:34:56"};
    for (int k = 0; k < 3; k++) {
        civil_t c;
        clock_parse(DAYS[k], &c);
        mbedtls_time_t t = (mbedtls_time_t)clock_to_unix(&c);
        struct tm tm;
        struct tm *r = mbedtls_platform_gmtime_r(&t, &tm);
        CHECK(r == &tm && tm.tm_year + 1900 == c.year && tm.tm_mon + 1 == c.mon && tm.tm_mday == c.day &&
                  tm.tm_hour == c.hour && tm.tm_min == c.min && tm.tm_sec == c.sec,
              "gmtime_r on %s", DAYS[k]);
    }
}

int main(void)
{
    test_dates();
    test_ret();
    test_cert_check();
    test_gmtime();
    printf("test_tls_core: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
