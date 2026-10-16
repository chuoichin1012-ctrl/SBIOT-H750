// Host tests for the MQTT session (spec §8, Plan 09 rulings 3 and 11): the real coreMQTT 2.3.1 (with the SBIOT patch),
// the outbox and mqtt_sess against a scripted broker in a thread, over socketpairs, with one context for the whole
// run as on the board. make -C stm32/firmware/test/host
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "bridge_core.h"
#include "core_mqtt.h"
#include "mqtt_sess.h"
#include "mqtt_transport.h"

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

static int s_quiet;
void coremqtt_log(const char *fmt, ...)
{
    if (s_quiet)
        return;
    va_list ap;
    va_start(ap, fmt);
    printf("  coreMQTT: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

// ---- the scripted broker ------------------------------------------------------------------------------

enum { DROP_NONE, DROP_BEFORE_PUBACK, DROP_BEFORE_PUBREC, DROP_BEFORE_PUBCOMP, DROP_MID_PUBLISH, HOLD_ACKS };
typedef struct {
    int fd;
    bool session_present;
    int drop;               // what this connection does
    // across connections: the broker's session (QoS 2 ids received, not yet released)
    uint16_t held[64];
    int nheld;
    // what arrived
    int delivered[64];      // per payload number "m<k>": deliveries to subscribers
    uint16_t ids[256];
    bool dups[256];
    int npub;
    int pubrels;
    size_t big;             // a large payload's bytes
    // oversized test: what to send the client, and what came back
    int send_big_qos;       // 0: nothing
    uint8_t acks[16];
    int nacks;
    bool closed;
    pthread_t th;
} broker_t;

static bool rd(int fd, uint8_t *b, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, b + got, n - got, 0);
        if (r <= 0)
            return false;
        got += (size_t)r;
    }
    return true;
}

static void wr(int fd, const uint8_t *b, size_t n) { (void)!send(fd, b, n, MSG_NOSIGNAL); }

static int packet(int fd, uint8_t *type, uint8_t *body, size_t max, size_t *len)
{
    uint8_t h;
    if (!rd(fd, &h, 1))
        return -1;
    size_t rl = 0, mul = 1;
    uint8_t c;
    do {
        if (!rd(fd, &c, 1))
            return -1;
        rl += (c & 0x7F) * mul;
        mul *= 128;
    } while (c & 0x80);
    *type = h;
    *len = rl;
    if (rl > max) {  // a big PUBLISH: keep its head only
        if (!rd(fd, body, max))
            return -1;
        static uint8_t sink[4096];
        for (size_t left = rl - max; left;) {
            size_t k = left < sizeof sink ? left : sizeof sink;
            if (!rd(fd, sink, k))
                return -1;
            left -= k;
        }
        return 1;
    }
    return rd(fd, body, rl) ? 0 : -1;
}

static void ack(int fd, uint8_t type, uint16_t id)
{
    uint8_t a[4] = {type, 2, (uint8_t)(id >> 8), (uint8_t)id};
    wr(fd, a, 4);
}

