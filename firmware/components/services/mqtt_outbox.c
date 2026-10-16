// The MQTT outbox and the subscriptions list (Plan 09); see mqtt_outbox.h.
#include "mqtt_outbox.h"
#include <string.h>

#define ALIGN 4u

static size_t rec_size(size_t tlen, size_t len) { return (sizeof(ob_msg_t) + tlen + len + ALIGN - 1) & ~(size_t)(ALIGN - 1); }
static ob_msg_t *at(const ob_t *o, size_t off) { return (ob_msg_t *)(o->mem + off); }

void ob_init(ob_t *o, void *mem, size_t cap)
{
    memset(o, 0, sizeof *o);
    o->mem = mem;
    o->cap = o->end = cap & ~(size_t)(ALIGN - 1);
}

// The ring has wrapped: records run head..end, then 0..tail.
static bool wrapped(const ob_t *o) { return o->count && o->tail <= o->head; }

// The record after the one at `off`.
static size_t next_off(const ob_t *o, size_t off)
{
    size_t n = off + at(o, off)->size;
    return n >= o->end ? 0 : n;
}

// Reclaims deleted records at the head.
static void reclaim(ob_t *o)
{
    while (o->count && at(o, o->head)->state == OB_DEAD) {
        o->used -= at(o, o->head)->size;
        o->count--;
        size_t n = o->head + at(o, o->head)->size;
        if (n >= o->end) {  // the head wraps: the gap before the end is free again
            n = 0;
            o->end = o->cap;
        }
        o->head = n;
    }
    if (!o->count) {
        o->head = o->tail = 0;
        o->end = o->cap;
    }
}

bool ob_push(ob_t *o, const char *topic, size_t tlen, const void *data, size_t len, int qos, bool retain, uint32_t now_ms)
{
    size_t need = rec_size(tlen, len), off;
    if (tlen > 0xFFFFu || need > o->cap) {
        o->dropped++;
        return false;
    }
    if (!o->count) {
        off = 0;
    } else if (!wrapped(o)) {
        if (o->cap - o->tail >= need) {
            off = o->tail;
        } else if (o->head >= need) {  // wrap: the gap from the tail to the end stays unused until the head passes
            o->end = o->tail;
            off = 0;
        } else {
            o->dropped++;
            return false;
        }
    } else if (o->head - o->tail >= need) {
        off = o->tail;
    } else {
        o->dropped++;
        return false;
    }
    ob_msg_t *m = at(o, off);
    memset(m, 0, sizeof *m);
    m->state = OB_QUEUED;
    m->qos = (uint8_t)qos;
    m->retain = retain;
    m->topic_len = (uint16_t)tlen;
    m->len = (uint32_t)len;
    m->t_ms = now_ms;
    m->size = (uint32_t)need;
    memcpy((uint8_t *)(m + 1), topic, tlen);
    memcpy((uint8_t *)(m + 1) + tlen, data, len);
    o->used += need;
    o->count++;
    o->tail = off + need;
    if (o->tail >= o->cap) {  // exactly full to the end: the next one starts at 0
        o->tail = 0;
        o->end = o->cap;
    }
    return true;
}

// Calls fn on every live record from the head; stops when fn returns true (that record is returned).
static ob_msg_t *walk(const ob_t *o, bool (*fn)(ob_msg_t *m, void *ctx), void *ctx)
{
    size_t off = o->head;
    for (size_t i = 0; i < o->count; i++) {
        ob_msg_t *m = at(o, off);
        if (m->state != OB_DEAD && fn(m, ctx))
            return m;
        off = next_off(o, off);
    }
    return NULL;
}

typedef struct {
    uint32_t now;
    bool expired;
} q_ctx_t;

static bool is_queued(ob_msg_t *m, void *ctx)
{
    q_ctx_t *q = ctx;
    if (m->state != OB_QUEUED)
        return false;
    if (q->now - m->t_ms >= OB_EXPIRE_MS) {  // only QUEUED messages expire: an id in use is never dropped
        m->state = OB_DEAD;
        q->expired = true;
        return false;
    }
    return true;
}

ob_msg_t *ob_next_queued(ob_t *o, uint32_t now_ms)
{
    q_ctx_t q = {now_ms, false};
    ob_msg_t *m = walk(o, is_queued, &q);
    if (q.expired)
        reclaim(o);
    return m;
}

void ob_sent(ob_t *o, ob_msg_t *m, uint16_t id)
{
    m->id = id;
    m->state = m->qos == 0 ? OB_DEAD : m->qos == 1 ? OB_WAIT_PUBACK : OB_WAIT_PUBREC;
    reclaim(o);
}

typedef struct {
    uint16_t id;
    uint8_t state;
} id_ctx_t;

static bool has_id(ob_msg_t *m, void *ctx)
{
    id_ctx_t *c = ctx;
    return m->id == c->id && m->state == c->state;
}

bool ob_ack(ob_t *o, uint16_t id, int type)
{
    id_ctx_t c = {id, (uint8_t)(type == OB_PUBACK ? OB_WAIT_PUBACK : type == OB_PUBREC ? OB_WAIT_PUBREC : OB_WAIT_PUBCOMP)};
    ob_msg_t *m = walk(o, has_id, &c);
    if (!m)
        return false;
    m->state = type == OB_PUBREC ? OB_WAIT_PUBCOMP : OB_DEAD;
    reclaim(o);
    return true;
}

static bool requeue(ob_msg_t *m, void *ctx)
{
    (void)ctx;
    if (m->state != OB_QUEUED) {
        m->state = OB_QUEUED;
        m->id = 0;
    }
    return false;
}

void ob_session(ob_t *o, bool kept)
{
    if (!kept)
        walk(o, requeue, NULL);
}

typedef struct {
    const ob_msg_t *prev;
    bool past;
} r_ctx_t;

static bool is_resend(ob_msg_t *m, void *ctx)
{
    r_ctx_t *r = ctx;
    if (r->prev && !r->past) {
        r->past = m == r->prev;
        return false;
    }
    return m->state == OB_WAIT_PUBACK || m->state == OB_WAIT_PUBREC;
}

ob_msg_t *ob_next_resend(ob_t *o, ob_msg_t *prev)
{
    r_ctx_t r = {prev, false};
    return walk(o, is_resend, &r);
}

static bool count_flight(ob_msg_t *m, void *ctx)
{
    if (m->state == OB_WAIT_PUBACK || m->state == OB_WAIT_PUBREC || m->state == OB_WAIT_PUBCOMP)
        ++*(int *)ctx;
    return false;
}

int ob_in_flight(const ob_t *o)
{
    int n = 0;
    walk(o, count_flight, &n);
    return n;
}

size_t ob_used(const ob_t *o) { return o->used; }

// ---- subscriptions -----------------------------------------------------------------------------------------

bool subs_add(subs_t *l, const char *topic, int qos, int tag)
{
    size_t n = topic ? strlen(topic) : 0;
    if (l->n >= SUBS_MAX || !n || n >= sizeof l->s[0].topic)
        return false;
    sub_t *s = &l->s[l->n++];
    memcpy(s->topic, topic, n + 1);
    s->qos = (uint8_t)qos;
    s->done = false;
    s->tag = tag;
    return true;
}

void subs_session(subs_t *l)
{
    for (int i = 0; i < l->n; i++)
        l->s[i].done = false;
}

sub_t *subs_next(subs_t *l)
{
    for (int i = 0; i < l->n; i++)
        if (!l->s[i].done)
            return &l->s[i];
    return NULL;
}
