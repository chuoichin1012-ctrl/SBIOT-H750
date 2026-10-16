// Host tests for the bridge logic (spec §6.4, §5.6, Plan 08a): make -C stm32/firmware/test/host
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bridge_core.h"

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

static bridge_cfg_t cfg_of(const char *port, const char *sys)
{
    cJSON *p = port ? cJSON_Parse(port) : NULL, *s = sys ? cJSON_Parse(sys) : NULL;
    bridge_cfg_t c;
    bridge_cfg_parse(p, s, &c);
    cJSON_Delete(p);
    cJSON_Delete(s);
    return c;
}

// ---- config ---------------------------------------------------------------------------------------

static void test_cfg(void)
{
    bridge_cfg_t c = cfg_of(
        "{\"workMode\":3,\"socketMode\":1,\"remoteHost\":\"h.example\",\"remotePort\":502,\"localPort\":7,\"udpAnyRemotePort\":1,"
        "\"listenPort\":8899,\"maxClients\":2,\"clientLimitAction\":1,\"allowedClients\":\"192.168.0.0/24\",\"httpHost\":\"web\","
        "\"httpPort\":8080,\"httpLocalPort\":0,\"httpMethod\":1,\"httpPath\":\"/in\",\"httpHeaders\":\"k: 1\",\"httpStripHeaders\":true,"
        "\"httpTimeout\":9,\"reconnectInterval\":5,\"shortConnectionTime\":4,\"modbusPoll\":true,\"modbusPollTimeout\":300,"
        "\"modbusException\":true,\"netHeartbeat\":true,\"netHeartbeatContent\":0,\"netHeartbeatHex\":1,\"netHeartbeatData\":\"AA55\","
        "\"netHeartbeatInterval\":3,\"registration\":1,\"registrationSend\":2,\"registrationHex\":0,\"registrationData\":\"ID42\","
        "\"tls\":0,\"tlsVerify\":1}",
        "{\"bufferOffline\":1}");
    CHECK(c.work == BR_TCP_SERVER && c.extra == BR_X_MODBUS && !strcmp(c.host, "h.example") && c.remote_port == 502 &&
              c.local_port == 7 && c.any_port && c.listen_port == 8899 && c.max_clients == 2 && c.limit_action == 1,
          "the socket fields");
    CHECK(!c.acl_bad && ipacl_allows(&c.acl, 0xC0A80005u) && !ipacl_allows(&c.acl, 0xC0A80105u), "allowedClients parsed");
    CHECK(!strcmp(c.http_host, "web") && c.http_port == 8080 && c.http_post && !strcmp(c.http_path, "/in") &&
              !strcmp(c.http_headers, "k: 1") && c.http_strip && c.http_timeout_s == 9,
          "the HTTP fields");
    CHECK(c.reconnect_s == 5 && c.short_s == 4 && c.poll && c.poll_timeout_ms == 300 && c.exception, "timings, polling, exception");
    CHECK(c.keep_offline, "system.bufferOffline");
    CHECK(c.modbus_gw && !c.modbus_client && !c.short_conn, "a TCP server with socketMode 1: the Modbus gateway");
    CHECK(!c.hb && c.reg == 0, "a TCP server: no network heartbeat, no registration (as the ESP32)");
    c = cfg_of("{\"workMode\":1,\"socketMode\":3,\"netHeartbeat\":true,\"netHeartbeatInterval\":3,\"registration\":4,\"tls\":1}", NULL);
    CHECK(c.modbus_gw && c.modbus_client && c.short_conn && c.hb && c.reg == 4 && c.tls == 1 && !c.keep_offline,
          "a TCP client with socketMode 3: Modbus client and short connection; heartbeat, registration, TLS kept");
    c = cfg_of("{\"workMode\":2,\"tls\":1,\"registration\":2}", NULL);
    CHECK(c.tls == 0 && c.reg == 0, "a UDP server: no TLS, no registration");
    c = cfg_of("{\"workMode\":3,\"maxClients\":40,\"allowedClients\":\"300.1.1.1\"}", NULL);
    CHECK(c.max_clients == BR_MAX_CLIENTS && c.acl_bad, "maxClients above 16 is 16; a bad allowedClients is flagged");
    c = cfg_of(NULL, NULL);
    CHECK(c.work == BR_UDP_CLIENT && c.max_clients == BR_MAX_CLIENTS && !c.modbus_gw, "no config: the defaults of an empty object");
    CHECK(bridge_reg_content(1) == 0 && bridge_reg_content(2) == 4 && bridge_reg_content(3) == 1 && bridge_reg_content(4) == 2 &&
              bridge_reg_content(5) == 3 && bridge_reg_content(0) == -1 && bridge_reg_content(6) == -1,
          "registration 1..5: custom, MAC, IMEI, SN, ICCID");
}

// ---- Modbus TCP reassembly ----------------------------------------------------------------------

