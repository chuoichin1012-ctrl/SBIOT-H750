// Host tests for tlsconn (spec §7.3, §5.6, Plan 08b): the client against an mbedTLS server in a thread,
// over 127.0.0.1, with a non-blocking client socket as the bridge has: make -C stm32/firmware/test/host
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "bridge_core.h"
#include "clock.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "tls_core.h"
#include "tls_shim.h"
#include "tls_test_port.h"
#include "tlsconn.h"

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

int g_tls_warnings;

// Allocation faults in the client's thread only (the server thread allocates too): calloc number n fails.
static __thread int t_fail_at = -1;
static __thread bool t_failed;
void *__real_calloc(size_t n, size_t size);
void *__wrap_calloc(size_t n, size_t size)
{
    if (t_fail_at >= 0 && t_fail_at-- == 0) {
        t_failed = true;
        return NULL;
    }
    return __real_calloc(n, size);
}

// ---- the test's store and clock ----------------------------------------------------------------

static const char *s_files[3];  // ca, cert, key fixtures for any scope; NULL: not uploaded

char *store_cert_get(const char *scope, const char *kind)
{
    (void)scope;
    int k = !strcmp(kind, "ca") ? 0 : !strcmp(kind, "cert") ? 1 : 2;
    return s_files[k] ? test_read_file(s_files[k], NULL) : NULL;
}

bool sysmgr_clock_set(void) { return g_test_utc_s != 0; }

static int64_t utc(const char *s)
{
    civil_t c;
    clock_parse(s, &c);
    return clock_to_unix(&c);
}

// ---- the server ----------------------------------------------------------------------------------

enum { SRV_HOLD, SRV_SILENT, SRV_SLOW_READ, SRV_BIG_WRITE, SRV_CLOSE };
typedef struct {
    int mode;
    const char *cert, *key;
    bool want_client;    // require a client certificate signed by ca.pem
    bool optional_client;  // ask for one, accept the session without it
    int lfd;             // listening socket
    int fd;              // the accepted connection
    size_t raw;          // bytes the client sent, before or during the handshake
    // results
    bool hs_ok;
    char sni[64];
    char peer_cn[64];
    size_t got;           // plaintext bytes read (SRV_SLOW_READ)
    bool data_ok;         // SRV_SLOW_READ: the pattern arrived intact
    volatile bool done;   // the client is finished: the server may go
} srv_t;

static int srv_send(void *ctx, const unsigned char *b, size_t n)
{
    ssize_t r = send(((srv_t *)ctx)->fd, b, n, MSG_NOSIGNAL);
    return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)r;
}

static int srv_recv(void *ctx, unsigned char *b, size_t n)
{
    srv_t *s = ctx;
    ssize_t r = recv(s->fd, b, n, 0);
    if (r > 0)
        s->raw += (size_t)r;
    return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)r;
}

static int srv_sni(void *ctx, mbedtls_ssl_context *ssl, const unsigned char *name, size_t len)
{
    (void)ssl;
    srv_t *s = ctx;
    snprintf(s->sni, sizeof s->sni, "%.*s", (int)len, name);
    return 0;
}

