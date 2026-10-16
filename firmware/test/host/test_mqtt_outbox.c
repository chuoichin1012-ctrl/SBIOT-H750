// Host tests for the MQTT outbox and the subscriptions list (spec §8, Plan 09 rulings 3 and 9):
// make -C stm32/firmware/test/host
#include <stdio.h>
#include <string.h>
#include "mqtt_outbox.h"

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

static uint8_t s_mem[4096];

static bool push(ob_t *o, const char *t, const char *p, int qos, uint32_t now)
{
    return ob_push(o, t, strlen(t), p, strlen(p), qos, false, now);
}

static void test_states(void)
{
    ob_t o;
    ob_init(&o, s_mem, sizeof s_mem);
    CHECK(push(&o, "a", "zero", 0, 0) && push(&o, "b", "one", 1, 0) && push(&o, "c", "two", 2, 0), "three queued");
    ob_msg_t *m = ob_next_queued(&o, 0);
    CHECK(m && !memcmp(ob_topic(m), "a", 1) && !memcmp(ob_payload(m), "zero", 4) && m->len == 4, "FIFO: the first, topic and payload in place");
    ob_sent(&o, m, 0);
    m = ob_next_queued(&o, 0);
    CHECK(m && m->qos == 1, "QoS 0 is gone once sent; next the QoS 1");
    ob_sent(&o, m, 7);
    ob_msg_t *c = ob_next_queued(&o, 0);
    ob_sent(&o, c, 8);
    CHECK(!ob_next_queued(&o, 0) && ob_in_flight(&o) == 2, "two in flight, none queued");
    CHECK(!ob_ack(&o, 7, OB_PUBREC) && ob_ack(&o, 7, OB_PUBACK), "PUBACK only for the one waiting for it");
    CHECK(ob_ack(&o, 8, OB_PUBREC) && ob_in_flight(&o) == 1 && !ob_ack(&o, 8, OB_PUBREC), "PUBREC: on to WAIT_PUBCOMP, once");
    CHECK(ob_ack(&o, 8, OB_PUBCOMP) && ob_in_flight(&o) == 0 && ob_used(&o) == 0, "PUBCOMP: gone, the space back");
}

static void test_ring(void)
{
    ob_t o;
    ob_init(&o, s_mem, 1024);
    char big[300];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    int n = 0;
    while (push(&o, "t", big, 1, 0))
        n++;
    CHECK(n == 3 && o.dropped == 1, "full: a new message is refused and counted (%d fit)", n);
    // the first goes, a new one wraps to the start; the others stay where they are
    ob_msg_t *m1 = ob_next_queued(&o, 0);
    ob_sent(&o, m1, 1);
    ob_msg_t *m2 = ob_next_queued(&o, 0);
    ob_sent(&o, m2, 2);
    const uint8_t *p2 = ob_payload(m2);
    CHECK(ob_ack(&o, 1, OB_PUBACK), "the first acked");
    CHECK(push(&o, "w", big, 1, 0), "a message wraps to the start");
    CHECK(ob_payload(m2) == p2 && m2->id == 2, "records never move (m2 in place)");
    // order kept across the wrap: m3 (queued before), then the wrapped one
    ob_msg_t *m3 = ob_next_queued(&o, 0);
    ob_sent(&o, m3, 3);
    ob_msg_t *w = ob_next_queued(&o, 0);
    CHECK(w && !memcmp(ob_topic(w), "w", 1), "the wrapped message comes after the older ones");
    ob_sent(&o, w, 4);
    CHECK(ob_in_flight(&o) == 3, "three in flight across the wrap");
    // an ack out of order frees nothing until the head goes
    ob_ack(&o, 3, OB_PUBACK);
    CHECK(!push(&o, "z", big, 1, 0), "space behind a slow head is not free yet (ruling 3)");
    ob_ack(&o, 2, OB_PUBACK);
    CHECK(push(&o, "z", big, 1, 0), "the head acked: room again");
    ob_ack(&o, 4, OB_PUBACK);
    ob_msg_t *z = ob_next_queued(&o, 0);
    ob_sent(&o, z, 5);
    ob_ack(&o, 5, OB_PUBACK);
    CHECK(ob_used(&o) == 0 && ob_in_flight(&o) == 0, "all gone: empty");
    // many small ones around the ring, a few times
    int sent = 0;
    for (int i = 0; i < 200; i++) {
        char t[16];
        snprintf(t, sizeof t, "s%d", i);
        CHECK(push(&o, t, "payload-bytes", 1, 0), "small %d", i);
        ob_msg_t *s = ob_next_queued(&o, 0);
        ob_sent(&o, s, (uint16_t)(i + 1));
        sent += ob_ack(&o, (uint16_t)(i + 1), OB_PUBACK);
    }
    CHECK(sent == 200 && ob_used(&o) == 0, "200 through the ring, nothing left (%d)", sent);
}