static uint8_t g_adu[8][MBTCP_MAX];
static size_t g_adu_len[8];
static int g_n;
static void on_adu(void *ctx, const uint8_t *adu, size_t len)
{
    if (g_n < 8) {
        memcpy(g_adu[g_n], adu, len);
        g_adu_len[g_n] = len;
    }
    g_n++;
}

static void test_mbtcp(void)
{
    const uint8_t a1[] = {0, 1, 0, 0, 0, 6, 9, 3, 0, 0, 0, 2};  // read 2 holding registers, tid 1
    const uint8_t a2[] = {0, 2, 0, 0, 0, 6, 9, 3, 0, 4, 0, 1};
    mbtcp_acc_t acc = {0};
    g_n = 0;
    mbtcp_push(&acc, a1, 5, on_adu, NULL);
    CHECK(g_n == 0, "half an ADU: nothing yet");
    mbtcp_push(&acc, a1 + 5, sizeof a1 - 5, on_adu, NULL);
    CHECK(g_n == 1 && g_adu_len[0] == sizeof a1 && !memcmp(g_adu[0], a1, sizeof a1), "split over two segments: joined");
    uint8_t two[sizeof a1 + sizeof a2];
    memcpy(two, a1, sizeof a1);
    memcpy(two + sizeof a1, a2, sizeof a2);
    g_n = 0;
    mbtcp_push(&acc, two, sizeof two, on_adu, NULL);
    CHECK(g_n == 2 && !memcmp(g_adu[1], a2, sizeof a2), "two in one segment: both");
    const uint8_t junk[] = {'h', 'i', 0x7F};
    g_n = 0;
    mbtcp_push(&acc, junk, sizeof junk, on_adu, NULL);
    mbtcp_push(&acc, a1, sizeof a1, on_adu, NULL);
    CHECK(g_n == 1 && !memcmp(g_adu[0], a1, sizeof a1), "garbage first: resynchronised on the next frame");
    const uint8_t big[] = {0, 3, 0, 0, 0x40, 0x00, 9, 3};  // length 16384: not Modbus TCP
    g_n = 0;
    mbtcp_push(&acc, big, sizeof big, on_adu, NULL);
    mbtcp_push(&acc, a2, sizeof a2, on_adu, NULL);
    CHECK(g_n == 1 && !memcmp(g_adu[0], a2, sizeof a2) && acc.len == 0, "an oversized length: dropped, the next frame comes in");
}

// ---- HTTP -----------------------------------------------------------------------------------------

static void test_http(void)
{
    char out[512];
    bridge_cfg_t c = cfg_of("{\"workMode\":4,\"httpHost\":\"web\",\"httpPort\":8080,\"httpMethod\":0,\"httpPath\":\"/data?v=\","
                            "\"httpHeaders\":\"k1: a\\nk2: b\\r\\n\\n\"}",
                            NULL);
    size_t n = http_head(&c, 1, out, sizeof out);
    CHECK(n == strlen("GET /data?v=") && !memcmp(out, "GET /data?v=", n), "GET: the path, the data follows in the URL");
    n = http_tail(&c, out, sizeof out);
    out[n] = 0;
    CHECK(!strcmp(out, " HTTP/1.0\r\nHost: web:8080\r\nk1: a\r\nk2: b\r\n\r\n"), "GET: the tail, headers normalised to CRLF (%s)", out);
    c = cfg_of("{\"workMode\":4,\"httpHost\":\"web\",\"httpPort\":80,\"httpMethod\":1,\"httpPath\":\"/in\",\"httpHeaders\":\"\"}", NULL);
    n = http_head(&c, 9, out, sizeof out);
    out[n] = 0;
    CHECK(!strcmp(out, "POST /in HTTP/1.0\r\nHost: web:80\r\nContent-Length: 9\r\n\r\n"), "POST: the head with the length (%s)", out);
    CHECK(http_tail(&c, out, sizeof out) == 0, "POST: no tail");
    const char resp[] = "HTTP/1.0 200 OK\r\nX: y\r\n\r\nOK-GET";
    size_t bl;
    const uint8_t *b = http_body((const uint8_t *)resp, sizeof resp - 1, true, &bl);
    CHECK(bl == 6 && !memcmp(b, "OK-GET", 6), "stripped: the body only");
    b = http_body((const uint8_t *)resp, sizeof resp - 1, false, &bl);
    CHECK(bl == sizeof resp - 1 && b == (const uint8_t *)resp, "not stripped: all of it");
    b = http_body((const uint8_t *)"HTTP/1.0 200 OK\r\nX: y", 21, true, &bl);
    CHECK(bl == 21, "no blank line: all of it, even stripped (as the ESP32)");
}

// ---- tx_progress ----------------------------------------------------------------------------------

static uint8_t g_wire[4096];
static size_t g_wire_len;
static int g_script[16], g_step;  // per call: >0 bytes taken at most, NET_AGAIN or NET_FAIL
static int fake_send(void *ctx, const uint8_t *data, size_t len)
{
    int r = g_script[g_step++];
    if (r <= 0)
        return r;
    size_t n = (size_t)r < len ? (size_t)r : len;
    memcpy(g_wire + g_wire_len, data, n);
    g_wire_len += n;
    return (int)n;
}