static void *server(void *arg)
{
    srv_t *s = arg;
    int fd = accept(s->lfd, NULL, NULL);
    if (fd < 0)
        return NULL;
    s->fd = fd;
    if (s->mode == SRV_SILENT) {  // accepts TCP and never answers
        uint8_t b[512];
        struct timeval tv = {0, 100000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        while (!s->done) {
            ssize_t r = recv(fd, b, sizeof b, 0);
            if (r == 0)
                break;
        }
        close(fd);
        return NULL;
    }
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context ent;
    mbedtls_x509_crt crt, ca;
    mbedtls_pk_context pk;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_entropy_init(&ent);
    mbedtls_x509_crt_init(&crt);
    mbedtls_x509_crt_init(&ca);
    mbedtls_pk_init(&pk);
    char *c = test_read_file(s->cert, NULL), *k = test_read_file(s->key, NULL), *a = test_read_file(PKI "ca.pem", NULL);
    mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, NULL, 0);
    mbedtls_x509_crt_parse(&crt, (const unsigned char *)c, strlen(c) + 1);
    mbedtls_x509_crt_parse(&ca, (const unsigned char *)a, strlen(a) + 1);
    mbedtls_pk_parse_key(&pk, (const unsigned char *)k, strlen(k) + 1, NULL, 0, mbedtls_ctr_drbg_random, &drbg);
    free(c);
    free(k);
    free(a);
    mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    mbedtls_ssl_conf_own_cert(&conf, &crt, &pk);
    mbedtls_ssl_conf_sni(&conf, srv_sni, s);
    if (s->want_client || s->optional_client) {
        mbedtls_ssl_conf_ca_chain(&conf, &ca, NULL);
        mbedtls_ssl_conf_authmode(&conf, s->want_client ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_OPTIONAL);
    }
    mbedtls_ssl_setup(&ssl, &conf);
    mbedtls_ssl_set_bio(&ssl, s, srv_send, srv_recv, NULL);
    int r = mbedtls_ssl_handshake(&ssl);
    s->hs_ok = r == 0;
    if (r == 0) {
        const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&ssl);
        if (peer)
            mbedtls_x509_dn_gets(s->peer_cn, sizeof s->peer_cn, &peer->subject);
        if (s->mode == SRV_SLOW_READ) {
            uint8_t b[1024];
            s->data_ok = true;
            for (;;) {
                int n = mbedtls_ssl_read(&ssl, b, sizeof b);
                if (n <= 0)
                    break;
                for (int i = 0; i < n; i++)
                    if (b[i] != (uint8_t)((s->got + (size_t)i) * 7 + 1))
                        s->data_ok = false;
                s->got += (size_t)n;
                usleep(2000);
            }
        } else if (s->mode == SRV_BIG_WRITE) {
            uint8_t b[4096];
            for (size_t i = 0; i < sizeof b; i++)
                b[i] = (uint8_t)i;
            CHECK(mbedtls_ssl_write(&ssl, b, sizeof b) == (int)sizeof b, "the server's 4 KB went as one record");
        } else if (s->mode == SRV_CLOSE) {
            mbedtls_ssl_close_notify(&ssl);
        }
    }
    while (!s->done)
        usleep(1000);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&ent);
    mbedtls_x509_crt_free(&crt);
    mbedtls_x509_crt_free(&ca);
    mbedtls_pk_free(&pk);
    close(fd);
    return NULL;
}

static pthread_t s_thr;

static int start(srv_t *s, int rcvbuf)
{
    s->lfd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(s->lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (rcvbuf)
        setsockopt(s->lfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t al = sizeof a;
    bind(s->lfd, (struct sockaddr *)&a, sizeof a);
    listen(s->lfd, 1);
    getsockname(s->lfd, (struct sockaddr *)&a, &al);
    pthread_create(&s_thr, NULL, server, s);
    return ntohs(a.sin_port);
}

static void finish(srv_t *s)
{
    s->done = true;
    pthread_join(s_thr, NULL);
    close(s->lfd);
}

// The client's socket as net_tcp_connect leaves it: connected, then non-blocking.
static int dial(int port, int sndbuf)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sndbuf)
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0)
        return -1;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

static void set_files(const char *ca, const char *cert, const char *key)
{
    s_files[0] = ca;
    s_files[1] = cert;
    s_files[2] = key;
}

/** One handshake against a fresh server: the session (closed by the caller), or NULL. */
static tls_conn_t *handshake(srv_t *s, const char *host, int verify, uint32_t *flags, int *fd)
{
    int port = start(s, 0);
    *fd = dial(port, 0);
    return tls_open(*fd, "port1", host, verify, 3000, flags);
}

static void end(srv_t *s, tls_conn_t *t, int fd)
{
    tls_close(t);
    close(fd);
    finish(s);
}