static void *broker_run(void *arg)
{
    broker_t *b = arg;
    static uint8_t body[8192];
    uint8_t t;
    size_t len;
    int pubs = 0;
    while (packet(b->fd, &t, body, sizeof body, &len) >= 0) {
        int type = t >> 4;
        if (type == 1) {  // CONNECT
            uint8_t ca[4] = {0x20, 2, b->session_present, 0};
            wr(b->fd, ca, 4);
            if (b->drop == DROP_MID_PUBLISH) {  // 2000 bytes of the 20 KB PUBLISH, then the connection drops
                static uint8_t part[2000];
                (void)rd(b->fd, part, sizeof part);
                break;
            }
            if (!b->session_present)
                b->nheld = 0;
            if (b->send_big_qos) {  // a PUBLISH of 6 KB to the client, then a small one
                static uint8_t big[6200];
                size_t n = 0;
                uint8_t q = (uint8_t)b->send_big_qos;
                big[n++] = (uint8_t)(0x30 | q << 1);
                size_t rl = 2 + 3 + 2 + 6000;
                big[n++] = (uint8_t)(0x80 | (rl & 0x7F));
                big[n++] = (uint8_t)(rl >> 7);
                big[n++] = 0, big[n++] = 3, memcpy(big + n, "big", 3), n += 3;
                big[n++] = 0, big[n++] = 77;  // packet id 77
                memset(big + n, 'B', 6000), n += 6000;
                wr(b->fd, big, n);
                const uint8_t small[] = {0x30, 2 + 5 + 2, 0, 5, 's', 'm', 'a', 'l', 'l', 'h', 'i'};
                wr(b->fd, small, sizeof small);
            }
        } else if (type == 3) {  // PUBLISH from the client
            int qos = (t >> 1) & 3;
            bool dup = t & 8;
            size_t tl = (size_t)body[0] << 8 | body[1];
            uint16_t id = qos ? (uint16_t)(body[2 + tl] << 8 | body[3 + tl]) : 0;
            const uint8_t *pl = body + 2 + tl + (qos ? 2 : 0);
            size_t plen = len - 2 - tl - (qos ? 2 : 0);
            if (b->npub < 256)
                b->ids[b->npub] = id, b->dups[b->npub] = dup, b->npub++;
            pubs++;
            if (plen > 4096)
                b->big = plen;
            int k = pl[0] == 'm' ? atoi((const char *)pl + 1) : -1;
            if (b->drop == DROP_BEFORE_PUBACK && qos == 1 && pubs == 2)
                break;
            if (b->drop == DROP_BEFORE_PUBREC && qos == 2 && pubs == 2) {
                // the broker received it (a QoS 2 delivery happens now) but the PUBREC never leaves
                bool seen = false;
                for (int i = 0; i < b->nheld; i++)
                    seen |= b->held[i] == id;
                if (!seen && k >= 0)
                    b->delivered[k]++, b->held[b->nheld++] = id;
                break;
            }
            if (qos == 2) {
                bool seen = false;
                for (int i = 0; i < b->nheld; i++)
                    seen |= b->held[i] == id;
                if (!seen && k >= 0)
                    b->delivered[k]++, b->held[b->nheld++] = id;
                ack(b->fd, 0x50, id);  // PUBREC
            } else {
                if (k >= 0)
                    b->delivered[k]++;
                if (qos == 1 && b->drop != HOLD_ACKS)
                    ack(b->fd, 0x40, id);  // PUBACK
            }
        } else if (type == 6) {  // PUBREL
            uint16_t id = (uint16_t)(body[0] << 8 | body[1]);
            b->pubrels++;
            if (b->drop == DROP_BEFORE_PUBCOMP && b->pubrels == 1)
                break;
            for (int i = 0; i < b->nheld; i++)
                if (b->held[i] == id)
                    b->held[i] = b->held[--b->nheld];
            ack(b->fd, 0x70, id);  // PUBCOMP
        } else if (type == 4 || type == 5 || type == 7) {  // the client's PUBACK, PUBREC, PUBCOMP
            if (b->nacks < 16)
                b->acks[b->nacks++] = t;
            if (type == 5)
                ack(b->fd, 0x62, (uint16_t)(body[0] << 8 | body[1]));  // PUBREL
        } else if (type == 8) {  // SUBSCRIBE
            uint8_t sa[5] = {0x90, 3, body[0], body[1], 0};
            wr(b->fd, sa, 5);
        } else if (type == 12) {
            uint8_t pr[2] = {0xD0, 0};
            wr(b->fd, pr, 2);
        }
    }
    shutdown(b->fd, SHUT_RDWR);
    close(b->fd);
    b->closed = true;
    return NULL;
}

// ---- the client side: POSIX ops for mqtt_transport -----------------------------------------------------

static int p_send(void *c, const uint8_t *d, size_t n);
static const uint8_t *s_region_lo, *s_region_hi;
static bool s_sent_from_region;
static int p_send(void *c, const uint8_t *d, size_t n)
{
    if (d >= s_region_lo && d < s_region_hi)
        s_sent_from_region = true;
    ssize_t r = send(*(int *)c, d, n, MSG_DONTWAIT | MSG_NOSIGNAL);
    return r > 0 ? (int)r : r == 0 ? NET_AGAIN : net_classify(errno);
}
static int p_recv(void *c, uint8_t *b, size_t n)
{
    ssize_t r = recv(*(int *)c, b, n, MSG_DONTWAIT);
    return r >= 0 ? (int)r : net_classify(errno);
}
static bool p_pending(void *c)
{
    (void)c;
    return false;
}
static int p_wait(void *c, bool write, uint32_t ms)
{
    int fd = *(int *)c;
    fd_set s;
    FD_ZERO(&s);
    FD_SET(fd, &s);
    struct timeval tv = {0, (long)ms * 1000};
    return select(fd + 1, write ? NULL : &s, write ? &s : NULL, NULL, &tv);
}
static void p_tick(void) { usleep(1000); }
static bool s_lock_fails;
static bool lock(void) { return !s_lock_fails; }
static void unlock(void) {}

