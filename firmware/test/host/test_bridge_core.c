#include <limits.h>
// Host tests for the bridge logic (spec §6.4, §5.6, Plan 08a): make -C stm32/firmware/test/host
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bridge_core.h"
#include "modbus.h"

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

// Plan 08b: bytes inside mbedTLS count as ready, or the bridge never reads them (Codex plan r1).
static void test_wait_ready(void)
{
    CHECK(wait_ready(0, 1) == 1, "select saw nothing, one TLS client holds bytes: 1");
    CHECK(wait_ready(-1, 1) == 1, "select failed, one holds bytes: still 1");
    CHECK(wait_ready(2, 0) == 2, "select's own count");
    CHECK(wait_ready(0, 0) == 0, "nothing");
    CHECK(wait_ready(1, 1) == 2, "both");
}

// ---- a reply while a frame is in flight (Codex 08b code review r1) --------------------------------
typedef struct {
    uint8_t out[128];
    size_t n;
    int calls;
    bool stuck;  // never takes a byte
} sink_t;

static int sink_send(void *ctx, const uint8_t *data, size_t len)
{
    sink_t *k = ctx;
    if (k->stuck || k->calls++ % 2)
        return NET_AGAIN;  // every other try: the socket (or mbedTLS) is full
    size_t m = len < 3 ? len : 3;
    memcpy(k->out + k->n, data, m);
    k->n += m;
    return (int)m;
}

static uint32_t s_fake_ms;
static uint32_t fake_now(void) { return s_fake_ms; }
static void fake_wait(void *ctx)
{
    (void)ctx;
    s_fake_ms += 10;
}

static void test_tx_finish(void)
{
    const uint8_t frame[] = "FRAME-0123456789";
    sink_t k = {0};
    tx_conn_t tx = {0};
    s_fake_ms = 0;
    CHECK(tx_progress(&tx, frame, sizeof frame - 1, sink_send, &k, 0) == TX_PENDING && tx.busy, "a frame half sent");
    CHECK(tx_finish(&tx, frame, sizeof frame - 1, sink_send, &k, fake_now, fake_wait, 2000) == TX_DONE && !tx.busy,
          "the frame is finished before the reply");
    const uint8_t reply[] = "REPLY";
    for (size_t off = 0; off < sizeof reply - 1;) {
        int r = sink_send(&k, reply + off, sizeof reply - 1 - off);
        if (r > 0)
            off += (size_t)r;
    }
    CHECK(k.n == 21 && !memcmp(k.out, "FRAME-0123456789REPLY", 21), "one stream: the frame whole, then the reply (%.*s)",
          (int)k.n, (const char *)k.out);
    sink_t idle = {0};
    tx_conn_t none = {0};
    CHECK(tx_finish(&none, frame, sizeof frame - 1, sink_send, &idle, fake_now, fake_wait, 2000) == TX_DONE && idle.calls == 0,
          "no frame in flight: nothing to finish, nothing sent");
    sink_t stuck = {.stuck = true};
    tx_conn_t t2 = {0};
    s_fake_ms = 0;
    tx_progress(&t2, frame, sizeof frame - 1, sink_send, &stuck, 0);
    CHECK(tx_finish(&t2, frame, sizeof frame - 1, sink_send, &stuck, fake_now, fake_wait, 2000) == TX_LOST && s_fake_ms >= 2000 &&
              s_fake_ms <= 2020,
          "a peer that takes nothing: lost after the reply's 2 s (%u ms)", (unsigned)s_fake_ms);
}

// ---- Plan 08c: Edge remote access framing, the report slot ---------------------------------------
typedef struct {
    uint8_t reqs[8][64];
    size_t req_len[8];
    int nreq;
    uint8_t passed[256];
    size_t npassed;
    bool answer;  // what req answers
} fp_t;

static bool fp_req(void *ctx, const uint8_t *req, size_t len)
{
    fp_t *f = ctx;
    memcpy(f->reqs[f->nreq], req, len);
    f->req_len[f->nreq++] = len;
    return f->answer;
}

static void fp_pass(void *ctx, const uint8_t *data, size_t len)
{
    fp_t *f = ctx;
    memcpy(f->passed + f->npassed, data, len);
    f->npassed += len;
}

