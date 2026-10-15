#include "serial.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "blog.h"
#include "netmgr.h"
#include "now_us.h"
#include "queue.h"
#include "semphr.h"
#include "serial_frame.h"
#include "syslog.h"
#include "sysmgr.h"
#include "task.h"
#include "uart_drv.h"

#define MAX_SUBSCRIBERS 6
#define QUEUE_LEN 32
#define RX_STACK_WORDS 768  // 3 KB (spec §6.1)
#define RX_PRIO 26          // above tcpip (24) and eth_rx (22): no serial byte lost to network load
#define HB_STACK_WORDS 512  // 2 KB
#define HB_PRIO 4

typedef struct {
    int n;  // 1 or 2
    bool started;
    sf_line_t line;
    bool mask7;  // 7 data bits and parity: the USART's word carries the parity in bit 7
    sf_rx_t rx;  // serial_rx[n] only
    uint32_t rd;
    uint32_t seen_wr;  // the DMA position a writer saw last (rx_quiet_since, under the bus mutex)
    uint32_t lost_seen;
    bool paused_seen;
    QueueHandle_t q;
    StaticQueue_t q_mem;
    uint8_t q_store[QUEUE_LEN * sizeof(uart_ev_t)];
    struct {
        serial_rx_fn fn;
        void *ctx;
    } subs[MAX_SUBSCRIBERS];
    int nsubs;
    SemaphoreHandle_t bus, txn_lock, txn_done;
    StaticSemaphore_t bus_mem, txn_lock_mem, txn_done_mem;
    volatile bool txn, txn_dropped;
    bool tail;  // a broken answer's tail: RTU timing until t3.5 of silence ends the resync (txn_lock)
    uint8_t *txn_buf;
    size_t txn_max, txn_len;
    int txn_unit;
    uint32_t gap_ms, reply_window_ms, idle_ms;
    bool anticollision;
    volatile uint64_t last_rx_us, last_tx_us, last_us, reply_until_us;  // under a critical section (64 bits)
    volatile uint32_t tx, rxc;
    // heartbeat
    uint32_t hb_period_s;
    int hb_content;
    bool hb_hex;
    char hb_data[41];
    StaticTask_t rx_tcb, hb_tcb;
    StackType_t rx_stack[RX_STACK_WORDS], hb_stack[HB_STACK_WORDS];
} sport_t;

static sport_t s_ports[SERIAL_PORTS];

static sport_t *P(int port)
{
    return port >= 1 && port <= SERIAL_PORTS && s_ports[port - 1].started ? &s_ports[port - 1] : NULL;
}

static uint64_t ts_get(volatile uint64_t *t)
{
    taskENTER_CRITICAL();
    uint64_t v = *t;
    taskEXIT_CRITICAL();
    return v;
}

static void ts_set(volatile uint64_t *t, uint64_t v)
{
    taskENTER_CRITICAL();
    *t = v;
    taskEXIT_CRITICAL();
}

static TickType_t ticks(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    return t ? t : 1;
}

static int num(const cJSON *o, const char *k, int dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valueint : dflt;
}

int serial_subscribe(int port, serial_rx_fn fn, void *ctx)
{
    sport_t *p = P(port);
    if (!p || p->nsubs >= MAX_SUBSCRIBERS)
        return -1;
    p->subs[p->nsubs].ctx = ctx;
    p->subs[p->nsubs].fn = fn;
    p->nsubs++;
    return 0;
}

// ---- RX: serial_rx[n] -------------------------------------------------------------------------------

// A complete frame, from sf_rx (in serial_rx[n]).
static void deliver(void *ctx, const uint8_t *data, size_t len)
{
    sport_t *p = ctx;
    xSemaphoreTake(p->txn_lock, portMAX_DELAY);
    // The bus owner gets what arrives while it waits, unless it is plainly another slave's frame
    // (a late answer to a transparent request): that one still belongs to the subscribers.
    bool ours = p->txn && !p->txn_len && !(p->txn_unit >= 0 && len > 0 && data[0] != p->txn_unit);
    if (ours) {
        size_t n = len < p->txn_max ? len : p->txn_max;
        memcpy(p->txn_buf, data, n);
        p->txn_len = n;
        xSemaphoreGive(p->txn_done);
        xSemaphoreGive(p->txn_lock);
        return;
    }
    xSemaphoreGive(p->txn_lock);
    ts_set(&p->reply_until_us, 0);  // the awaited answer (if any) is in: the bus is free for transactions
    for (int i = 0; i < p->nsubs; i++)
        p->subs[i].fn(p->n, data, len, p->subs[i].ctx);
}