// ---- one client for the whole test, as the board's ----------------------------------------------------

static MQTTContext_t s_ctx;
static uint8_t s_netbuf[4096];
static MQTTPubAckInfo_t s_out[16], s_in[16];
static int s_fd = -1;
static mqtt_link_t s_link = {.send = p_send, .recv = p_recv, .pending = p_pending, .wait = p_wait, .tick = p_tick, .conn = &s_fd};
static NetworkContext_t s_nc = {.link = &s_link};
static ob_t s_ob;
static uint8_t s_obmem[64 * 1024];
static subs_t s_subs;
static mqtt_sess_t s_sess = {.ctx = &s_ctx, .ob = &s_ob, .subs = &s_subs, .lock = lock, .unlock = unlock, .now_ms = now_ms};
static char s_got[16][16];
static int s_ngot;

static void on_event(MQTTContext_t *c, MQTTPacketInfo_t *p, MQTTDeserializedInfo_t *d)
{
    (void)c;
    if ((p->type & 0xF0) == MQTT_PACKET_TYPE_PUBLISH) {
        if (s_ngot < 16)
            snprintf(s_got[s_ngot++], 16, "%.*s", (int)d->pPublishInfo->payloadLength, (const char *)d->pPublishInfo->pPayload);
    } else {
        ms_ack(&s_sess, p->type, d->packetIdentifier);
    }
}

static void client_init(void)
{
    TransportInterface_t t = {.recv = mqtt_rx, .send = mqtt_tx, .pNetworkContext = &s_nc};
    MQTTFixedBuffer_t fb = {.pBuffer = s_netbuf, .size = sizeof s_netbuf};
    MQTT_Init(&s_ctx, &t, now_ms, on_event, &fb);
    MQTT_InitStatefulQoS(&s_ctx, s_out, 16, s_in, 16);
    ob_init(&s_ob, s_obmem, sizeof s_obmem);
    s_region_lo = s_obmem, s_region_hi = s_obmem + sizeof s_obmem;
}

/** A new connection to the broker `b` (its fd from a fresh socketpair): MQTT_Connect, then ms_resume. */
static bool connect_to(broker_t *b, bool clean, bool *kept)
{
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    int sz = 8192;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
    setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);
    fcntl(sv[0], F_SETFL, O_NONBLOCK);
    s_fd = sv[0];
    b->fd = sv[1];
    b->closed = false;
    pthread_create(&b->th, NULL, broker_run, b);
    MQTTConnectInfo_t ci = {.cleanSession = clean, .keepAliveSeconds = 60, .pClientIdentifier = "t", .clientIdentifierLength = 1};
    bool sp = false;
    MQTTStatus_t r = MQTT_Connect(&s_ctx, &ci, NULL, 2000, &sp);
    *kept = sp;
    return r == MQTTSuccess && ms_resume(&s_sess, sp) == MQTTSuccess;
}

static void disconnect(broker_t *b)
{
    close(s_fd);
    s_fd = -1;
    pthread_join(b->th, NULL);
    s_ctx.connectStatus = MQTTNotConnected;
}

/** Runs the loop until the broker hangs up or `ms` passed: ms_feed's error, or MQTTSuccess. */
static MQTTStatus_t run(broker_t *b, int ms)
{
    uint32_t end = now_ms() + (uint32_t)ms;
    while (now_ms() < end && !b->closed) {
        MQTTStatus_t f = ms_feed(&s_sess);
        if (f != MQTTSuccess)
            return f;
        MQTTStatus_t r = MQTT_ProcessLoop(&s_ctx);
        if (r != MQTTSuccess && r != MQTTNeedMoreBytes)
            break;
    }
    return MQTTSuccess;
}

static void push_m(int k, int qos)
{
    char p[8];
    int n = snprintf(p, sizeof p, "m%d", k);
    ob_push(&s_ob, "t", 1, p, (size_t)n, qos, false, now_ms());
}

// ---- cases ---------------------------------------------------------------------------------------------

