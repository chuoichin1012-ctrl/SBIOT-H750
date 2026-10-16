// The outbox and the subscriptions on coreMQTT (Plan 09); see mqtt_sess.h.
#include "mqtt_sess.h"
#include <string.h>

// One record to the broker. The record never moves (mqtt_outbox.h), so it is sent without the mutex, straight from
// the outbox: coreMQTT sends the payload by I/O vector.
static MQTTStatus_t publish(mqtt_sess_t *s, ob_msg_t *m, bool dup)
{
    MQTTPublishInfo_t pi = {.qos = (MQTTQoS_t)m->qos, .retain = m->retain, .dup = dup, .pTopicName = ob_topic(m),
                            .topicNameLength = m->topic_len, .pPayload = ob_payload(m), .payloadLength = m->len};
    uint16_t id = m->qos == 0 ? 0 : dup ? m->id : MQTT_GetPacketId(s->ctx);
    MQTTStatus_t r = MQTT_Publish(s->ctx, &pi, id);
    if (dup)
        return r;  // a resend keeps its state and id either way
    s->lock();
    if (r == MQTTSuccess || r == MQTTSendFailed)
        ob_sent(s->ob, m, id);  // failed midway: its bytes may have reached the broker (Plan 09 ruling 3)
    else
        ob_sent(s->ob, m, 0), m->state = OB_DEAD;  // a message coreMQTT refuses (bad topic): never again
    s->unlock();
    return r == MQTTSendFailed || r == MQTTSuccess ? r : MQTTSuccess;
}

MQTTStatus_t ms_resume(mqtt_sess_t *s, bool kept)
{
    s->lock();
    ob_session(s->ob, kept);
    subs_session(s->subs);
    s->unlock();
    MQTTStatus_t r = MQTTSuccess;
    for (ob_msg_t *m = NULL; kept && r == MQTTSuccess;) {
        s->lock();
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
    for (;;) {
        s->lock();
        ob_msg_t *m = ob_in_flight(s->ob) < OB_MAX_IN_FLIGHT ? ob_next_queued(s->ob, s->now_ms()) : NULL;
        s->unlock();
        if (!m)
            return MQTTSuccess;
        MQTTStatus_t r = publish(s, m, false);
        if (r != MQTTSuccess)
            return r;
    }
}

MQTTStatus_t ms_subscribe(mqtt_sess_t *s)
{
    for (;;) {
        s->lock();
        sub_t *e = subs_next(s->subs), copy;
        if (e)
            copy = *e;
        s->unlock();
        if (!e)
            return MQTTSuccess;
        MQTTSubscribeInfo_t si = {.qos = (MQTTQoS_t)copy.qos, .pTopicFilter = copy.topic,
                                  .topicFilterLength = (uint16_t)strlen(copy.topic)};
        MQTTStatus_t r = MQTT_Subscribe(s->ctx, &si, 1, MQTT_GetPacketId(s->ctx));
        if (r != MQTTSuccess)
            return r;
        s->lock();
        e->done = true;  // entries are only ever added: e is still this one
        s->unlock();
    }
}

void ms_ack(mqtt_sess_t *s, uint8_t packet_type, uint16_t id)
{
    int t = packet_type == MQTT_PACKET_TYPE_PUBACK ? OB_PUBACK : packet_type == MQTT_PACKET_TYPE_PUBREC ? OB_PUBREC
            : packet_type == MQTT_PACKET_TYPE_PUBCOMP                                                   ? OB_PUBCOMP
                                                                                                        : -1;
    if (t < 0)
        return;
    s->lock();
    ob_ack(s->ob, id, t);
    s->unlock();
}
