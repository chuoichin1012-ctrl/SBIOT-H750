// The MQTT gateway (spec §8, Plan 09): the ESP32's mqttgw.c on coreMQTT 2.3.1, for both ports.
//
// Publish topics (bound to UART1/UART2 by `ports`): mode 0 publishes every serial frame; mode 1 "identifier,payload"
// publishes the payload (the identifier a whole comma-separated field). Subscribe topics: mode 0 writes the payload
// to the bound ports, mode 1 topic + delimiter + payload. ioControl: Modbus RTU IO requests (io_mb_remote), the reply
// published on the publish topics with ioControl. Custom mode: a serial frame "topic,qos,retain,payload".
//
// Tasks: `mqtt` is coreMQTT's only caller (connect, the outbox's resends, the loop, reconnectIfSilent); `mqtt_io`
// moves serial frames into the outbox and broker messages to the ports, IO control and Edge (it may wait on the UART
// or a device, `mqtt` never does). Each hands the other its data through rings in SRAM4; the serial subscribers only
// queue. The outbox (64 KB, SRAM2+3) keeps messages in place: `mqtt` publishes from their records (mqtt_sess).
#include "wdog.h"
#include "mqttgw.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "blog.h"
#include "core_mqtt.h"
#include "io_mb.h"
#include "message_buffer.h"
#include "mqtt_outbox.h"
#include "mqtt_sess.h"
#include "mqtt_transport.h"
#include "mqttutil.h"
#include "netmgr.h"
#include "now_us.h"
#include "semphr.h"
#include "serial.h"
#include "services.h"
#include "store.h"
#include "svc_net.h"
#include "syslog.h"
#include "sysmgr.h"
#include "task.h"
#include "tlsconn.h"

#define SRAM23_BSS __attribute__((section(".sram23_bss")))
#define SRAM4_BSS __attribute__((section(".sram4_bss")))
#define AXI_BSS __attribute__((section(".axi_bss")))  // tables and buffers: DTCM keeps the stacks (spec §5.3)
#define MAX_TOPICS 16
#define MAX_LISTENERS 4
#define OUTBOX_BYTES (64 * 1024)     // spec §5.3
#define UP_BYTES (6 * 1024)          // per port: serial -> mqtt_io (one writer each)
#define DOWN_BYTES (12 * 1024)       // mqtt -> mqtt_io
#define NETBUF_BYTES 4096            // coreMQTT's receive buffer: larger messages are dropped (ruling 11)
#define IO_BUF_BYTES (NETBUF_BYTES + 160)
#define MQTT_STACK_WORDS 1536        // 6 KB (spec §6.1)
#define IO_STACK_WORDS 1024          // 4 KB
#define PRIO 5
#define LOCK_MS 2000u
#define REST_EVERY 8  // loop turns between two one-tick rests, in both tasks

typedef struct {
    char topic[129];
    int mode, qos;
    bool retain, io;
    uint8_t ports;  // bit 0 UART1, bit 1 UART2
    char ident[65], delim[17];
} topic_t;

typedef struct {
    char host[129], client[129], user[129], pass[201], will_topic[129], will_msg[129], name[129];
    uint16_t port;
    bool mqtt31, clean, auth, will, will_retain;
    int keepalive, silent_s, retry_s, will_qos, tls, verify;
    bool custom;
    uint8_t custom_ports;
} mqtt_cfg_t;

static mqtt_cfg_t C AXI_BSS;
static topic_t s_pub[MAX_TOPICS] AXI_BSS, s_sub[MAX_TOPICS] AXI_BSS;
static int s_npub, s_nsub;
static struct {
    char topic[129];
    mqttgw_rx_fn fn;
} s_listen[MAX_LISTENERS] AXI_BSS;
static int s_nlisten;
static bool s_keep_offline;
static volatile bool s_running, s_online;
static char s_state[24] = "NOT RUNNING";