// A tail gap smaller than a record header before the wrap (Plan 09: records walk by count and an end mark).
static void test_small_gap(void)
{
    ob_t o;
    int tested = 0;
    for (int fill = 1; fill <= 30; fill++) {  // tail gaps of every size from the end
        ob_init(&o, s_mem, 512);
        char p[512];
        memset(p, 'p', sizeof p);
        size_t first = 512 - sizeof(ob_msg_t) - 1 - (size_t)fill;  // leaves `fill` bytes or so at the end
        ob_push(&o, "a", 1, p, first / 2, 1, false, 0);
        if (!ob_push(&o, "b", 1, p, first - first / 2 - sizeof(ob_msg_t) - 1, 1, false, 0))
            continue;  // the rounding left no room for b: not a gap case
        tested++;
        ob_msg_t *a = ob_next_queued(&o, 0);
        ob_sent(&o, a, 1);
        ob_ack(&o, 1, OB_PUBACK);  // room at the start now
        bool w = ob_push(&o, "c", 1, p, 40, 1, false, 0);
        ob_msg_t *b = ob_next_queued(&o, 0);
        ob_sent(&o, b, 2);
        ob_msg_t *c = ob_next_queued(&o, 0);
        CHECK(w && c && ob_topic(c)[0] == 'c', "gap %d: the wrapped record is found", fill);
        if (c)
            ob_sent(&o, c, 3);
        ob_ack(&o, 2, OB_PUBACK);
        ob_ack(&o, 3, OB_PUBACK);
        CHECK(ob_used(&o) == 0 && ob_in_flight(&o) == 0, "gap %d: empty after all acks (%zu used)", fill, ob_used(&o));
    }
    CHECK(tested >= 25, "most gap sizes tested (%d)", tested);
}

static void test_session(void)
{
    ob_t o;
    ob_init(&o, s_mem, sizeof s_mem);
    push(&o, "a", "1", 1, 0);
    push(&o, "b", "2", 2, 0);
    push(&o, "c", "3", 2, 0);
    push(&o, "d", "4", 1, 0);
    ob_msg_t *a = ob_next_queued(&o, 0);
    ob_sent(&o, a, 11);
    ob_msg_t *b = ob_next_queued(&o, 0);
    ob_sent(&o, b, 12);
    ob_msg_t *c = ob_next_queued(&o, 0);
    ob_sent(&o, c, 13);
    ob_ack(&o, 13, OB_PUBREC);  // c waits for PUBCOMP: coreMQTT resends its PUBREL
    ob_session(&o, true, 0);
    ob_msg_t *r1 = ob_next_resend(&o, NULL), *r2 = ob_next_resend(&o, r1), *r3 = ob_next_resend(&o, r2);
    CHECK(r1 == a && r2 == b && !r3 && a->id == 11 && b->id == 12, "a kept session: the PUBLISHes to resend, with their ids");
    CHECK(c->state == OB_WAIT_PUBCOMP && c->id == 13, "WAIT_PUBCOMP stays for coreMQTT's PUBREL");
    ob_session(&o, false, 0);
    CHECK(a->state == OB_QUEUED && b->state == OB_QUEUED && c->state == OB_QUEUED && !a->id && !c->id,
          "a new session: everything in flight is queued again, no ids");
    ob_msg_t *q = ob_next_queued(&o, 0);
    CHECK(q == a, "and goes in its order, before d");
}