static void rx_task(void *arg)
{
    sport_t *p = arg;
    p->rd = uart_drv_wr(p->n);
    for (;;) {
        uart_ev_t ev;
        xQueueReceive(p->q, &ev, portMAX_DELAY);
        uint32_t errors, lost;
        uart_drv_counters(p->n, &errors, &lost);
        if (lost != p->lost_seen) {  // events were lost: start over from where the DMA is
            p->lost_seen = lost;
            sf_rx_overrun(&p->rx);
            p->rd = uart_drv_wr(p->n);
        }
        // Events queue up behind a busy reader: their positions are checked against the DMA's now.
        uint32_t rd0 = p->rd;
        p->rxc += sf_rx_ring(&p->rx, uart_drv_ring(p->n), UART_RING, &p->rd, ev.wr, uart_drv_wr(p->n), ev.type == UART_EV_ERROR, p->mask7);
        if (p->rd != rd0) {
            uint64_t now = now_us();
            ts_set(&p->last_rx_us, now);
            ts_set(&p->last_us, now);
        }
        switch (ev.type) {
        case UART_EV_GAP:
        case UART_EV_RTU_END:
            sf_rx_end(&p->rx);
            break;
        case UART_EV_RTU_VIOL:
            sf_rx_violation(&p->rx);
            if (p->txn) {  // the answer broke t1.5: the transaction ends with it
                p->txn_dropped = true;
                xSemaphoreGive(p->txn_done);
            }
            break;
        case UART_EV_MODE:
            sf_rx_mode(&p->rx, (sf_mode_t)ev.arg);
            break;
        default:
            break;
        }
        // The tail of a broken answer has ended with t3.5 of silence: back to transparent.
        xSemaphoreTake(p->txn_lock, portMAX_DELAY);
        if (p->tail && !p->rx.resync) {
            p->tail = false;
            uart_drv_set_timing(p->n, SF_TRANSPARENT);
            sf_rx_mode(&p->rx, SF_TRANSPARENT);
        }
        xSemaphoreGive(p->txn_lock);
        if (p->rx.tx_paused != p->paused_seen) {
            p->paused_seen = p->rx.tx_paused;
            uart_drv_flow(p->n, p->paused_seen);
        }
    }
}

// ---- TX ---------------------------------------------------------------------------------------------

// Since when nothing came in, as a writer can tell. serial_rx[n] stamps its reads, which wait for half
// a ring or a silence; a frame still arriving shows first as the DMA's position moving, or as BUSY.
// Under the bus mutex.
static uint64_t rx_quiet_since(sport_t *p)
{
    uint32_t wr = uart_drv_wr(p->n);
    if (wr != p->seen_wr || uart_drv_rx_busy(p->n)) {
        p->seen_wr = wr;
        ts_set(&p->last_rx_us, now_us());
    }
    return ts_get(&p->last_rx_us);
}

// 485 collision avoidance: wait until nothing was received for rs485IdleTime.
static void wait_idle_bus(sport_t *p)
{
    if (!p->anticollision || !p->idle_ms)
        return;
    for (int guard = 0; guard < 200; guard++) {
        uint64_t since = rx_quiet_since(p);  // first: it may stamp now
        uint64_t quiet_ms = (now_us() - since) / 1000u;
        if (quiet_ms >= p->idle_ms)
            return;
        vTaskDelay(ticks((uint32_t)(p->idle_ms - quiet_ms + 1)));
    }
}

static int send_raw(sport_t *p, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    wait_idle_bus(p);
    int r = uart_drv_write(p->n, data, len, timeout_ms);
    if (r == 0)
        p->tx += (uint32_t)len;
    uint64_t now = now_us();
    ts_set(&p->last_us, now);
    ts_set(&p->last_tx_us, now);
    return r;
}

int serial_write(int port, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    sport_t *p = P(port);
    if (!p)
        return -1;
    if (xSemaphoreTake(p->bus, ticks(timeout_ms)) != pdTRUE) {
        SLOG_W("serial", "UART%d busy for %lu ms, %u bytes not sent", port, (unsigned long)timeout_ms, (unsigned)len);
        return -1;
    }
    int r = send_raw(p, data, len, timeout_ms);
    ts_set(&p->reply_until_us, now_us() + (uint64_t)p->reply_window_ms * 1000u);
    xSemaphoreGive(p->bus);
    return r;
}