static uint8_t s_ob_mem[OUTBOX_BYTES] SRAM23_BSS;
static ob_t s_ob;
static subs_t s_subs AXI_BSS;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_mem;
static uint8_t s_up_mem[SERIAL_PORTS][UP_BYTES + 1] SRAM4_BSS, s_down_mem[DOWN_BYTES + 1] SRAM4_BSS;
static StaticMessageBuffer_t s_up_cb[SERIAL_PORTS], s_down_cb;
static MessageBufferHandle_t s_up[SERIAL_PORTS], s_down;
static volatile uint32_t s_up_drops, s_down_drops;

static MQTTContext_t s_ctx;
static MQTTPubAckInfo_t s_out_rec[OB_MAX_IN_FLIGHT], s_in_rec[OB_MAX_IN_FLIGHT];
static conn_t s_conn = {.fd = -1};
static mqtt_link_t s_link;
static NetworkContext_t s_nc;
static mqtt_sess_t s_sess;
static TaskHandle_t s_mqtt_task, s_io_task;
static volatile uint64_t s_last_rx_us;
static uint8_t *s_io_buf;  // mqtt_io's frame or message (heap)

static uint32_t now_ms(void) { return (uint32_t)(now_us() / 1000u); }

static void set_state(const char *s)
{
    taskENTER_CRITICAL();
    snprintf(s_state, sizeof s_state, "%s", s);
    taskEXIT_CRITICAL();
}

// No holder of the mutex ever waits on anything, so a timeout means a bug. Other tasks then give up and do nothing;
// `mqtt` waits on, logging, because giving up half-way through its exchanges would leave coreMQTT's state and the
// outbox's apart (Codex code review r2).
static bool lock(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_MS)) == pdTRUE)
        return true;
    SLOG_E("mqtt", "the outbox stayed locked for %u ms", (unsigned)LOCK_MS);
    return false;
}
static void lock_wait(void)
{
    while (!lock()) {
    }
}
static void unlock(void) { xSemaphoreGive(s_lock); }

void coremqtt_log(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    SLOG_W("mqtt", "coreMQTT: %s", line);
}

static void touch_rx(void)
{
    uint64_t t = now_us();
    taskENTER_CRITICAL();
    s_last_rx_us = t;
    taskEXIT_CRITICAL();
}
static uint64_t last_rx(void)
{
    taskENTER_CRITICAL();
    uint64_t t = s_last_rx_us;
    taskEXIT_CRITICAL();
    return t;
}

// ---- config ---------------------------------------------------------------------------------------------

static int num(const cJSON *o, const char *k) { return (int)cJSON_GetNumberValue(cJSON_GetObjectItem(o, k)); }
static const char *text(const cJSON *o, const char *k)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(o, k));
    return s ? s : "";
}
static bool flag(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valuedouble != 0);
}
static void copy(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s); }

/** The enabled topics, or -1 when the list cannot be read (store_get gives a known name's default, so NULL is no memory). */
static int load_topics(const char *name, topic_t *out)
{
    cJSON *list = store_get(name);
    if (!cJSON_IsArray(list)) {
        cJSON_Delete(list);
        return -1;
    }
    int n = 0;
    const cJSON *t;
    cJSON_ArrayForEach(t, list)
    {
        if (n >= MAX_TOPICS || !flag(t, "enabled") || !*text(t, "topic"))
            continue;
        topic_t *x = &out[n++];
        copy(x->topic, sizeof x->topic, text(t, "topic"));
        x->mode = num(t, "mode");
        x->qos = num(t, "qos");
        x->retain = flag(t, "retain");
        x->io = flag(t, "ioControl");
        x->ports = (uint8_t)num(t, "ports");
        copy(x->ident, sizeof x->ident, text(t, "identifier"));
        copy(x->delim, sizeof x->delim, text(t, "delimiter"));
    }
    cJSON_Delete(list);
    return n;
}