static void phase(const char *what, int drop, int qos, bool clean)
{
    broker_t b = {.drop = drop, .session_present = false};
    bool kept;
    CHECK(connect_to(&b, clean, &kept), "%s: first connect", what);
    for (int k = 0; k < 4; k++)
        push_m(k, qos);
    run(&b, 1500);  // the broker drops at its phase
    disconnect(&b);
    uint16_t first_ids[4] = {0};
    for (int i = 0; i < b.npub && i < 4; i++)
        first_ids[i] = b.ids[i];
    int before = b.npub;
    b.drop = DROP_NONE;
    b.session_present = !clean;  // a kept session when it was asked for, a new one otherwise
    CHECK(connect_to(&b, clean, &kept) && kept == !clean, "%s: second connect, session %s", what, clean ? "new" : "kept");
    run(&b, 1500);
    disconnect(&b);
    int lost = 0, twice = 0;
    for (int k = 0; k < 4; k++) {
        lost += b.delivered[k] == 0;
        twice += b.delivered[k] > 1;
    }
    CHECK(!lost, "%s: nothing lost (%d %d %d %d)", what, b.delivered[0], b.delivered[1], b.delivered[2], b.delivered[3]);
    if (!clean && qos == 2)
        CHECK(!twice, "%s: a kept session never duplicates QoS 2", what);
    bool dup_same = false, fresh = true;
    for (int i = before; i < b.npub; i++) {
        for (int j = 0; j < before && j < 4; j++)
            if (b.ids[i] == first_ids[j] && b.dups[i])
                dup_same = true;
        if (b.dups[i])
            fresh = false;
    }
    if (!clean && drop != DROP_BEFORE_PUBCOMP)
        CHECK(dup_same, "%s: resent with DUP and its id", what);
    if (clean)
        CHECK(fresh, "%s: a new session sends again without DUP, new ids", what);
    CHECK(ob_in_flight(&s_ob) == 0 && ob_used(&s_ob) == 0, "%s: the outbox empty", what);
}

static void test_phases(void)
{
    phase("QoS 1, drop before PUBACK, kept", DROP_BEFORE_PUBACK, 1, false);
    phase("QoS 1, drop before PUBACK, new", DROP_BEFORE_PUBACK, 1, true);
    phase("QoS 2, drop before PUBREC, kept", DROP_BEFORE_PUBREC, 2, false);
    phase("QoS 2, drop before PUBREC, new", DROP_BEFORE_PUBREC, 2, true);
    phase("QoS 2, drop before PUBCOMP, kept", DROP_BEFORE_PUBCOMP, 2, false);
    phase("QoS 2, drop before PUBCOMP, new", DROP_BEFORE_PUBCOMP, 2, true);
}

static void test_window(void)
{
    broker_t b = {.drop = HOLD_ACKS};
    bool kept;
    connect_to(&b, true, &kept);
    for (int k = 20; k < 40; k++)
        push_m(k, 1);
    run(&b, 800);
    CHECK(b.npub == 16 && ob_in_flight(&s_ob) == 16, "20 queued, no acks: 16 in flight, no more (%d)", b.npub);
    disconnect(&b);
    b.drop = DROP_NONE;
    connect_to(&b, true, &kept);  // a new session: all 20 go again
    run(&b, 1500);
    disconnect(&b);
    int got = 0;
    for (int k = 20; k < 40; k++)
        got += b.delivered[k % 64] > 0;
    CHECK(got == 20 && ob_used(&s_ob) == 0, "then all 20 delivered (%d)", got);
}

static void test_big_and_ids(void)
{
    broker_t b = {0};
    bool kept;
    connect_to(&b, true, &kept);
    static uint8_t big[26 * 1024];
    memset(big, 'R', sizeof big);
    s_sent_from_region = false;
    ob_push(&s_ob, "edge/report", 11, big, sizeof big, 1, false, now_ms());
    run(&b, 1500);
    CHECK(b.big == sizeof big && s_sent_from_region, "a 26 KB payload, sent straight from its record (%zu)", b.big);
    uint16_t last = b.npub ? b.ids[b.npub - 1] : 0;
    disconnect(&b);
    connect_to(&b, true, &kept);
    push_m(50, 1);
    run(&b, 800);
    disconnect(&b);
    CHECK(b.npub && b.ids[b.npub - 1] != 1 && b.ids[b.npub - 1] > last, "packet ids go on across reconnects (%u after %u)",
          b.npub ? b.ids[b.npub - 1] : 0, last);
}