int serial_transact(int port, const uint8_t *req, size_t len, uint8_t *resp, size_t max, int unit, uint32_t timeout_ms)
{
    sport_t *p = P(port);
    if (!p || xSemaphoreTake(p->bus, ticks(timeout_ms + p->reply_window_ms + 1000u)) != pdTRUE)
        return -1;
    // An answer to a transparent write is still due: leave it to the subscribers. Bounded by the
    // window, so a busy downlink delays a poll but never starves it.
    while (now_us() < ts_get(&p->reply_until_us))
        vTaskDelay(ticks(5));
    // A frame still arriving belongs to the subscribers: let it finish first.
    for (uint64_t since; since = rx_quiet_since(p), (now_us() - since) / 1000u < p->gap_ms + 1u;)
        vTaskDelay(ticks(p->gap_ms + 1u));
    xSemaphoreTake(p->txn_lock, portMAX_DELAY);
    p->tail = false;  // serial_rx[n] no longer switches to transparent: this transaction owns the mode
    xSemaphoreGive(p->txn_lock);
    uart_drv_set_mode(port, SF_RTU);
    xSemaphoreTake(p->txn_lock, portMAX_DELAY);
    xSemaphoreTake(p->txn_done, 0);  // drop a stale signal
    p->txn_buf = resp;
    p->txn_max = max;
    p->txn_len = 0;
    p->txn_unit = unit;
    p->txn_dropped = false;
    p->txn = true;
    xSemaphoreGive(p->txn_lock);
    int got = send_raw(p, req, len, 1000) ? -1 : 0;
    if (got == 0)
        xSemaphoreTake(p->txn_done, ticks(timeout_ms));
    xSemaphoreTake(p->txn_lock, portMAX_DELAY);
    p->txn = false;
    if (got == 0)
        got = p->txn_dropped ? SERIAL_DROPPED : (int)p->txn_len;
    // The tail of a broken answer is dropped up to t3.5 of silence, however long it runs: the port stays
    // in RTU timing until then, and serial_rx[n] switches it back (it ends the resync, under txn_lock).
    bool tail = got == SERIAL_DROPPED && p->rx.resync;
    p->tail = tail;
    xSemaphoreGive(p->txn_lock);
    if (!tail)
        uart_drv_set_mode(port, SF_TRANSPARENT);
    xSemaphoreGive(p->bus);
    return got;
}

void serial_counters(int port, serial_counters_t *c)
{
    memset(c, 0, sizeof *c);
    sport_t *p = P(port);
    if (!p)
        return;
    uint32_t errors;
    c->tx = p->tx;
    c->rx = p->rxc;
    c->overruns = p->rx.overruns;
    c->line_errors = p->rx.line_errors;
    c->violations = p->rx.violations;
    c->dropped = p->rx.dropped;
    uart_drv_counters(port, &errors, &c->lost);
}

uint64_t serial_last_activity_us(int port)
{
    sport_t *p = P(port);
    return p ? ts_get(&p->last_us) : 0;
}

// ---- heartbeat --------------------------------------------------------------------------------------

// serialHeartbeatContent: 0 custom, 1 IMEI, 2 serial number, 3 ICCID, 4 MAC (the ESP32's sys_payload).
static size_t payload(sport_t *p, uint8_t *out, size_t max)
{
    char text[24] = "";
    if (p->hb_content == 0)
        return sf_payload_custom(p->hb_data, p->hb_hex, out, max);
    if (p->hb_content == 2) {
        snprintf(text, sizeof text, "%s", sysmgr_serial());
    } else if (p->hb_content == 4) {
        net_status_t ns;
        netmgr_status(&ns);
        snprintf(text, sizeof text, "%02x:%02x:%02x:%02x:%02x:%02x", ns.mac[0], ns.mac[1], ns.mac[2], ns.mac[3], ns.mac[4], ns.mac[5]);
    }  // 1 IMEI, 3 ICCID: the modem comes with Plan 10; nothing to send until then, as the ESP32 without a modem
    size_t n = strlen(text) < max ? strlen(text) : max;
    memcpy(out, text, n);
    return n;
}

// A fixed payload to the device when nothing was sent to it for serialHeartbeatInterval seconds.
static void hb_task(void *arg)
{
    sport_t *p = arg;
    uint8_t buf[64];
    uint64_t period_us = (uint64_t)p->hb_period_s * 1000000u;
    ts_set(&p->last_tx_us, now_us());
    for (;;) {
        int64_t wait_us = (int64_t)(ts_get(&p->last_tx_us) + period_us) - (int64_t)now_us();
        if (wait_us > 0) {
            vTaskDelay(ticks((uint32_t)(wait_us / 1000) + 1));
            continue;
        }
        size_t n = payload(p, buf, sizeof buf);
        if (n)
            serial_write(p->n, buf, n, 1000);
        else
            ts_set(&p->last_tx_us, now_us());  // nothing to send yet: try again next period
    }
}