static void test_frame_push(void)
{
    uint8_t rtu[8] = {5, 3, 0, 10, 0, 2};
    size_t n = 6 + 2;
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < 6; i++) {
        crc ^= rtu[i];
        for (int k = 0; k < 8; k++)
            crc = crc & 1 ? (uint16_t)(crc >> 1 ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    rtu[6] = (uint8_t)crc, rtu[7] = (uint8_t)(crc >> 8);
    mbtcp_acc_t a = {0};
    fp_t f = {.answer = true};
    frame_push(&a, rtu, 3, false, fp_req, fp_pass, &f);
    frame_push(&a, rtu + 3, 3, false, fp_req, fp_pass, &f);
    CHECK(f.nreq == 0 && a.len == 6, "an RTU request in pieces: held");
    frame_push(&a, rtu + 6, 2, false, fp_req, fp_pass, &f);
    CHECK(f.nreq == 1 && f.req_len[0] == n && !memcmp(f.reqs[0], rtu, n) && a.len == 0 && f.npassed == 0, "then given whole");
    uint8_t two[16];
    memcpy(two, rtu, 8), memcpy(two + 8, rtu, 8);
    fp_t g = {.answer = true};
    frame_push(&a, two, 16, false, fp_req, fp_pass, &g);
    CHECK(g.nreq == 2 && a.len == 0, "two in one segment: both");
    fp_t h = {.answer = false};
    frame_push(&a, rtu, 8, false, fp_req, fp_pass, &h);
    CHECK(h.nreq == 1 && h.npassed == 8 && !memcmp(h.passed, rtu, 8), "not Edge's: it goes on whole");
    fp_t j = {.answer = true};
    frame_push(&a, (const uint8_t *)"hello", 5, false, fp_req, fp_pass, &j);
    CHECK(j.nreq == 0 && j.npassed == 5 && a.len == 0, "not a request: it goes on as it is");
    uint8_t mb[12];
    const uint8_t pdu[] = {1, 3, 0, 0, 0, 1};
    size_t m = mb_tcp_wrap(pdu, sizeof pdu, 1, mb, sizeof mb);
    fp_t k = {.answer = true};
    frame_push(&a, mb, 5, true, fp_req, fp_pass, &k);
    frame_push(&a, mb + 5, m - 5, true, fp_req, fp_pass, &k);
    CHECK(k.nreq == 1 && k.req_len[0] == m, "an MBAP request in two pieces");
}

static void test_acc_stale(void)
{
    mbtcp_acc_t a = {0};
    CHECK(!acc_stale(&a, 10000000), "empty: never stale");
    a.len = 3;
    acc_stamp(&a, 1000000);
    CHECK(!acc_stale(&a, 1000000 + 199000) && acc_stale(&a, 1000000 + 200000), "200 ms after its byte, not 199");
    acc_stamp(&a, 1000000 + 150000);  // more bytes at 150 ms
    CHECK(!acc_stale(&a, 1000000 + 210000) && acc_stale(&a, 1000000 + 350000), "silence counts from the last byte");
}

static void test_report_accept(void)
{
    bridge_cfg_t c = cfg_of("{\"workMode\":3}", NULL);
    CHECK(!report_accept(&c, false, true), "a bridge that does not run");
    CHECK(!report_accept(&c, true, false) && report_accept(&c, true, true), "TCP server: only with a client");
    c = cfg_of("{\"workMode\":1}", NULL);
    CHECK(!report_accept(&c, true, false), "TCP client: not while away");
    c = cfg_of("{\"workMode\":1,\"socketMode\":2}", NULL);
    CHECK(report_accept(&c, true, false), "TCP client, short connection: it dials for the report");
    c = cfg_of("{\"workMode\":4}", NULL);
    CHECK(report_accept(&c, true, false), "HTTP client: a request for the report");
    c = cfg_of("{\"workMode\":1,\"socketMode\":1}", NULL);
    CHECK(!report_accept(&c, true, true), "a Modbus TCP client: never");
    c = cfg_of("{\"workMode\":0}", NULL);
    CHECK(!report_accept(&c, true, false) && report_accept(&c, true, true), "UDP: with its peer");
    CHECK(report_take(1, false) && !report_take(1, true) && !report_take(2, false) && !report_take(0, false),
          "the report goes when offered and nothing is in flight, once");
}

// Plan 10c task 3 (Codex review, Important 3): a workMode outside 0..4, as a hand-made config may carry,
// names "unknown" instead of reading before or past the table.
static void test_mode_name(void)
{
    static const char *const want[] = {"UDP client", "TCP client", "UDP server", "TCP server", "HTTP client"};
    for (int i = 0; i < 5; i++) CHECK(!strcmp(bridge_mode_name(i), want[i]), "mode %d: %s", i, bridge_mode_name(i));
    const int bad[] = {-1, 5, INT_MIN, INT_MAX};
    for (int i = 0; i < 4; i++) CHECK(!strcmp(bridge_mode_name(bad[i]), "unknown"), "mode %d: unknown", bad[i]);
}

int main(void)
{
    test_mode_name();
    test_frame_push();
    test_acc_stale();
    test_report_accept();
    test_wait_ready();
    test_tx_finish();
    test_cfg();
    test_mbtcp();
    test_http();
    test_tx();
    printf("test_bridge_core: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