static void read_cfg(const cJSON *c)
{
    copy(C.host, sizeof C.host, text(c, "host"));
    C.port = (uint16_t)num(c, "port");
    C.mqtt31 = num(c, "version") == 0;
    copy(C.client, sizeof C.client, text(c, "clientId"));
    C.keepalive = num(c, "keepAlive");
    C.silent_s = num(c, "reconnectIfSilent");
    C.retry_s = num(c, "retryInterval");
    C.clean = flag(c, "cleanSession");
    C.auth = flag(c, "auth");
    copy(C.user, sizeof C.user, text(c, "username"));
    copy(C.pass, sizeof C.pass, text(c, "password"));
    C.will = flag(c, "will") && *text(c, "willTopic");
    copy(C.will_topic, sizeof C.will_topic, text(c, "willTopic"));
    copy(C.will_msg, sizeof C.will_msg, text(c, "willMessage"));
    C.will_qos = num(c, "willQos");
    C.will_retain = flag(c, "willRetain");
    C.tls = num(c, "tls");
    C.verify = num(c, "tlsVerify");
    // The name for SNI and the certificate check (Plan 09 ruling 5): sniHost when asked for, else the host.
    copy(C.name, sizeof C.name, flag(c, "sni") && *text(c, "sniHost") ? text(c, "sniHost") : C.host);
    C.custom = num(c, "customMode") == 1;
    C.custom_ports = (uint8_t)num(c, "customModePorts");
    if (!C.client[0]) {  // Plan 09 ruling 4: derived from the MAC, as esp-mqtt did
        net_status_t ns;
        netmgr_status(&ns);
        snprintf(C.client, sizeof C.client, "SBIOT-%02X%02X%02X", ns.mac[3], ns.mac[4], ns.mac[5]);
    }
}

// ---- the queue into the outbox --------------------------------------------------------------------------

static bool enqueue(const char *topic, size_t tlen, const void *data, size_t len, int qos, bool retain)
{
    if (!s_running || (!s_online && !s_keep_offline))
        return false;  // with bufferOffline it waits for the broker, QoS 0 too
    if (!lock())
        return false;
    bool ok = ob_push(&s_ob, topic, tlen, data, len, qos, retain, now_ms());
    unlock();
    if (!ok) {
        static uint32_t s_warned;
        if (now_ms() - s_warned > 60000 || !s_warned) {
            s_warned = now_ms();
            SLOG_W("mqtt", "outbox full: a message to %.*s dropped (%u so far)", (int)tlen, topic, (unsigned)s_ob.dropped);
        }
        return false;
    }
    if (s_online)
        svc_net_activity();
    xTaskNotifyGive(s_mqtt_task);  // feed it now
    return true;
}

bool mqttgw_publish(const char *topic, const void *data, size_t len, int qos, bool retain)
{
    return topic && enqueue(topic, strlen(topic), data, len, qos, retain);
}

bool mqttgw_add_listener(const char *topic, int qos, mqttgw_rx_fn fn)
{
    if (!s_running || !topic || !*topic)
        return false;
    if (!lock())
        return false;
    bool ok = s_nlisten < MAX_LISTENERS && subs_add(&s_subs, topic, qos, 100 + s_nlisten);
    if (ok) {
        copy(s_listen[s_nlisten].topic, sizeof s_listen[0].topic, topic);
        s_listen[s_nlisten].fn = fn;
        s_nlisten++;
    }
    unlock();
    if (ok)
        xTaskNotifyGive(s_mqtt_task);  // subscribed at once when connected (Codex plan r1)
    return ok;
}

// ---- uplink and downlink, in mqtt_io ---------------------------------------------------------------------

static void uplink(int port, const uint8_t *data, size_t len)
{
    uint8_t bit = (uint8_t)(1u << (port - 1));
    if (C.custom && (C.custom_ports & bit)) {
        char topic[129];
        int qos;
        bool retain;
        const char *payload;
        size_t pl;
        if (mqtt_custom_parse((const char *)data, len, topic, sizeof topic, &qos, &retain, &payload, &pl))
            enqueue(topic, strlen(topic), payload, pl, qos, retain);
        else
            SLOG_W("mqtt", "custom mode: UART%d frame is not \"topic,qos,retain,payload\"", port);
        return;
    }
    for (int i = 0; i < s_npub; i++) {
        const topic_t *t = &s_pub[i];
        if (!(t->ports & bit))
            continue;
        if (t->mode == 0) {
            enqueue(t->topic, strlen(t->topic), data, len, t->qos, t->retain);
        } else {
            size_t off = mqtt_alias_match(t->ident, (const char *)data, len);
            if (off)
                enqueue(t->topic, strlen(t->topic), data + off, len - off, t->qos, t->retain);
        }
    }
}