// ---- start ------------------------------------------------------------------------------------------

int serial_start(int port, const cJSON *pcfg, const cJSON *sys)
{
    if (port < 1 || port > SERIAL_PORTS)
        return -1;
    sport_t *p = &s_ports[port - 1];
    memset(p, 0, sizeof *p);
    p->n = port;
    p->line.baud = (uint32_t)num(pcfg, "baudRate", 115200);
    p->line.data_bits = (uint8_t)(num(pcfg, "dataBits", 8) == 7 ? 7 : 8);
    p->line.parity = (uint8_t)num(pcfg, "parity", 0);
    p->line.stop_bits = (uint8_t)(num(pcfg, "stopBits", 1) == 2 ? 2 : 1);
    p->mask7 = p->line.data_bits == 7 && p->line.parity;
    uint32_t packet_len = (uint32_t)num(pcfg, "packetLength", 0);
    p->gap_ms = sf_gap_ms(&p->line, (uint32_t)num(pcfg, "packetInterval", 0));
    p->anticollision = num(sys, "rs485CollisionAvoid", 0) == 1;
    p->idle_ms = (uint32_t)num(sys, "rs485IdleTime", 10);
    // Reply window = the port's response timeout (modbusPollTimeout), at least two frame gaps, at most 2 s.
    p->reply_window_ms = (uint32_t)num(pcfg, "modbusPollTimeout", 200);
    if (p->reply_window_ms < 2 * p->gap_ms)
        p->reply_window_ms = 2 * p->gap_ms;
    if (p->reply_window_ms > 2000)
        p->reply_window_ms = 2000;
    bool xonxoff = num(pcfg, "flowControl", 0) == 2;
    sf_rx_init(&p->rx, SF_TRANSPARENT, packet_len, xonxoff, deliver, p);

    p->q = xQueueCreateStatic(QUEUE_LEN, sizeof(uart_ev_t), p->q_store, &p->q_mem);
    p->bus = xSemaphoreCreateMutexStatic(&p->bus_mem);
    p->txn_lock = xSemaphoreCreateMutexStatic(&p->txn_lock_mem);
    p->txn_done = xSemaphoreCreateBinaryStatic(&p->txn_done_mem);
    if (uart_drv_init(port, &p->line, p->gap_ms * 1000u, xonxoff, p->q)) {
        SLOG_E("serial", "UART%d could not be set up at %lu baud", port, (unsigned long)p->line.baud);
        return -1;
    }
    p->started = true;
    char name[12];
    snprintf(name, sizeof name, "serial_rx%d", port);
    xTaskCreateStatic(rx_task, name, RX_STACK_WORDS, p, RX_PRIO, p->rx_stack, &p->rx_tcb);

    const cJSON *hb = cJSON_GetObjectItemCaseSensitive(pcfg, "serialHeartbeat");
    p->hb_period_s = (uint32_t)num(pcfg, "serialHeartbeatInterval", 0);
    if (cJSON_IsTrue(hb) && p->hb_period_s) {
        p->hb_content = num(pcfg, "serialHeartbeatContent", 0);
        p->hb_hex = num(pcfg, "serialHeartbeatHex", 0) == 1;
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(pcfg, "serialHeartbeatData");
        snprintf(p->hb_data, sizeof p->hb_data, "%s", cJSON_IsString(d) ? d->valuestring : "");
        snprintf(name, sizeof name, "serial_hb%d", port);
        xTaskCreateStatic(hb_task, name, HB_STACK_WORDS, p, HB_PRIO, p->hb_stack, &p->hb_tcb);
    }
    char ca[32] = "";
    if (p->anticollision)
        snprintf(ca, sizeof ca, ", bus idle %lu ms", (unsigned long)p->idle_ms);
    blog("serial: UART%d %lu %u%c%u, gap %lu ms, max %lu bytes, reply window %lu ms%s%s\r\n", port, (unsigned long)p->line.baud,
         p->line.data_bits, "NOE"[p->line.parity % 3], p->line.stop_bits, (unsigned long)p->gap_ms,
         (unsigned long)p->rx.packet_len, (unsigned long)p->reply_window_ms, xonxoff ? ", XON/XOFF" : "", ca);
    return 0;
}