static void test_tx(void)
{
    uint8_t frame[100];
    for (int i = 0; i < 100; i++)
        frame[i] = (uint8_t)i;
    tx_conn_t c = {0};
    int s[] = {30, NET_AGAIN, NET_AGAIN, 40, NET_AGAIN, 1000};
    memcpy(g_script, s, sizeof s);
    g_step = 0;
    g_wire_len = 0;
    int r[6];
    for (int k = 0; k < 6; k++)
        r[k] = tx_progress(&c, frame, sizeof frame, fake_send, NULL, 1000 + k);
    CHECK(r[0] == TX_PENDING && r[2] == TX_PENDING && r[4] == TX_PENDING && r[5] == TX_DONE, "partial, again, ..., done");
    CHECK(g_wire_len == 100 && !memcmp(g_wire, frame, 100), "every byte once, in order (%u)", (unsigned)g_wire_len);
    CHECK(!c.busy && c.off == 0, "ready for the next frame");
    // NET_FAIL: lost.
    int f[] = {10, NET_FAIL};
    memcpy(g_script, f, sizeof f);
    g_step = 0;
    tx_progress(&c, frame, sizeof frame, fake_send, NULL, 5000);
    CHECK(tx_progress(&c, frame, sizeof frame, fake_send, NULL, 5001) == TX_LOST, "NET_FAIL: lost");
    // 10 s with no byte taken: lost; a byte at 9.9 s restarts the 10 s.
    c = (tx_conn_t){0};
    for (int k = 0; k < 16; k++)
        g_script[k] = NET_AGAIN;
    g_step = 0;
    CHECK(tx_progress(&c, frame, sizeof frame, fake_send, NULL, 0) == TX_PENDING, "again at 0 s: pending");
    g_step = 0;
    CHECK(tx_progress(&c, frame, sizeof frame, fake_send, NULL, 9999) == TX_PENDING, "again at 9.999 s: pending");
    g_step = 0;
    CHECK(tx_progress(&c, frame, sizeof frame, fake_send, NULL, 10000) == TX_LOST, "again at 10 s: lost");
    c = (tx_conn_t){0};
    g_step = 0;
    tx_progress(&c, frame, sizeof frame, fake_send, NULL, 0);
    g_script[0] = 5;
    g_step = 0;
    CHECK(tx_progress(&c, frame, sizeof frame, fake_send, NULL, 9900) == TX_PENDING, "5 bytes at 9.9 s");
    g_script[0] = NET_AGAIN;
    g_step = 0;
    CHECK(tx_progress(&c, frame, sizeof frame, fake_send, NULL, 19000) == TX_PENDING, "again at 19 s: pending (9.1 s since)");
    g_step = 0;
    CHECK(tx_progress(&c, frame, sizeof frame, fake_send, NULL, 19900) == TX_LOST, "again at 19.9 s: lost");
    // net_classify, with the errno names lwIP uses (LWIP_ERRNO_INCLUDE <errno.h>)
    CHECK(net_classify(EWOULDBLOCK) == NET_AGAIN && net_classify(EAGAIN) == NET_AGAIN && net_classify(ENOMEM) == NET_AGAIN &&
              net_classify(ENOBUFS) == NET_AGAIN,
          "EWOULDBLOCK, EAGAIN, ENOMEM, ENOBUFS: again");
    CHECK(net_classify(ECONNRESET) == NET_FAIL && net_classify(ENOTCONN) == NET_FAIL && net_classify(EPIPE) == NET_FAIL &&
              net_classify(ECONNABORTED) == NET_FAIL && net_classify(EBADF) == NET_FAIL,
          "ECONNRESET, ENOTCONN, EPIPE, ECONNABORTED, EBADF: fail");
    CHECK(!accept_allowed(3, 4) && accept_allowed(4, 4) && accept_allowed(5, 4), "accept while at least 4 netconns are free");
    // idleRestart, with an activity stamped between the two reads (Codex code review r1)
    const uint64_t L = 60000000u;
    CHECK(!idle_due(100000000u, 100000005u, L), "an activity newer than now: not idle (no unsigned wrap)");
    CHECK(!idle_due(159999999u, 100000000u, L) && idle_due(160000000u, 100000000u, L), "idle after 60 s exactly");
    // 64-bit waits: no wrap around 24.85 or 49.71 days of uptime
    const uint64_t d2485 = 2147483648ull * 1000u, d4971 = 4294967296ull * 1000u;
    CHECK(wait_over(d2485 + 5, 0) && wait_over(d4971 + 5, 0), "no wait (0) is over at 24.85 and 49.71 days");
    CHECK(!wait_over(d2485, d2485 + 2000000u) && wait_over(d2485 + 2000000u, d2485 + 2000000u), "a 2 s wait across 24.85 days");
}

int main(void)
{
    test_cfg();
    test_mbtcp();
    test_http();
    test_tx();
    printf("test_bridge_core: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