// ---- cases ---------------------------------------------------------------------------------------

static void test_handshake(void)
{
    set_files(PKI "ca.pem", NULL, NULL);
    g_test_utc_s = 0;
    srv_t s = {.mode = SRV_HOLD, .cert = PKI "server.pem", .key = PKI "server.key"};
    int fd, w0 = g_tls_warnings;
    uint32_t flags = 99;
    tls_conn_t *t = handshake(&s, "localhost", 1, &flags, &fd);
    CHECK(t && s.hs_ok, "tlsVerify 1, the right CA: a session (flags 0x%x)", (unsigned)flags);
    CHECK(g_tls_warnings - w0 == 1, "no clock: one warning about the dates (%d)", g_tls_warnings - w0);
    end(&s, t, fd);
}

static void test_wrongca(void)
{
    set_files(PKI "ca.pem", NULL, NULL);
    srv_t s = {.mode = SRV_HOLD, .cert = PKI "other.pem", .key = PKI "other.key"};
    int fd;
    uint32_t flags = 0;
    tls_conn_t *t = handshake(&s, "localhost", 1, &flags, &fd);
    CHECK(!t && (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED), "another CA's server: refused (flags 0x%x)", (unsigned)flags);
    end(&s, t, fd);
}

static void test_names(void)
{
    set_files(PKI "ca.pem", NULL, NULL);
    static const struct {
        const char *host;
        bool ok;
    } CASES[] = {{"localhost", true}, {"127.0.0.1", true}, {"otherhost", false}, {"127.0.0.2", false}};
    for (int k = 0; k < 4; k++) {
        srv_t s = {.mode = SRV_HOLD, .cert = PKI "server.pem", .key = PKI "server.key"};
        int fd;
        uint32_t flags = 0;
        tls_conn_t *t = handshake(&s, CASES[k].host, 1, &flags, &fd);
        if (CASES[k].ok)
            CHECK(t && !strcmp(s.sni, CASES[k].host), "%s: a session, SNI \"%s\"", CASES[k].host, s.sni);
        else
            CHECK(!t && (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH), "%s: not in the SAN, refused (0x%x)", CASES[k].host,
                  (unsigned)flags);
        end(&s, t, fd);
    }
}

static void test_expired(void)
{
    set_files(PKI "ca.pem", NULL, NULL);
    g_test_utc_s = utc("2026-02-01 00:00:00");
    srv_t s = {.mode = SRV_HOLD, .cert = PKI "server.pem", .key = PKI "server.key"};
    int fd;
    uint32_t flags = 0;
    tls_conn_t *t = handshake(&s, "localhost", 1, &flags, &fd);
    CHECK(!t && (flags & MBEDTLS_X509_BADCERT_EXPIRED), "clock set, expired: refused (0x%x)", (unsigned)flags);
    end(&s, t, fd);
    g_test_utc_s = utc("2026-01-02 00:00:00");
    srv_t s2 = {.mode = SRV_HOLD, .cert = PKI "server.pem", .key = PKI "server.key"};
    int w0 = g_tls_warnings;
    t = handshake(&s2, "localhost", 1, &flags, &fd);
    CHECK(t && g_tls_warnings == w0, "clock set, in its dates: a session, no date warning");
    end(&s2, t, fd);
    g_test_utc_s = 0;
}

static void test_verify_none(void)
{
    set_files(NULL, NULL, NULL);
    srv_t s = {.mode = SRV_HOLD, .cert = PKI "other.pem", .key = PKI "other.key"};
    int fd;
    tls_conn_t *t = handshake(&s, "127.0.0.1", 0, NULL, &fd);
    CHECK(t, "tlsVerify 0: any server, no CA needed");
    end(&s, t, fd);
    srv_t s2 = {.mode = SRV_HOLD, .cert = PKI "server.pem", .key = PKI "server.key"};
    t = handshake(&s2, "127.0.0.1", 1, NULL, &fd);
    CHECK(!t && s2.raw == 0, "tlsVerify 1 without a CA: refused before any byte");
    end(&s2, t, fd);
}

