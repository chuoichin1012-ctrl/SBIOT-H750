// Host tests for mqtt_transport (spec §8, Plan 09 ruling 2): coreMQTT's transport contract on the bridge's
// connection results, and no spin while nothing moves. make -C stm32/firmware/test/host
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "bridge_core.h"
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

// ---- scripted ops --------------------------------------------------------------------------------

typedef struct {
    int send_r[8], recv_r[8];  // what each call answers, in turn (>0 bytes, 0 closed, NET_AGAIN, NET_FAIL)
    int ns, nr;
    bool pending;
    int waits, waits_write, ticks, wait_r;
} script_t;

static int s_send(void *c, const uint8_t *d, size_t n)
{
    script_t *s = c;
    (void)d, (void)n;
    return s->send_r[s->ns++];
}
static int s_recv(void *c, uint8_t *b, size_t n)
{
    script_t *s = c;
    int r = s->recv_r[s->nr++];
    if (r > 0)
        memset(b, 'x', (size_t)r);
    (void)n;
    return r;
}
static bool s_pending(void *c) { return ((script_t *)c)->pending; }
static int s_wait(void *c, bool write, uint32_t ms)
{
    script_t *s = c;
    (void)ms;
    s->waits++;
    s->waits_write += write;
    return s->wait_r;
}
static script_t *s_cur;
static void s_tick(void) { s_cur->ticks++; }

static NetworkContext_t ctx_for(script_t *s, mqtt_link_t *l)
{
    *l = (mqtt_link_t){.send = s_send, .recv = s_recv, .pending = s_pending, .wait = s_wait, .tick = s_tick, .conn = s};
    s_cur = s;
    return (NetworkContext_t){.link = l};
}

static void test_results(void)
{
    script_t s = {.recv_r = {5, NET_AGAIN, 0, NET_FAIL}, .send_r = {3, NET_AGAIN, NET_FAIL}};
    mqtt_link_t l;
    NetworkContext_t nc = ctx_for(&s, &l);
    uint8_t b[16];
    CHECK(mqtt_rx(&nc, b, 16) == 5, "bytes: their count");
    CHECK(mqtt_rx(&nc, b, 16) == 0 && s.waits == 1 && s.waits_write == 0, "nothing yet: 0, after a wait for reading");
    CHECK(mqtt_rx(&nc, b, 16) == -1, "closed by the peer: -1");
    CHECK(mqtt_rx(&nc, b, 16) == -1, "an error: -1");
    CHECK(mqtt_tx(&nc, b, 10) == 3, "a partial write: the bytes written");
    CHECK(mqtt_tx(&nc, b, 7) == 0 && s.waits_write == 1, "nothing taken: 0, after a wait for writing");
    CHECK(mqtt_tx(&nc, b, 7) == -1, "a send error: -1");
}

static void test_pending(void)
{
    script_t s = {.recv_r = {NET_AGAIN, NET_AGAIN}, .send_r = {NET_AGAIN}, .pending = true};
    mqtt_link_t l;
    NetworkContext_t nc = ctx_for(&s, &l);
    uint8_t b[4];
    CHECK(mqtt_rx(&nc, b, 4) == 0 && s.waits == 0, "bytes held inside mbedTLS: reading does not wait");
    CHECK(mqtt_tx(&nc, b, 4) == 0 && s.waits_write == 1, "a send that gets AGAIN waits for writing, whatever is pending (Codex r1)");
}

static void test_no_spin(void)
{
    // select answers at once (wait_r 1) and nothing moves: a tick's sleep from the second result without progress
    script_t s = {.recv_r = {NET_AGAIN, NET_AGAIN, NET_AGAIN, 4, NET_AGAIN}, .pending = true, .wait_r = 1};
    mqtt_link_t l;
    NetworkContext_t nc = ctx_for(&s, &l);
    uint8_t b[4];
    mqtt_rx(&nc, b, 4);
    CHECK(s.ticks == 0, "one result without progress: no sleep yet");
    mqtt_rx(&nc, b, 4);
    mqtt_rx(&nc, b, 4);
    CHECK(s.ticks == 2, "then a tick each time (%d)", s.ticks);
    mqtt_rx(&nc, b, 4);
    mqtt_rx(&nc, b, 4);
    CHECK(s.ticks == 2, "progress resets the count");
}

// ---- real sockets: CPU while the peer is silent mid-packet ------------------------------------------

static int p_send(void *c, const uint8_t *d, size_t n)
{
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

static double cpu_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static double wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void test_cpu(void)
{
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    send(sv[1], "\x30\x7f", 2, 0);  // the start of a PUBLISH, then silence
    mqtt_link_t l = {.send = p_send, .recv = p_recv, .pending = p_pending, .wait = p_wait, .tick = p_tick, .conn = &sv[0]};
    NetworkContext_t nc = {.link = &l};
    uint8_t b[128];
    double c0 = cpu_ms(), t0 = wall_ms();
    int got = 0;
    while (wall_ms() - t0 < 2000) {  // as coreMQTT's recvExact calls it: again and again while it returns 0
        int r = mqtt_rx(&nc, b, sizeof b);
        if (r > 0)
            got += r;
    }
    double cpu = cpu_ms() - c0, wall = wall_ms() - t0;
    CHECK(got == 2 && cpu < 0.05 * wall, "2 s of a silent peer mid-packet: %.1f ms of CPU (%.1f %%)", cpu, 100 * cpu / wall);
    // a full send window: the same
    int one = 4096;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &one, sizeof one);
    static uint8_t big[1 << 16];
    c0 = cpu_ms(), t0 = wall_ms();
    while (wall_ms() - t0 < 1000)
        mqtt_tx(&nc, big, sizeof big);
    cpu = cpu_ms() - c0, wall = wall_ms() - t0;
    CHECK(cpu < 0.05 * wall, "1 s of a full send window: %.1f ms of CPU (%.1f %%)", cpu, 100 * cpu / wall);
    close(sv[0]);
    close(sv[1]);
}

int main(void)
{
    test_results();
    test_pending();
    test_no_spin();
    test_cpu();
    printf("test_mqtt_transport: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
