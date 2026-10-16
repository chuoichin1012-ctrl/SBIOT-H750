// The MQTT outbox (spec §8, Plan 09 ruling 3), host-tested (test_mqtt_outbox): the messages waiting for the broker,
// each with its state, kept in place in a ring of records over a caller's region (64 KB in SRAM2+3 on the board).
// A record never moves, so the `mqtt` task publishes straight from it without the mutex; space is reclaimed from the
// head over deleted records. The caller holds the outbox's mutex around every call here. Also the subscriptions
// list (`subs`), kept in the same file because both are the `mqtt` task's session state.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { OB_QUEUED, OB_WAIT_PUBACK, OB_WAIT_PUBREC, OB_WAIT_PUBCOMP, OB_DEAD };
enum { OB_PUBACK, OB_PUBREC, OB_PUBCOMP };
#define OB_EXPIRE_MS (3600u * 1000u)  // a QUEUED message older than this is dropped (uptime, the clock may be unset)
#define OB_MAX_IN_FLIGHT 16

typedef struct {
    uint8_t state, qos;
    bool retain;
    uint8_t _pad;
    uint16_t id, topic_len;
    uint32_t len, t_ms, size;  // payload bytes, when queued, the record's bytes
} ob_msg_t;

typedef struct {
    uint8_t *mem;
    size_t cap, head, tail, end;  // records run head..end, then 0..tail once wrapped (end == cap otherwise)
    size_t count, used;           // records (deleted ones too, until reclaimed) and their bytes
    uint32_t dropped;             // messages refused for room
} ob_t;

void ob_init(ob_t *o, void *mem, size_t cap);
/** A copy of the message at the tail; false when it does not fit (counted in `dropped`). */
bool ob_push(ob_t *o, const char *topic, size_t tlen, const void *data, size_t len, int qos, bool retain, uint32_t now_ms);
static inline const char *ob_topic(const ob_msg_t *m) { return (const char *)(m + 1); }
static inline const uint8_t *ob_payload(const ob_msg_t *m) { return (const uint8_t *)(m + 1) + m->topic_len; }
/** The oldest QUEUED message, expiring the ones over OB_EXPIRE_MS on the way; NULL when none. */
ob_msg_t *ob_next_queued(ob_t *o, uint32_t now_ms);
/** After its PUBLISH (or a failed attempt, Plan 09 ruling 3): QoS 0 is deleted; 1 waits for PUBACK; 2 for PUBREC. */
void ob_sent(ob_t *o, ob_msg_t *m, uint16_t id);
/** An ack from the broker: PUBACK and PUBCOMP delete, PUBREC moves on to WAIT_PUBCOMP. False when no message waits for it. */
bool ob_ack(ob_t *o, uint16_t id, int type);
/** After a connect: a new session sends everything in flight again as QUEUED (new ids); a kept one leaves it. */
void ob_session(ob_t *o, bool kept, uint32_t now_ms);
/** A packet id held by a message waiting for an ack: not to be given to another (MQTT 3.1.1 §2.3.1). */
bool ob_id_used(const ob_t *o, uint16_t id);
/** A kept session's PUBLISH resends (WAIT_PUBACK, WAIT_PUBREC), in order: after `prev` (NULL: the first). */
ob_msg_t *ob_next_resend(ob_t *o, ob_msg_t *prev);
int ob_in_flight(const ob_t *o);
size_t ob_used(const ob_t *o);

// ---- subscriptions -----------------------------------------------------------------------------------------

#define SUBS_MAX 24  // 16 subscribe topics, Edge's request topic, and room
typedef struct {
    char topic[129];
    uint8_t qos;
    bool done;  // subscribed in this session
    int tag;    // the owner's (a subscribe topic's index, or a listener's)
} sub_t;
typedef struct {
    sub_t s[SUBS_MAX];
    int n;
} subs_t;

/** A new entry, not yet subscribed; false when full or the topic is empty or too long. */
bool subs_add(subs_t *l, const char *topic, int qos, int tag);
/** A connect: everything is to be subscribed again. */
void subs_session(subs_t *l);
/** The next entry to subscribe in this session, or NULL. */
sub_t *subs_next(subs_t *l);