static void io_reply(const uint8_t *data, size_t len)
{
    for (int i = 0; i < s_npub; i++)
        if (s_pub[i].io)
            enqueue(s_pub[i].topic, strlen(s_pub[i].topic), data, len, s_pub[i].qos, s_pub[i].retain);
}

static int s_hb_mqtt = -1, s_hb_io = -1;  // the supervisor's slots (Plan 11a task 0b)

/**
 * One forward is done: `mqtt_io` beats, as several subscriptions and both ports may each take one message
 * (at 600 baud, 4 KB takes 67 s, so eight forwards are past the 400 s timeout). A write that never got the
 * bus does not beat: a stuck lock must still reset the board (spec §11.4). One forward is at most about
 * 177 s: 2 s for the bus, the 485 idle wait (2.5 s + 2 s), and uart_drv_write's 2 s + 2 x 4256 chars at
 * 20 ms (600 baud, 12-bit chars).
 */
static void forwarded(int r)
{
    if (r != SERIAL_BUSY)
        wdog_hb_beat(s_hb_io);
}

static void downlink(const char *topic, size_t tlen, const uint8_t *data, size_t len)
{
    for (int i = 0; i < s_nlisten; i++)  // Edge's request topic
        if (mqtt_topic_match(s_listen[i].topic, topic, tlen)) {
            s_listen[i].fn(data, len);
            wdog_hb_beat(s_hb_io);  // the handler beats itself while it waits (wdog_hb_beat_self)
        }
    for (int i = 0; i < s_nsub; i++) {
        const topic_t *t = &s_sub[i];
        if (!mqtt_topic_match(t->topic, topic, tlen))
            continue;
        if (t->io) {
            static uint8_t reply[300] AXI_BSS;
            size_t n = io_mb_remote(data, len, reply, sizeof reply);
            if (n) {  // an IO request: answered, not forwarded
                io_reply(reply, n);
                continue;
            }
        }
        for (int port = 1; port <= SERIAL_PORTS; port++) {
            if (!(t->ports & (1u << (port - 1))))
                continue;
            if (t->mode == 1) {  // topic + delimiter + payload
                size_t dl = strlen(t->delim), n = tlen + dl + len;
                uint8_t *b = malloc(n);
                if (!b)
                    continue;
                memcpy(b, topic, tlen);
                memcpy(b + tlen, t->delim, dl);
                memcpy(b + tlen + dl, data, len);
                forwarded(serial_write(port, b, n, 2000));
                free(b);
            } else {
                forwarded(serial_write(port, data, len, 2000));
            }
        }
    }
}

static void io_task(void *arg)
{
    (void)arg;
    uint32_t seen_up = 0, seen_down = 0;
    // 400 s: an Edge write it makes may wait 60 s for the bus and 65.5 s for the reply (Codex code review r2)
    s_hb_io = wdog_hb_add("mqtt_io", 400000);
    for (unsigned turn = 1;; turn++) {
        wdog_hb_beat(s_hb_io);
        if (turn % REST_EVERY == 0)
            vTaskDelay(1);  // as `mqtt`: rings that are never empty must not starve lower tasks
        size_t n = xMessageBufferReceive(s_down, s_io_buf, IO_BUF_BYTES, pdMS_TO_TICKS(10));
        if (n > 2) {
            uint16_t tl = (uint16_t)(s_io_buf[0] | s_io_buf[1] << 8);
            if ((size_t)tl + 2 <= n)
                downlink((const char *)s_io_buf + 2, tl, s_io_buf + 2 + tl, n - 2 - tl);
        }
        for (int p = 0; p < SERIAL_PORTS; p++) {
            for (int k = 0; s_up[p] && k < 8; k++) {
                size_t m = xMessageBufferReceive(s_up[p], s_io_buf, IO_BUF_BYTES, 0);
                if (!m)
                    break;
                uplink(p + 1, s_io_buf, m);
            }
        }
        if (s_up_drops != seen_up || s_down_drops != seen_down) {
            seen_up = s_up_drops, seen_down = s_down_drops;
            SLOG_W("mqtt", "queue full: %lu serial frames and %lu broker messages dropped so far", (unsigned long)seen_up,
                   (unsigned long)seen_down);
        }
    }
}