static void test_mid_publish(void)
{
    static uint8_t big[20 * 1024];
    memset(big, 'M', sizeof big);
    for (int clean = 0; clean <= 1; clean++) {
        broker_t b = {.drop = DROP_MID_PUBLISH};
        bool kept;
        connect_to(&b, clean, &kept);
        ob_push(&s_ob, "t", 1, big, sizeof big, 1, false, now_ms());
        push_m(60, 1);
        MQTTStatus_t r = run(&b, 1500);
        disconnect(&b);
        ob_msg_t *held = ob_next_resend(&s_ob, NULL);
        uint16_t id = held ? held->id : 0;
        CHECK(r == MQTTSendFailed && b.npub == 0 && held && held->len == sizeof big && id,
              "%s: the PUBLISH failed after 2000 bytes, and keeps its id (%s, %u)", clean ? "new" : "kept", MQTT_Status_strerror(r), id);
        int before = b.npub;
        b.drop = DROP_NONE;
        b.session_present = !clean;
        connect_to(&b, clean, &kept);
        run(&b, 1500);
        disconnect(&b);
        bool same = false;
        for (int i = before; i < b.npub; i++)
            same |= b.ids[i] == id && b.dups[i];
        CHECK(b.big == sizeof big && b.delivered[60], "a drop in the middle of a PUBLISH (%s): resent whole, the one behind it too",
              clean ? "new" : "kept");
        if (!clean)
            CHECK(same, "kept: the same id with DUP");
        CHECK(ob_used(&s_ob) == 0, "the outbox empty");
    }
}

static void test_lock_fails(void)
{
    broker_t b = {0};
    bool kept;
    connect_to(&b, true, &kept);
    push_m(44, 1);
    s_lock_fails = true;
    MQTTStatus_t r = ms_feed(&s_sess);
    s_lock_fails = false;
    CHECK(r != MQTTSuccess && ob_next_queued(&s_ob, now_ms()), "the mutex not taken: nothing done, the message stays queued (%s)",
          MQTT_Status_strerror(r));
    run(&b, 800);
    disconnect(&b);
    CHECK(b.delivered[44] == 1 && ob_used(&s_ob) == 0, "then sent once the mutex is free");
}

static void test_id_wrap(void)
{
    broker_t b = {.drop = HOLD_ACKS};
    bool kept;
    connect_to(&b, true, &kept);
    push_m(45, 1);
    run(&b, 500);
    uint16_t held = b.npub ? b.ids[0] : 0;
    s_ctx.nextPacketId = held;  // the counter wrapped round onto an id still waiting for its PUBACK
    push_m(46, 1);
    run(&b, 500);
    disconnect(&b);
    CHECK(b.npub == 2 && b.ids[1] != held && b.delivered[46], "a wrapped packet id skips the one in use (%u, %u)", held,
          b.npub > 1 ? b.ids[1] : 0);
    b.drop = DROP_NONE;
    connect_to(&b, true, &kept);
    run(&b, 800);
    disconnect(&b);
    CHECK(ob_used(&s_ob) == 0, "the outbox empty");
}

static void test_oversized(void)
{
    for (int q = 1; q <= 2; q++) {
        broker_t b = {.send_big_qos = q};
        bool kept;
        s_ngot = 0;
        s_quiet = 1;
        connect_to(&b, true, &kept);
        run(&b, 1000);
        s_quiet = 0;
        disconnect(&b);
        bool puback = false, pubrec = false, pubcomp = false;
        for (int i = 0; i < b.nacks; i++) {
            puback |= b.acks[i] >> 4 == 4;
            pubrec |= b.acks[i] >> 4 == 5;
            pubcomp |= b.acks[i] >> 4 == 7;
        }
        if (q == 1)
            CHECK(puback, "an oversized QoS 1 message still gets its PUBACK");
        else
            CHECK(pubrec && pubcomp, "an oversized QoS 2 message: PUBREC, then PUBCOMP on PUBREL (%d %d)", pubrec, pubcomp);
        CHECK(s_ngot == 1 && !strcmp(s_got[0], "hi"), "QoS %d: the dropped payload never reaches the application, the next message does", q);
    }
}

int main(void)
{
    client_init();
    test_phases();
    test_window();
    test_big_and_ids();
    test_mid_publish();
    test_lock_fails();
    test_id_wrap();
    test_oversized();
    printf("test_mqtt_session: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