static void test_mutual(void)
{
    g_test_utc_s = utc("2026-01-02 00:00:00");  // the test server checks our certificate's dates in full
    srv_t s = {.mode = SRV_HOLD, .cert = PKI "server.pem", .key = PKI "server.key", .want_client = true};
    set_files(PKI "ca.pem", PKI "client.pem", PKI "client.key");
    int fd;
    tls_conn_t *t = handshake(&s, "localhost", 2, NULL, &fd);
    CHECK(t && strstr(s.peer_cn, "CN=gateway"), "tlsVerify 2: the server sees our certificate (%s)", s.peer_cn);
    end(&s, t, fd);
    srv_t s2 = {.mode = SRV_HOLD, .cert = PKI "server.pem", .key = PKI "server.key", .want_client = true};
    set_files(PKI "ca.pem", PKI "client.pem", NULL);
    t = handshake(&s2, "localhost", 2, NULL, &fd);
    CHECK(!t && s2.raw == 0 && !s2.hs_ok, "no key uploaded: refused, the server never saw a ClientHello");
    end(&s2, t, fd);
    g_test_utc_s = 0;
}

// tlsVerify 2 with memory running out at each allocation in turn (Codex 08b code review r1): a session
// never goes up without our certificate, even to a server that would accept one without it.
static void test_mutual_oom(void)
{
    g_test_utc_s = utc("2026-01-02 00:00:00");
    set_files(PKI "ca.pem", PKI "client.pem", PKI "client.key");
    int bad = 0, n = 0;
    for (;; n++) {
        srv_t s = {.mode = SRV_HOLD, .cert = PKI "server.pem", .key = PKI "server.key", .optional_client = true};
        int port = start(&s, 0), fd = dial(port, 0);
        t_failed = false;
        t_fail_at = n;
        tls_conn_t *t = tls_open(fd, "port1", "localhost", 2, 3000, NULL);
        bool reached = t_failed;
        t_fail_at = -1;
        bool up = t != NULL;
        end(&s, t, fd);  // joins the server thread: peer_cn is final
        if (up && !strstr(s.peer_cn, "CN=gateway"))
            bad++;
        if (!reached)
            break;  // the whole handshake ran without hitting allocation n
    }
    CHECK(bad == 0, "out of memory at any of %d allocations: never a session without our certificate (%d)", n, bad);
    g_test_utc_s = 0;
}

static double cpu_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void test_deadline(void)
{
    set_files(PKI "ca.pem", NULL, NULL);
    srv_t s = {.mode = SRV_SILENT};
    int fd = dial(start(&s, 0), 0);
    uint64_t t0 = now_us();
    double c0 = cpu_ms();
    tls_conn_t *t = tls_open(fd, "port1", "localhost", 1, 500, NULL);
    double ms = (double)(now_us() - t0) / 1000, cpu = cpu_ms() - c0;
    CHECK(!t && ms >= 400 && ms <= 600, "a silent server: given up after %.0f ms (500)", ms);
    CHECK(cpu < 50, "waiting, not spinning: %.1f ms of CPU", cpu);
    end(&s, t, fd);
}

typedef struct {
    tls_conn_t *t;
    int again;
} sender_t;

static int send_tls(void *ctx, const uint8_t *data, size_t len)
{
    sender_t *x = ctx;
    int r = tls_send_some(x->t, data, len);
    if (r == NET_AGAIN)
        x->again++;
    return r;
}