static void test_expiry(void)
{
    ob_t o;
    ob_init(&o, s_mem, sizeof s_mem);
    push(&o, "a", "1", 1, 1000);
    push(&o, "b", "2", 1, 1000);
    ob_msg_t *a = ob_next_queued(&o, 1000);
    ob_sent(&o, a, 1);
    uint32_t later = 1000 + OB_EXPIRE_MS;
    CHECK(!ob_next_queued(&o, later), "a QUEUED message over an hour old is dropped");
    CHECK(ob_in_flight(&o) == 1 && ob_ack(&o, 1, OB_PUBACK), "one in flight never expires");
    CHECK(ob_used(&o) == 0, "and the space is back");
    push(&o, "c", "3", 0, 0xFFFFF000u);
    CHECK(ob_next_queued(&o, 0x00001000u), "the uptime counter wrapping is not an expiry");
}

static void test_requeue(void)
{
    ob_t o;
    ob_init(&o, s_mem, sizeof s_mem);
    push(&o, "a", "1", 2, 1000);
    push(&o, "b", "2", 0, 1000);
    push(&o, "c", "3", 1, 1000);
    ob_msg_t *a = ob_next_queued(&o, 1000);
    ob_sent(&o, a, 21);
    ob_sent(&o, ob_next_queued(&o, 1000), 0);  // b: QoS 0, deleted (a holds the head: not reclaimed yet)
    ob_msg_t *c = ob_next_queued(&o, 1000);
    ob_sent(&o, c, 22);
    ob_ack(&o, 22, OB_PUBACK);  // c: deleted too
    CHECK(ob_id_used(&o, 21) && !ob_id_used(&o, 22) && !ob_id_used(&o, 0), "ids in use: only the ones waiting for an ack");
    uint32_t later = 1000 + 2 * OB_EXPIRE_MS;  // the broker came back two hours later, with a new session
    ob_session(&o, false, later);
    CHECK(ob_next_queued(&o, later + 1) == a, "a message in flight is queued again with its wait restarted (Codex code review)");
    ob_sent(&o, a, 23);
    CHECK(!ob_next_queued(&o, later + 1), "deleted records stay deleted: no QoS 0 or acked message sent again");
    CHECK(ob_ack(&o, 23, OB_PUBREC) && ob_ack(&o, 23, OB_PUBCOMP) && ob_used(&o) == 0, "and the outbox empties");
}

static void test_subs(void)
{
    subs_t l = {0};
    CHECK(subs_add(&l, "a/+", 1, 0) && subs_add(&l, "b/#", 2, 1), "two topics");
    sub_t *s = subs_next(&l);
    CHECK(s && !strcmp(s->topic, "a/+"), "the first to subscribe");
    s->done = true;
    subs_next(&l)->done = true;
    CHECK(!subs_next(&l), "all subscribed in this session");
    CHECK(subs_add(&l, "edge/req", 0, 100), "a listener added while connected (Codex plan r1)");
    s = subs_next(&l);
    CHECK(s && s->tag == 100, "it is subscribed at once, without a reconnect");
    s->done = true;
    subs_session(&l);
    int n = 0;
    while ((s = subs_next(&l))) {
        s->done = true;
        n++;
    }
    CHECK(n == 3, "a connect subscribes everything again (%d)", n);
    CHECK(!subs_add(&l, "", 0, 1), "an empty topic is refused");
}

int main(void)
{
    test_states();
    test_ring();
    test_small_gap();
    test_session();
    test_expiry();
    test_requeue();
    test_subs();
    printf("test_mqtt_outbox: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
