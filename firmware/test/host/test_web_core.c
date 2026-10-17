// Host tests for the web rules (spec §10.1): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "web_core.h"

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

#define S(x) (x), strlen(x)

static void test_token(void)
{
    char t[64];
    CHECK(web_token_make("admin", "admin", t, sizeof t) == 16 && strcmp(t, "YWRtaW46YWRtaW4=") == 0, "admin:admin (%s)", t);
    CHECK(web_token_make("a", "", t, sizeof t) == 4 && strcmp(t, "YTo=") == 0, "a: (%s)", t);
    CHECK(web_token_make("ab", "c", t, sizeof t) == 8 && strcmp(t, "YWI6Yw==") == 0, "ab:c (%s)", t);
    CHECK(web_token_make("admin", "admin", t, 16) == 0, "no room for the NUL: 0");
    CHECK(web_token_eq("YWRtaW46YWRtaW4=", S("YWRtaW46YWRtaW4=")), "equal");
    CHECK(!web_token_eq("YWRtaW46YWRtaW4=", S("YWRtaW46YWRtaW4")), "a prefix");
    CHECK(!web_token_eq("YWRtaW46YWRtaW4=", S("YWRtaW46YWRtaW4=x")), "longer");
    CHECK(!web_token_eq("YWRtaW46YWRtaW4=", S("YWRtaW46YWRtaW5=")), "same length, one byte off");
    CHECK(!web_token_eq("YWRtaW46YWRtaW4=", S("")), "empty");
    CHECK(!web_token_eq("", S("")), "an empty login never matches");
    CHECK(web_basic_ok("YWRtaW46YWRtaW4=", S("Basic YWRtaW46YWRtaW4=")), "Basic header");
    CHECK(!web_basic_ok("YWRtaW46YWRtaW4=", S("Bearer YWRtaW46YWRtaW4=")), "another scheme");
    CHECK(!web_basic_ok("YWRtaW46YWRtaW4=", S("Basic  YWRtaW46YWRtaW4=")), "two spaces");
    CHECK(!web_basic_ok("YWRtaW46YWRtaW4=", S("Basic")), "no token");
}

static void test_decode(void)
{
    char o[64];
    size_t n = web_url_decode(S("YWRtaW46YWRtaW4%3D"), o, sizeof o);
    CHECK(n == 16 && memcmp(o, "YWRtaW46YWRtaW4=", 16) == 0, "%%3D");
    n = web_url_decode(S("a%2Bb%2fc+d"), o, sizeof o);
    CHECK(n == 7 && memcmp(o, "a+b/c d", 7) == 0, "%%2B, %%2f and +");
    n = web_url_decode(S("a%2"), o, sizeof o);
    CHECK(n == 3 && memcmp(o, "a%2", 3) == 0, "a cut escape stays as it is");
    CHECK(web_url_decode(S("abcd"), o, 4) == SIZE_MAX, "too small (no room for the NUL)");
}

static void test_names(void)
{
    char n[16];
    CHECK(web_config_name(S("/api/config/port1"), n, sizeof n) && strcmp(n, "port1") == 0, "port1");
    CHECK(web_config_name(S("/api/config/system?x=1"), n, sizeof n) && strcmp(n, "system") == 0, "a query");
    CHECK(!web_config_name(S("/api/config/"), n, sizeof n), "no name");
    CHECK(!web_config_name(S("/api/config/a/b"), n, sizeof n), "a slash");
    CHECK(!web_config_name(S("/api/config/abcdefghijklmnopq"), n, sizeof n), "too long");
    CHECK(!web_config_name(S("/api/configport1"), n, sizeof n), "not the prefix");
    CHECK(web_endpoint(true, S("/api/ota")) == WEB_EP_OTA, "OTA");
    CHECK(web_endpoint(true, S("/api/cert?scope=mqtt&kind=ca")) == WEB_EP_CERT, "cert");
    CHECK(web_endpoint(true, S("/api/config/edgeDevices")) == WEB_EP_EDGE, "edgeDevices");
    CHECK(web_endpoint(true, S("/api/config/edgeDevicesX")) == WEB_EP_PLAIN, "not edgeDevices");
    CHECK(web_endpoint(false, S("/api/config/edgeDevices")) == WEB_EP_PLAIN, "a GET");
    CHECK(web_endpoint(true, S("/api/config/system")) == WEB_EP_PLAIN, "system");
}