/** serial_rx[n]: queue the frame, never wait. */
static void on_serial(int port, const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    if (xMessageBufferSend(s_up[port - 1], data, len, 0) != len)
        s_up_drops++;
}

// ---- the mqtt task ---------------------------------------------------------------------------------------

static int l_send(void *c, const uint8_t *d, size_t n) { return conn_send_some(c, d, n); }
static int l_recv(void *c, uint8_t *b, size_t n) { return conn_recv(c, b, n); }
static bool l_pending(void *c) { return conn_pending(c); }
static int l_wait(void *c, bool write, uint32_t ms)
{
    conn_t *k = c;
    if (k->fd < 0)
        return -1;
    fd_set s;
    FD_ZERO(&s);
    FD_SET(k->fd, &s);
    struct timeval tv = {.tv_sec = 0, .tv_usec = (long)ms * 1000};
    return lwip_select(k->fd + 1, write ? NULL : &s, write ? &s : NULL, NULL, &tv);
}
static void l_tick(void) { vTaskDelay(1); }

// coreMQTT's events, inside MQTT_ProcessLoop: outbox state changes, and broker messages onto the down-ring.
static void on_event(MQTTContext_t *c, MQTTPacketInfo_t *p, MQTTDeserializedInfo_t *d)
{
    (void)c;
    if ((p->type & 0xF0U) != MQTT_PACKET_TYPE_PUBLISH) {
        ms_ack(&s_sess, p->type, d->packetIdentifier);
        return;
    }
    const MQTTPublishInfo_t *pi = d->pPublishInfo;
    touch_rx();
    svc_net_activity();
    static uint8_t rec[2 + NETBUF_BYTES] AXI_BSS;  // topic and payload came in one packet of the network buffer
    size_t tl = pi->topicNameLength, n = 2 + tl + pi->payloadLength;
    if (n > sizeof rec) {
        s_down_drops++;
        return;
    }
    rec[0] = (uint8_t)tl, rec[1] = (uint8_t)(tl >> 8);
    memcpy(rec + 2, pi->pTopicName, tl);
    memcpy(rec + 2 + tl, pi->pPayload, pi->payloadLength);
    if (xMessageBufferSend(s_down, rec, n, 0) != n)
        s_down_drops++;
}

static void drop_connection(void)
{
    conn_close(&s_conn);
    s_ctx.connectStatus = MQTTNotConnected;  // not MQTT_Disconnect: a lost connection keeps the will (OASIS 3.1.1 §3.1.2.5)
    s_online = false;
}