static void test_slow_reader(void)
{
    set_files(PKI "ca.pem", NULL, NULL);
    srv_t s = {.mode = SRV_SLOW_READ, .cert = PKI "server.pem", .key = PKI "server.key"};
    int fd = dial(start(&s, 4096), 4096);
    sender_t x = {.t = tls_open(fd, "port1", "localhost", 1, 3000, NULL)};
    CHECK(x.t, "a session");
    static uint8_t frame[1024];
    size_t sent = 0;
    bool lost = false;
    for (int f = 0; x.t && f < 256 && !lost; f++) {  // 256 frames of 1 KB, as the bridge sends them
        for (size_t i = 0; i < sizeof frame; i++)
            frame[i] = (uint8_t)((sent + i) * 7 + 1);
        tx_conn_t tx = {0};
        for (;;) {
            int r = tx_progress(&tx, frame, sizeof frame, send_tls, &x, (uint32_t)(now_us() / 1000));
            if (r == TX_DONE)
                break;
            if (r == TX_LOST) {
                lost = true;
                break;
            }
            fd_set w;
            FD_ZERO(&w);
            FD_SET(fd, &w);
            struct timeval tv = {0, 10000};
            select(fd + 1, NULL, &w, NULL, &tv);
        }
        sent += sizeof frame;
    }
    tls_close(x.t);  // close_notify: the server's read ends
    x.t = NULL;
    for (int i = 0; i < 3000 && s.got < 256 * 1024; i++)
        usleep(1000);
    CHECK(!lost && s.got == 256 * 1024 && s.data_ok, "256 KB to a slow reader: every byte once, in order (%zu)", s.got);
    CHECK(x.again > 0, "the socket filled up on the way (%d NET_AGAIN)", x.again);
    close(fd);
    finish(&s);
}

static void test_pending(void)
{
    set_files(PKI "ca.pem", NULL, NULL);
    srv_t s = {.mode = SRV_BIG_WRITE, .cert = PKI "server.pem", .key = PKI "server.key"};
    int fd = dial(start(&s, 0), 0);
    tls_conn_t *t = tls_open(fd, "port1", "localhost", 1, 3000, NULL);
    CHECK(t, "a session");
    uint8_t buf[1500];
    size_t got = 0;
    bool hidden = false, ok = true;
    uint64_t until = now_us() + 2000000;
    while (t && got < 4096 && now_us() < until) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(fd, &r);
        struct timeval tv = {0, 0};
        int sel = select(fd + 1, &r, NULL, NULL, &tv);
        if (sel == 0 && got > 0 && tls_pending(t))
            hidden = true;  // the socket says nothing, mbedTLS still holds bytes
        if (sel == 0 && !tls_pending(t)) {
            usleep(1000);
            continue;
        }
        int n = tls_recv(t, buf, sizeof buf);
        if (n > 0) {
            for (int i = 0; i < n; i++)
                ok &= buf[i] == (uint8_t)(got + (size_t)i);
            got += (size_t)n;
        }
    }
    CHECK(got == 4096 && ok, "4 KB in one record, read 1500 at a time: all of it (%zu)", got);
    CHECK(hidden, "between reads, select saw nothing while tls_pending held the rest");
    end(&s, t, fd);
}

static void test_close(void)
{
    set_files(PKI "ca.pem", NULL, NULL);
    srv_t s = {.mode = SRV_CLOSE, .cert = PKI "server.pem", .key = PKI "server.key"};
    int fd = dial(start(&s, 0), 0);
    tls_conn_t *t = tls_open(fd, "port1", "localhost", 1, 3000, NULL);
    uint8_t b[64];
    int r = NET_AGAIN;
    for (int i = 0; t && i < 2000 && r == NET_AGAIN; i++) {
        r = tls_recv(t, b, sizeof b);
        if (r == NET_AGAIN)
            usleep(1000);
    }
    CHECK(t && r == 0, "close_notify: 0, closed (%d)", r);
    end(&s, t, fd);
}

int main(void)
{
    test_handshake();
    test_wrongca();
    test_names();
    test_expired();
    test_verify_none();
    test_mutual();
    test_mutual_oom();
    test_deadline();
    test_slow_reader();
    test_pending();
    test_close();
    printf("test_tlsconn: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