static void test_policy(void)
{
    const size_t slot = 1472u * 1024u;
    CHECK(web_body_check(WEB_EP_PLAIN, true, false, 10240, slot) == 0, "10 240 bytes");
    CHECK(web_body_check(WEB_EP_PLAIN, true, false, 10241, slot) == 413, "10 241 bytes");
    CHECK(web_body_check(WEB_EP_PLAIN, false, false, 0, slot) == 0, "a GET: no body, no length");
    CHECK(web_body_check(WEB_EP_PLAIN, true, true, 10, slot) == 411, "chunked");
    CHECK(web_body_check(WEB_EP_EDGE, true, false, 96000, slot) == 0, "Edge 96 000 bytes");
    CHECK(web_body_check(WEB_EP_EDGE, true, false, 96001, slot) == 413, "Edge 96 001 bytes");
    CHECK(web_body_check(WEB_EP_EDGE, false, false, 0, slot) == 411, "Edge without a length");
    CHECK(web_body_check(WEB_EP_CERT, true, false, 12288, slot) == 0, "cert 12 KB");
    CHECK(web_body_check(WEB_EP_CERT, true, false, 12289, slot) == 413, "cert over 12 KB");
    CHECK(web_body_check(WEB_EP_OTA, true, false, slot, slot) == 0, "OTA, the slot");
    CHECK(web_body_check(WEB_EP_OTA, true, false, slot + 1, slot) == 413, "OTA over the slot");
    CHECK(web_body_check(WEB_EP_OTA, true, true, 100, slot) == 411, "OTA chunked");
    CHECK(!web_stalled(1000, 1000 + WEB_STALL_MS - 1), "under 30 s");
    CHECK(web_stalled(1000, 1000 + WEB_STALL_MS), "30 s");
}

#define CERT "-----BEGIN CERTIFICATE-----\nMIIBMTCB2aADAgECAgkAluqk\ngeuV/zUw+CgY=\n-----END CERTIFICATE-----\n"

static void test_pem(void)
{
    CHECK(web_pem_check("ca", S(CERT)) == 0, "a certificate as ca");
    CHECK(web_pem_check("cert", S("\r\n" CERT "\r\n")) == 0, "with CRLF and blank lines around");
    CHECK(web_pem_check("key", S(CERT)) == -1, "a certificate as key");
    CHECK(web_pem_check("key", S("-----BEGIN PRIVATE KEY-----\nMIIEvQ==\n-----END PRIVATE KEY-----\n")) == 0, "PKCS#8");
    CHECK(web_pem_check("key", S("-----BEGIN RSA PRIVATE KEY-----\nMIIEvQ==\n-----END RSA PRIVATE KEY-----\n")) == 0, "RSA");
    CHECK(web_pem_check("key", S("-----BEGIN EC PRIVATE KEY-----\nMHcCAQ==\n-----END EC PRIVATE KEY-----\n")) == 0, "EC");
    CHECK(web_pem_check("key", S("-----BEGIN ENCRYPTED PRIVATE KEY-----\nMIIE\n-----END ENCRYPTED PRIVATE KEY-----\n")) == -1,
          "an encrypted key");
    CHECK(web_pem_check("ca", S("-----BEGIN CERTIFICATE-----\nMIIB\n-----END PRIVATE KEY-----\n")) == -1, "END of another label");
    CHECK(web_pem_check("ca", S("-----BEGIN CERTIFICATE-----\nMI!B\n-----END CERTIFICATE-----\n")) == -1, "not base64");
    CHECK(web_pem_check("ca", S("-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----\n")) == -1, "empty");
    CHECK(web_pem_check("ca", S("junk\n" CERT)) == -1, "text before");
    CHECK(web_pem_check("ca", S(CERT CERT)) == -1, "two certificates (one per file)");
    CHECK(web_pem_check("ca", S("-----BEGIN CERTIFICATE-----\nMIIB\n")) == -1, "no END");
    CHECK(web_pem_check("dh", S(CERT)) == -1, "an unknown kind");
}

