// The outbox and the subscriptions on coreMQTT (Plan 09); see mqtt_sess.h.
#include "mqtt_sess.h"
#include <string.h>

#define FEED_BURST 8  // publishes per ms_feed: the loop then reads, and the task rests now and then (Codex code review)

/** The next packet id no message waiting for an ack holds: coreMQTT's counter wraps at 65535 without looking. */
static uint16_t next_id(mqtt_sess_t *s)
{
    uint16_t id = MQTT_GetPacketId(s->ctx);
    for (int k = 0; k < OB_MAX_IN_FLIGHT && ob_id_used(s->ob, id); k++)
        id = MQTT_GetPacketId(s->ctx);
    return id;
}

// One record to the broker. The record never moves (mqtt_outbox.h), so it is sent without the mutex, straight from
// the outbox: coreMQTT sends the payload by I/O vector.
static MQTTStatus_t publish(mqtt_sess_t *s, ob_msg_t *m, bool dup)
{
    MQTTPublishInfo_t pi = {.qos = (MQTTQoS_t)m->qos, .retain = m->retain, .dup = dup, .pTopicName = ob_topic(m),
                            .topicNameLength = m->topic_len, .pPayload = ob_payload(m), .payloadLength = m->len};
    uint16_t id = 0;
    if (m->qos && dup)
        id = m->id;
    else if (m->qos) {
        if (!s->lock())
            return MQTTIllegalState;
        id = next_id(s);
        s->unlock();
    }
    MQTTStatus_t r = MQTT_Publish(s->ctx, &pi, id);
    if (dup)
        return r;  // a resend keeps its state and id either way
    if (r != MQTTSuccess && r != MQTTSendFailed && r != MQTTBadParameter)
        return r;  // nothing went out (no memory, a state collision): it stays QUEUED, the connection is made again
    if (!s->lock())
        return MQTTIllegalState;
    if (r == MQTTBadParameter)
        ob_sent(s->ob, m, 0), m->state = OB_DEAD;  // a message coreMQTT refuses (bad topic): never again
    else
        ob_sent(s->ob, m, id);  // failed midway: its bytes may have reached the broker (Plan 09 ruling 3)
    s->unlock();
    return r == MQTTBadParameter ? MQTTSuccess : r;
}

MQTTStatus_t ms_resume(mqtt_sess_t *s, bool kept)
{
    if (!s->lock())
        return MQTTIllegalState;
    ob_session(s->ob, kept, s->now_ms());
    subs_session(s->subs);
    s->unlock();
    MQTTStatus_t r = MQTTSuccess;
    for (ob_msg_t *m = NULL; kept && r == MQTTSuccess;) {
        if (!s->lock())
            return MQTTIllegalState;
        m = ob_next_resend(s->ob, m);
        s->unlock();
        if (!m)
            break;
        r = publish(s, m, true);
        s->resent++;
    }
    return r == MQTTSuccess ? ms_subscribe(s) : r;
}

MQTTStatus_t ms_feed(mqtt_sess_t *s)
{
    for (int k = 0; k < FEED_BURST; k++) {
        if (!s->lock())
            return MQTTIllegalState;
        ob_msg_t *m = ob_in_flight(s->ob) < OB_MAX_IN_FLIGHT ? ob_next_queued(s->ob, s->now_ms()) : NULL;
        s->unlock();
        if (!m)
            return MQTTSuccess;
        MQTTStatus_t r = publish(s, m, false);
        if (r != MQTTSuccess)
            return r;
    }
    return MQTTSuccess;
}

MQTTStatus_t ms_subscribe(mqtt_sess_t *s)
{
    for (;;) {
        if (!s->lock())
            return MQTTIllegalState;
        sub_t *e = subs_next(s->subs), copy;
        if (e)
            copy = *e;
        s->unlock();
        if (!e)
            return MQTTSuccess;
        MQTTSubscribeInfo_t si = {.qos = (MQTTQoS_t)copy.qos, .pTopicFilter = copy.topic,
                                  .topicFilterLength = (uint16_t)strlen(copy.topic)};
        if (!s->lock())
            return MQTTIllegalState;
        uint16_t id = next_id(s);
        s->unlock();
        MQTTStatus_t r = MQTT_Subscribe(s->ctx, &si, 1, id);
        if (r != MQTTSuccess)
            return r;
        if (!s->lock())
            return MQTTIllegalState;
        e->done = true;  // entries are only ever added: e is still this one
        s->unlock();
    }
}

void ms_ack(mqtt_sess_t *s, uint8_t packet_type, uint16_t id)
{
    int t = packet_type == MQTT_PACKET_TYPE_PUBACK ? OB_PUBACK : packet_type == MQTT_PACKET_TYPE_PUBREC ? OB_PUBREC
            : packet_type == MQTT_PACKET_TYPE_PUBCOMP                                                   ? OB_PUBCOMP
                                                                                                        : -1;
    if (t < 0 || !s->lock())
        return;  // an ack not recorded: the message is resent on the next connect, and acked again
    ob_ack(s->ob, id, t);
    s->unlock();
}
