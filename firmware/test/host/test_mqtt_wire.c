// Host tests for the coreMQTT patch's MQTT 3.1 part (spec §8, Plan 09 ruling 1): CONNECT byte for byte in 3.1
// and 3.1.1, and the 3.1 session rule (no Session Present in its CONNACK). make -C stm32/firmware/test/host
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "core_mqtt.h"

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

void coremqtt_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("  coreMQTT: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

// ---- a transport that records what is sent and serves canned bytes ----------------------------------

struct NetworkContext {
    uint8_t out[512];
    size_t nout;
    const uint8_t *in;
    size_t nin, pos;
};

static int32_t tx(NetworkContext_t *c, const void *buf, size_t n)
{
    memcpy(c->out + c->nout, buf, n);
    c->nout += n;
    return (int32_t)n;
}

static int32_t rx(NetworkContext_t *c, void *buf, size_t n)
{
    size_t k = c->nin - c->pos < n ? c->nin - c->pos : n;
    memcpy(buf, c->in + c->pos, k);
    c->pos += k;
    return (int32_t)k;
}

static uint32_t ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void on_event(MQTTContext_t *c, MQTTPacketInfo_t *p, MQTTDeserializedInfo_t *d)
{
    (void)c, (void)p, (void)d;
}

/** One MQTT_Connect against a canned CONNACK: the status, the bytes sent, and Session Present. */
static MQTTStatus_t connect(bool mqtt31, bool clean, const uint8_t connack[4], NetworkContext_t *nc, bool *sp)
{
    static uint8_t netbuf[1024];
    MQTTContext_t ctx;
    TransportInterface_t t = {.recv = rx, .send = tx, .pNetworkContext = nc};
    MQTTFixedBuffer_t fb = {.pBuffer = netbuf, .size = sizeof netbuf};
    memset(nc->out, 0, sizeof nc->out);
    nc->nout = 0;
    nc->in = connack;
    nc->nin = 4;
    nc->pos = 0;
    MQTT_Init(&ctx, &t, ms, on_event, &fb);
    MQTTConnectInfo_t ci = {.cleanSession = clean, .keepAliveSeconds = 60, .pClientIdentifier = "gw1",
                            .clientIdentifierLength = 3, .pUserName = "u", .userNameLength = 1, .pPassword = "pw",
                            .passwordLength = 2, .mqtt31 = mqtt31};
    MQTTPublishInfo_t will = {.qos = MQTTQoS1, .retain = true, .pTopicName = "w", .topicNameLength = 1,
                              .pPayload = "bye", .payloadLength = 3};
    *sp = false;
    return MQTT_Connect(&ctx, &ci, &will, 1000, sp);
}

static void test_connect_bytes(void)
{
    const uint8_t ok[4] = {0x20, 2, 0, 0};
    NetworkContext_t nc;
    bool sp;
    // 3.1.1: "MQTT" level 4. Flags: user, password, will retain, will QoS 1, will, clean = 0xEE.
    CHECK(connect(false, true, ok, &nc, &sp) == MQTTSuccess, "3.1.1 connects");
    const uint8_t v311[] = {0x10, 30, 0, 4, 'M', 'Q', 'T', 'T', 4, 0xEE, 0, 60, 0, 3, 'g', 'w', '1', 0, 1, 'w', 0, 3, 'b', 'y', 'e',
                            0, 1, 'u', 0, 2, 'p', 'w'};
    CHECK(nc.nout == sizeof v311 && !memcmp(nc.out, v311, sizeof v311), "3.1.1 CONNECT byte for byte (%zu bytes)", nc.nout);
    // 3.1: "MQIsdp" level 3, Remaining Length 2 bytes longer, the rest the same.
    CHECK(connect(true, true, ok, &nc, &sp) == MQTTSuccess, "3.1 connects");
    const uint8_t v31[] = {0x10, 32, 0, 6, 'M', 'Q', 'I', 's', 'd', 'p', 3, 0xEE, 0, 60, 0, 3, 'g', 'w', '1', 0, 1, 'w', 0, 3, 'b', 'y',
                           'e', 0, 1, 'u', 0, 2, 'p', 'w'};
    CHECK(nc.nout == sizeof v31 && !memcmp(nc.out, v31, sizeof v31), "3.1 CONNECT byte for byte (%zu bytes)", nc.nout);
}

static void test_session_rule(void)
{
    NetworkContext_t nc;
    bool sp;
    const uint8_t reserved[4] = {0x20, 2, 0x5A, 0};  // 3.1: the first byte is reserved, whatever it holds
    CHECK(connect(true, false, reserved, &nc, &sp) == MQTTSuccess && sp, "3.1, cleanSession 0: the session is kept");
    CHECK(connect(true, true, reserved, &nc, &sp) == MQTTSuccess && !sp, "3.1, cleanSession 1: a new session");
    const uint8_t sp1[4] = {0x20, 2, 1, 0}, sp0[4] = {0x20, 2, 0, 0};
    CHECK(connect(false, false, sp1, &nc, &sp) == MQTTSuccess && sp, "3.1.1: Session Present 1");
    CHECK(connect(false, false, sp0, &nc, &sp) == MQTTSuccess && !sp, "3.1.1: Session Present 0, a new session");
    CHECK(connect(false, false, reserved, &nc, &sp) == MQTTBadResponse, "3.1.1 still checks the reserved bits");
    const uint8_t refused[4] = {0x20, 2, 0, 5};
    CHECK(connect(true, true, refused, &nc, &sp) == MQTTServerRefused, "3.1: a refusal is still a refusal");
}

int main(void)
{
    test_connect_bytes();
    test_session_rule();
    printf("test_mqtt_wire: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