// Plan 10c task 9 (Codex review, Important 9): a client that stops reading its answer loses the slot.
// The clock starts when the send queue fills, restarts at each write that sent bytes, stops when it empties.
static void test_send_stall(void)
{
    web_send_t t = {0};
    uint64_t now = 3600000;  // an hour idle: nothing queued, nothing counted
    CHECK(!web_send_poll(&t, 0, false, now), "idle: not stalled");
    CHECK(!web_send_poll(&t, 500, false, now), "the first answer after an hour: not stalled at once");
    CHECK(!web_send_poll(&t, 500, false, now + WEB_STALL_MS - 1), "nor before the stall time");
    CHECK(web_send_poll(&t, 500, false, now + WEB_STALL_MS), "nothing written for the stall time: stalled");
    web_send_t m = {0};
    bool cut = false;
    web_send_poll(&m, 4000, false, now);
    for (uint64_t ms = 1000; ms <= 2 * WEB_STALL_MS; ms += 1000)
        cut |= web_send_poll(&m, 4000, true, now + ms);  // a write every second
    CHECK(!cut, "a write every second for twice the stall time: never stalled");
    web_send_t e = {0};
    web_send_poll(&e, 100, false, now);
    web_send_poll(&e, 0, true, now + 1000);  // emptied
    CHECK(!web_send_poll(&e, 100, false, now + WEB_STALL_MS + 5000), "filled again later: counted from the new fill");
    CHECK(web_send_poll(&e, 100, false, now + 2 * WEB_STALL_MS + 5000), "and stalled a stall time after it");
}

// The WebSocket console's UART monitor (the user, 2026-10-17): every frame of the port, both ways, one text line
// each: "TX " or "RX ", the bytes in hex, a newline.
static void test_mon_line(void)
{
    char out[WEB_MON_LINE_MAX];
    const uint8_t req[] = {0x04, 0x04, 0x00, 0x00, 0x00, 0x02, 0x70, 0x5E};
    size_t n = web_mon_line(out, sizeof out, true, req, sizeof req);
    CHECK(n == strlen("TX 04 04 00 00 00 02 70 5E\n") && !strcmp(out, "TX 04 04 00 00 00 02 70 5E\n"), "a request sent: %s", out);
    n = web_mon_line(out, sizeof out, false, (const uint8_t *)"\xab", 1);
    CHECK(!strcmp(out, "RX AB\n") && n == 6, "a byte received: %s", out);
    uint8_t big[1460];
    memset(big, 0xFF, sizeof big);
    n = web_mon_line(out, sizeof out, false, big, sizeof big);
    CHECK(n == 3 + 3 * 1460 && out[n - 1] == '\n' && out[n] == '\0', "the largest frame fits WEB_MON_LINE_MAX (%u)", (unsigned)n);
    char small[16];
    n = web_mon_line(small, sizeof small, true, req, sizeof req);
    CHECK(n == strlen(small) && n < sizeof small && small[n - 1] == '\n', "a short buffer: cut on a whole byte, still a line (%s)", small);
    n = web_mon_line(out, sizeof out, true, NULL, 0);
    CHECK(!strcmp(out, "TX not sent\n") && n == 12, "an empty TX record: the frame above did not go out (%s)", out);
}

static size_t s_chunks[4], s_nchunks;
static void chunk(const uint8_t *d, size_t len, void *ctx)
{
    (void)d;
    (void)ctx;
    if (s_nchunks < 4)
        s_chunks[s_nchunks] = len;
    s_nchunks++;
}

// A write longer than a frame (2000 bytes typed on the page) is shown whole, as several lines (Codex review).
static void test_mon_each(void)
{
    static uint8_t big[2000];
    s_nchunks = 0;
    web_mon_each(big, sizeof big, 1460, chunk, NULL);
    CHECK(s_nchunks == 2 && s_chunks[0] == 1460 && s_chunks[1] == 540, "2000 bytes: 1460 + 540 (%u records)", (unsigned)s_nchunks);
    s_nchunks = 0;
    web_mon_each(big, 1460, 1460, chunk, NULL);
    CHECK(s_nchunks == 1 && s_chunks[0] == 1460, "a frame of exactly 1460: one record");
    s_nchunks = 0;
    web_mon_each(NULL, 0, 1460, chunk, NULL);
    CHECK(s_nchunks == 1 && s_chunks[0] == 0, "an empty record still goes through (TX not sent)");
}

int main(void)
{
    test_mon_line();
    test_mon_each();
    test_send_stall();
    test_token();
    test_decode();
    test_names();
    test_policy();
    test_pem();
    printf("test_web_core: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