static bool dial(void)
{
    int fd = net_tcp_connect(C.host, C.port, 0, 10000);
    if (fd < 0)
        return false;
    s_conn.fd = fd;
    s_conn.tls = NULL;
    if (C.tls) {
        s_conn.tls = tls_open(fd, "mqtt", C.name, C.verify, 10000, NULL);  // the certificates are read each time
        if (!s_conn.tls) {
            conn_close(&s_conn);
            return false;
        }
        static bool s_logged;
        if (!s_logged) {
            s_logged = true;
            SLOG_I("mqtt", "stack headroom after the TLS handshake: %u bytes",
                   (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
        }
    }
    MQTTConnectInfo_t ci = {.cleanSession = C.clean, .keepAliveSeconds = (uint16_t)C.keepalive, .pClientIdentifier = C.client,
                            .clientIdentifierLength = (uint16_t)strlen(C.client), .mqtt31 = C.mqtt31};
    if (C.auth) {
        ci.pUserName = C.user, ci.userNameLength = (uint16_t)strlen(C.user);
        ci.pPassword = C.pass, ci.passwordLength = (uint16_t)strlen(C.pass);
    }
    MQTTPublishInfo_t will = {.qos = (MQTTQoS_t)C.will_qos, .retain = C.will_retain, .pTopicName = C.will_topic,
                              .topicNameLength = (uint16_t)strlen(C.will_topic), .pPayload = C.will_msg,
                              .payloadLength = strlen(C.will_msg)};
    bool kept = false;
    MQTTStatus_t r = MQTT_Connect(&s_ctx, &ci, C.will ? &will : NULL, 10000, &kept);
    if (r != MQTTSuccess) {
        if (r == MQTTServerRefused) {
            SLOG_W("mqtt", "the broker refused the connection");
            set_state("REFUSED");
        }
        drop_connection();
        return false;
    }
    s_online = true;
    touch_rx();
    set_state("CONNECTED");
    SLOG_I("mqtt", "connected to %s:%u (MQTT %s, %s session)", C.host, C.port, C.mqtt31 ? "3.1" : "3.1.1", kept ? "kept" : "new");
    if (ms_resume(&s_sess, kept) != MQTTSuccess) {
        drop_connection();
        return false;
    }
    return true;
}

/** retryInterval, whatever is queued meanwhile: a notify must not shorten it (each frame queued offline notifies). */
/** In 1 s slices with a heartbeat between them: the wait is work, its length unchanged (Plan 11a task 0b). */
static void retry_wait(void)
{
    for (uint32_t left = (uint32_t)(C.retry_s > 0 ? C.retry_s : 3) * 1000u; left;) {
        uint32_t slice = left < 1000u ? left : 1000u;
        vTaskDelay(pdMS_TO_TICKS(slice));
        left -= slice;
        wdog_hb_beat(s_hb_mqtt);
    }
}

static void mqtt_task(void *arg)
{
    (void)arg;
    s_hb_mqtt = wdog_hb_add("mqtt", 120000);  // a dial with its TLS handshake fits well inside
    for (;;) {
        wdog_hb_beat(s_hb_mqtt);
        if (!netmgr_online() || !dial()) {
            if (strcmp(s_state, "REFUSED") != 0)
                set_state("CONNECTING");
            retry_wait();
            continue;
        }
        for (unsigned turn = 1;; turn++) {
            wdog_hb_beat(s_hb_mqtt);
            ulTaskNotifyTake(pdTRUE, 0);
            if (turn % REST_EVERY == 0)
                vTaskDelay(1);  // a broker that never pauses, or an outbox always fed, must not starve lower tasks
            if (ms_subscribe(&s_sess) != MQTTSuccess || ms_feed(&s_sess) != MQTTSuccess)
                break;
            MQTTStatus_t r = MQTT_ProcessLoop(&s_ctx);  // waits up to MQTT_WAIT_MS in the transport when idle
            if (r != MQTTSuccess && r != MQTTNeedMoreBytes) {
                SLOG_W("mqtt", "connection lost (%s)", MQTT_Status_strerror(r));
                break;
            }
            if (C.silent_s > 0 && now_us() - last_rx() >= (uint64_t)C.silent_s * 1000000u) {
                SLOG_W("mqtt", "nothing received for %d s, reconnecting", C.silent_s);
                break;
            }
        }
        drop_connection();
        set_state("CONNECTING");
        retry_wait();
    }
}

// ---- start --------------------------------------------------------------------------------------------

static bool s_enabled;  // on at boot (Plan 10c)

bool mqttgw_enabled(void) { return s_enabled; }

void mqttgw_start(const cJSON *sys)
{
    cJSON *c = store_get("mqtt");
    if (!c) {
        set_state("NO MEMORY");
        SLOG_E("mqtt", "its config cannot be read (no memory): MQTT stays off");
        return;
    }
    bool on = num(c, "enabled") == 1;
    s_enabled = on;
    if (on)
        read_cfg(c);
    cJSON_Delete(c);
    if (!on)
        return;
    s_keep_offline = num(sys, "bufferOffline") == 1;
    if (C.tls && C.verify >= 1) {  // a check asked for but impossible: MQTT stays off and says why
        char *ca = store_cert_get("mqtt", "ca"), *cert = C.verify == 2 ? store_cert_get("mqtt", "cert") : NULL,
             *key = C.verify == 2 ? store_cert_get("mqtt", "key") : NULL;
        bool no_ca = !ca, no_client = C.verify == 2 && (!cert || !key);
        free(ca), free(cert), free(key);
        if (no_ca || no_client) {
            set_state(no_ca ? "NO CA CERTIFICATE" : "NO CLIENT CERTIFICATE");
            SLOG_E("mqtt", "%s: upload it (MQTT stays off)", no_ca ? "server verification needs a CA certificate"
                                                                  : "mutual TLS needs a client certificate and key");
            return;
        }
    }
    s_npub = load_topics("mqttPublish", s_pub);
    s_nsub = load_topics("mqttSubscribe", s_sub);
    uint8_t *netbuf = s_npub >= 0 && s_nsub >= 0 ? malloc(NETBUF_BYTES) : NULL;
    s_io_buf = netbuf ? malloc(IO_BUF_BYTES) : NULL;
    if (!s_io_buf) {  // never a gateway CONNECTED without its topics
        free(netbuf);
        set_state("NO MEMORY");
        SLOG_E("mqtt", "no memory for %s: MQTT stays off", s_npub < 0 || s_nsub < 0 ? "its topic lists" : "its buffers");
        return;
    }
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_mem);
    ob_init(&s_ob, s_ob_mem, sizeof s_ob_mem);
    for (int i = 0; i < s_nsub; i++)
        subs_add(&s_subs, s_sub[i].topic, s_sub[i].qos, i);
    s_down = xMessageBufferCreateStatic(DOWN_BYTES, s_down_mem, &s_down_cb);
    uint8_t ports = C.custom ? C.custom_ports : 0;
    for (int i = 0; i < s_npub; i++)
        ports |= s_pub[i].ports;
    for (int p = 0; p < SERIAL_PORTS; p++)
        if (ports & (1u << p)) {
            s_up[p] = xMessageBufferCreateStatic(UP_BYTES, s_up_mem[p], &s_up_cb[p]);
            serial_subscribe(p + 1, on_serial, NULL);
        }
    s_link = (mqtt_link_t){.send = l_send, .recv = l_recv, .pending = l_pending, .wait = l_wait, .tick = l_tick, .conn = &s_conn};
    s_nc = (NetworkContext_t){.link = &s_link};
    TransportInterface_t t = {.recv = mqtt_rx, .send = mqtt_tx, .pNetworkContext = &s_nc};
    MQTTFixedBuffer_t fb = {.pBuffer = netbuf, .size = NETBUF_BYTES};
    // Once for the whole run: MQTT_Init resets the packet ids (Codex plan r1).
    MQTT_Init(&s_ctx, &t, now_ms, on_event, &fb);
    MQTT_InitStatefulQoS(&s_ctx, s_out_rec, OB_MAX_IN_FLIGHT, s_in_rec, OB_MAX_IN_FLIGHT);
    s_sess = (mqtt_sess_t){.ctx = &s_ctx, .ob = &s_ob, .subs = &s_subs, .lock = lock_wait, .unlock = unlock, .now_ms = now_ms};
    s_running = true;
    set_state("CONNECTING");
    static StaticTask_t mtcb, itcb;
    static StackType_t mstack[MQTT_STACK_WORDS], istack[IO_STACK_WORDS];
    s_mqtt_task = xTaskCreateStatic(mqtt_task, "mqtt", MQTT_STACK_WORDS, NULL, PRIO, mstack, &mtcb);
    s_io_task = xTaskCreateStatic(io_task, "mqtt_io", IO_STACK_WORDS, NULL, PRIO, istack, &itcb);
    SLOG_I("mqtt", "broker %s:%u%s, MQTT %s, client %s, %d publish / %d subscribe topics%s", C.host, C.port, C.tls ? " (TLS)" : "",
           C.mqtt31 ? "3.1" : "3.1.1", C.client, s_npub, s_nsub, C.custom ? ", custom mode" : "");
}

void mqttgw_status(char *state, size_t len)
{
    taskENTER_CRITICAL();
    snprintf(state, len, "%s", s_state);
    taskEXIT_CRITICAL();
}
