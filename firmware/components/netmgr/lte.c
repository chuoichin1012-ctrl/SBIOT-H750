// The LTE modem (spec §7.2, Plan 10). See lte.h.
// - cmux_rx: the only reader of UART3's ring and the only writer to UART3, and the only user of the cmux_t.
//   In AT mode the bytes go to the AT stream; with CMUX, DLC2 goes there and DLC1 to PPP (Plan 10 T4).
// - lte: the power sequence (lte_power), AT over the plain UART then over DLC2, the identity, the SIM, the
//   CMUX opening, and the polling of the signal every 20 s.
#include "lte.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "at_parse.h"
#include "blog.h"
#include "cmux.h"
#include "lte_power.h"
#include "lte_uart.h"
#include "main.h"  // LTE_PWR, LTE_RESET (through Q1/Q2: high is pressed)
#include "now_us.h"
#include "stream_buffer.h"
#include "syslog.h"
#include "task.h"

#define AXI __attribute__((section(".axi_bss")))
#define CMUX_STACK_WORDS (2048 / sizeof(StackType_t))
#define LTE_STACK_WORDS (6144 / sizeof(StackType_t))
#define CMUX_PRIO 25  // above tcpip (spec §6.1): no byte lost to network load
#define LTE_PRIO 5
#define POLL_MS 20000
#define AT_OUT_SIZE 512u
#define AT_IN_SIZE 1024u
#define PPP_OUT_SIZE 4096u
#define CMUX_CMD "AT+CMUX=0,0,5,2048,20,3,30,10,2"

enum { M_AT, M_MUX, M_PLAIN_PPP };
enum { REQ_MUX_START = 1, REQ_MUX_CLOSE = 2 };
enum { EV_RUNNING = 1 << CMUX_EV_RUNNING, EV_CLOSED = 1 << CMUX_EV_DLC_CLOSED, EV_FAILED = 1 << CMUX_EV_FAILED };

static net_cfg_t s_cfg;
static cmux_t s_mux AXI;
static uint8_t s_at_out_mem[AT_OUT_SIZE + 1] AXI, s_at_in_mem[AT_IN_SIZE + 1] AXI, s_ppp_out_mem[PPP_OUT_SIZE + 1] AXI;
static StaticStreamBuffer_t s_at_out_sb, s_at_in_sb, s_ppp_out_sb;
static StreamBufferHandle_t s_at_out, s_at_in, s_ppp_out;
static StackType_t s_cmux_stack[CMUX_STACK_WORDS], s_lte_stack[LTE_STACK_WORDS];
static StaticTask_t s_cmux_tcb, s_lte_tcb;
static TaskHandle_t s_cmux_task, s_lte_task;
static volatile int s_mode = M_AT;
static volatile uint32_t s_req, s_events, s_last_rx_ms;
static volatile uint32_t s_overrun, s_in_drop, s_out_drop;
static lte_info_t s_info = {.csq = 99, .state = "disconnected", .modem = "UNKNOWN", .mux = "AT", .sim = ""};
static uint32_t s_release_ms;  // the last PWRKEY or RESET_N release, for ready_ms

static uint32_t ms(void) { return (uint32_t)(now_us() / 1000u); }

// ---- cmux_rx ----------------------------------------------------------------------------------------

static void ppp_input(const uint8_t *p, size_t n)
{
    (void)p;
    (void)n;  // PPP comes with Plan 10 T4
}

static void at_input(const uint8_t *p, size_t n)
{
    if (xStreamBufferSend(s_at_in, p, n, 0) != n)
        s_in_drop++;  // the lte task does not read: lines are lost, its command times out
}

static void mux_data(void *ctx, int dlc, const uint8_t *p, size_t n)
{
    (void)ctx;
    if (dlc == 2)
        at_input(p, n);
    else if (dlc == 1)
        ppp_input(p, n);
}

static void mux_event(void *ctx, cmux_event_t ev, int dlc)
{
    (void)ctx;
    (void)dlc;
    s_events |= 1u << ev;
    xTaskNotifyGive(s_lte_task);
}

static const cmux_io_t k_mux_io = {mux_data, mux_event, NULL};

/** From a stream buffer into a DLC's queue, as much as it has room for. */
static void feed_dlc(StreamBufferHandle_t sb, int dlc)
{
    uint8_t b[256];
    size_t room, n;
    while ((room = cmux_room(&s_mux, dlc)) > 0 && (n = xStreamBufferReceive(sb, b, room < sizeof b ? room : sizeof b, 0)) > 0)
        cmux_send(&s_mux, dlc, b, n);
}

static void cmux_task(void *arg)
{
    (void)arg;
    uint32_t rd = lte_uart_wr();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));  // HT, TC, IDLE, a request; and the CMUX timers
        uint32_t now = ms();
        taskENTER_CRITICAL();
        uint32_t req = s_req;
        s_req = 0;
        taskEXIT_CRITICAL();
        if (req & REQ_MUX_CLOSE && s_mode == M_MUX) {
            cmux_close(&s_mux);
            size_t n;
            while ((n = cmux_tx_next(&s_mux, lte_uart_txbuf(), LTE_UART_TXBUF)))
                lte_uart_write(lte_uart_txbuf(), n, 100);
            s_mode = M_AT;
        }
        if (req & REQ_MUX_START) {
            cmux_init(&s_mux, &k_mux_io);
            cmux_start(&s_mux, now);
            s_mode = M_MUX;
        }
        // RX: everything the DMA wrote since the last pass
        uint32_t wr = lte_uart_wr();
        if (wr - rd > LTE_UART_RING) {  // lapped: those bytes are gone (spec §7.2: counted, PPP recovers)
            s_overrun++;
            rd = wr;
        }
        const uint8_t *ring = lte_uart_ring();
        while (rd != wr) {
            uint32_t off = rd % LTE_UART_RING, n = wr - rd;
            if (n > LTE_UART_RING - off)
                n = LTE_UART_RING - off;
            if (s_mode == M_MUX)
                cmux_input(&s_mux, ring + off, n, now);
            else if (s_mode == M_PLAIN_PPP)
                ppp_input(ring + off, n);
            else
                at_input(ring + off, n);
            rd += n;
        }
        // TX
        if (s_mode == M_MUX) {
            cmux_tick(&s_mux, now);
            feed_dlc(s_at_out, 2);
            feed_dlc(s_ppp_out, 1);
            size_t n;
            while ((n = cmux_tx_next(&s_mux, lte_uart_txbuf(), LTE_UART_TXBUF)))
                lte_uart_write(lte_uart_txbuf(), n, 100);
            s_last_rx_ms = cmux_last_rx_ms(&s_mux);
        } else {
            StreamBufferHandle_t sb = s_mode == M_PLAIN_PPP ? s_ppp_out : s_at_out;
            size_t n;
            while ((n = xStreamBufferReceive(sb, lte_uart_txbuf(), LTE_UART_TXBUF, 0)))
                lte_uart_write(lte_uart_txbuf(), n, 100);
        }
    }
}

static void request(uint32_t r)
{
    taskENTER_CRITICAL();
    s_req |= r;
    taskEXIT_CRITICAL();
    xTaskNotifyGive(s_cmux_task);
}

// ---- AT ---------------------------------------------------------------------------------------------

typedef void (*line_fn)(const char *line, void *ctx);

/** One command over the current AT channel (the plain UART, or DLC2): its final result, AT_NONE on a timeout.
 *  Each other line goes to fn. */
static at_final_t at_cmd(const char *cmd, uint32_t timeout_ms, line_fn fn, void *ctx)
{
    static at_lines_t lines;
    uint8_t b[64];
    while (xStreamBufferReceive(s_at_in, b, sizeof b, 0)) {
    }  // whatever is left is late
    lines.n = 0;
    xStreamBufferSend(s_at_out, cmd, strlen(cmd), pdMS_TO_TICKS(100));
    xStreamBufferSend(s_at_out, "\r", 1, pdMS_TO_TICKS(100));
    xTaskNotifyGive(s_cmux_task);
    uint32_t t0 = ms();
    for (;;) {
        uint32_t spent = ms() - t0;
        if (spent >= timeout_ms)
            return AT_NONE;
        size_t n = xStreamBufferReceive(s_at_in, b, sizeof b, pdMS_TO_TICKS(timeout_ms - spent));
        for (size_t i = 0; i < n; i++) {
            const char *line = at_feed(&lines, b[i]);
            if (!line || !strcmp(line, cmd))
                continue;  // the echo, before ATE0
            at_final_t f = at_final(line);
            if (f != AT_NONE)
                return f;
            if (fn)
                fn(line, ctx);
        }
    }
}

static void info_set(const char **field, const char *v)
{
    taskENTER_CRITICAL();
    *field = v;
    taskEXIT_CRITICAL();
}

// lte_power's hardware
static bool hw_at_ok(void *ctx, uint32_t window_ms)
{
    (void)ctx;
    uint32_t t0 = ms();
    while (ms() - t0 < window_ms) {
        uint32_t a = ms();
        if (at_cmd("AT", 500, NULL, NULL) == AT_OK) {
            if (s_release_ms)
                s_info.ready_ms = ms() - s_release_ms;
            return true;
        }
        uint32_t spent = ms() - a;
        if (spent < 500)
            vTaskDelay(pdMS_TO_TICKS(500 - spent));
    }
    return false;
}

static void hw_pulse(void *ctx, int pin, uint32_t len_ms)
{
    (void)ctx;
    GPIO_TypeDef *port = pin == LTE_PIN_PWRKEY ? LTE_PWR_GPIO_Port : LTE_RESET_GPIO_Port;
    uint16_t bit = pin == LTE_PIN_PWRKEY ? LTE_PWR_Pin : LTE_RESET_Pin;
    SLOG_I("lte", "%s %lu ms", pin == LTE_PIN_PWRKEY ? "PWRKEY" : "RESET_N", (unsigned long)len_ms);
    HAL_GPIO_WritePin(port, bit, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(len_ms));
    HAL_GPIO_WritePin(port, bit, GPIO_PIN_RESET);
    s_release_ms = ms();
}

static void hw_log(void *ctx, const char *msg)
{
    (void)ctx;
    SLOG_E("lte", "%s", msg);
}

static const lte_hw_t k_hw = {hw_at_ok, hw_pulse, hw_log, NULL};

// reply lines
static void on_imei(const char *line, void *ctx)
{
    char v[16];
    if (at_imei(line, v)) {
        taskENTER_CRITICAL();
        memcpy(s_info.imei, v, sizeof v);
        taskEXIT_CRITICAL();
    }
    (void)ctx;
}

static void on_iccid(const char *line, void *ctx)
{
    char v[21];
    if (at_iccid(line, v)) {
        taskENTER_CRITICAL();
        memcpy(s_info.iccid, v, sizeof v);
        taskEXIT_CRITICAL();
    }
    (void)ctx;
}

static void on_first(const char *line, void *ctx)
{
    char *out = ctx;
    if (!out[0])
        snprintf(out, sizeof s_info.fw, "%s", line);
}

static void on_cpin(const char *line, void *ctx) { *(bool *)ctx |= at_cpin_ready(line); }

static void on_csq(const char *line, void *ctx)
{
    int r;
    if (at_csq(line, &r)) {
        s_info.csq = r;
        *(bool *)ctx = true;
    }
}

static void on_nwinfo(const char *line, void *ctx)
{
    char t[24];
    if (at_nwinfo(line, t, sizeof t)) {
        taskENTER_CRITICAL();
        memcpy(s_info.net_type, t, sizeof t);
        taskEXIT_CRITICAL();
    }
    (void)ctx;
}

static bool sim_ready(void)
{
    bool ready = false;
    at_cmd("AT+CPIN?", 5000, on_cpin, &ready);
    info_set(&s_info.sim, ready ? "READY" : "NO SIM");
    return ready;
}

static void identity(void)
{
    at_cmd("ATE0", 1000, NULL, NULL);
    at_cmd("AT+CGSN", 2000, on_imei, NULL);
    char fw[sizeof s_info.fw] = "";
    at_cmd("AT+QGMR", 2000, on_first, fw);
    taskENTER_CRITICAL();
    memcpy(s_info.fw, fw, sizeof fw);
    taskEXIT_CRITICAL();
    SLOG_I("lte", "IMEI %s, firmware %s, AT %lu ms after the release", s_info.imei[0] ? s_info.imei : "-", fw[0] ? fw : "-",
           (unsigned long)s_info.ready_ms);
}

/** Waits up to ms for one of the CMUX events in mask; the events seen. */
static uint32_t wait_events(uint32_t mask, uint32_t wait_ms)
{
    uint32_t t0 = ms();
    for (;;) {
        taskENTER_CRITICAL();
        uint32_t e = s_events & mask;
        s_events &= ~mask;
        taskEXIT_CRITICAL();
        if (e || ms() - t0 >= wait_ms)
            return e;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
}

/** CMUX up: the command, then DLC0, DLC1, DLC2 and their MSCs. False: the caller recovers. */
static bool open_mux(void)
{
    if (at_cmd(CMUX_CMD, 3000, NULL, NULL) != AT_OK) {
        SLOG_W("lte", "AT+CMUX refused");
        return false;  // Plan 10 T4: the plain PPP fallback
    }
    taskENTER_CRITICAL();
    s_events = 0;
    taskEXIT_CRITICAL();
    info_set(&s_info.mux, "OPENING");
    vTaskDelay(pdMS_TO_TICKS(100));  // the modem switches to the multiplexer after its OK
    request(REQ_MUX_START);
    if (wait_events(EV_RUNNING | EV_FAILED, 3000) & EV_RUNNING) {
        info_set(&s_info.mux, "RUNNING");
        SLOG_I("lte", "CMUX running");
        return true;
    }
    info_set(&s_info.mux, "FAILED");
    SLOG_E("lte", "CMUX did not open");
    return false;
}

/** RECOVER: CLD, 1 s, RESET_N, back to AT. */
static void recover(void)
{
    SLOG_W("lte", "recovering: CLD, RESET_N");
    request(REQ_MUX_CLOSE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    hw_pulse(NULL, LTE_PIN_RESET, LTE_RESET_MS);
    info_set(&s_info.mux, "AT");
    info_set(&s_info.modem, "UNKNOWN");
    info_set(&s_info.sim, "");
}

static void lte_task(void *arg)
{
    (void)arg;
    lte_power_t power = LTE_UNKNOWN;
    for (;;) {
        power = lte_power_up(&k_hw, power);
        info_set(&s_info.modem, power == LTE_ON ? "ON" : "UNKNOWN");
        if (power != LTE_ON) {
            vTaskDelay(pdMS_TO_TICKS(LTE_RETRY_MS));
            continue;
        }
        identity();
        char cmd[64];
        snprintf(cmd, sizeof cmd, "AT+CGDCONT=1,\"IP\",\"%s\"", s_cfg.apn);
        at_cmd(cmd, 2000, NULL, NULL);
        if (!open_mux()) {
            recover();
            power = LTE_UNKNOWN;
            continue;
        }
        // DLC2 from here: the signal every 20 s, the SIM, and the supervision
        bool no_sim = !sim_ready();
        if (no_sim)
            SLOG_W("lte", "NO SIM");
        at_cmd("AT+QCCID", 2000, on_iccid, NULL);
        uint32_t last_addr = ms(), next_cpin = ms() + LTE_RETRY_MS;
        for (;;) {
            bool alive = false;
            at_cmd("AT+CSQ", 2000, on_csq, &alive);
            at_cmd("AT+QNWINFO", 2000, on_nwinfo, NULL);
            if (no_sim && (int32_t)(ms() - next_cpin) >= 0) {  // NO SIM: asked again every 60 s, nothing reset
                next_cpin = ms() + LTE_RETRY_MS;
                if (sim_ready()) {
                    no_sim = false;
                    at_cmd("AT+QCCID", 2000, on_iccid, NULL);
                    SLOG_I("lte", "SIM ready");
                }
            }
            if (no_sim)
                last_addr = ms();  // nothing dialled, nothing missed (PPP: Plan 10 T4)
            lte_action_t act = lte_supervise(ms(), last_addr, s_last_rx_ms, alive, no_sim);
            if (wait_events(EV_CLOSED | EV_FAILED, 0) || act == LTE_ACT_RECOVER) {
                recover();
                power = LTE_UNKNOWN;
                break;
            }
            if (act == LTE_ACT_CFUN) {
                SLOG_W("lte", "no PPP address for 2 minutes: AT+CFUN=1,1");
                at_cmd("AT+CFUN=1,1", 5000, NULL, NULL);
                last_addr = ms();
            }
            if (wait_events(EV_CLOSED | EV_FAILED, POLL_MS)) {
                recover();
                power = LTE_UNKNOWN;
                break;
            }
        }
    }
}

// ---- start, info ------------------------------------------------------------------------------------

void lte_start(const net_cfg_t *cfg)
{
    s_cfg = *cfg;
    s_at_out = xStreamBufferCreateStatic(AT_OUT_SIZE, 1, s_at_out_mem, &s_at_out_sb);
    s_at_in = xStreamBufferCreateStatic(AT_IN_SIZE, 1, s_at_in_mem, &s_at_in_sb);
    s_ppp_out = xStreamBufferCreateStatic(PPP_OUT_SIZE, 1, s_ppp_out_mem, &s_ppp_out_sb);
    if (lte_uart_init()) {
        SLOG_E("lte", "UART3 did not start");
        return;
    }
    s_cmux_task = xTaskCreateStatic(cmux_task, "cmux_rx", CMUX_STACK_WORDS, NULL, CMUX_PRIO, s_cmux_stack, &s_cmux_tcb);
    lte_uart_reader(s_cmux_task);
    if (cfg->priority == 0) {  // Ethernet only: UART3 listens, the modem is left alone (Plan 10 T5 powers it down)
        SLOG_I("lte", "network priority 0: Ethernet only");
        return;
    }
    s_lte_task = xTaskCreateStatic(lte_task, "lte", LTE_STACK_WORDS, NULL, LTE_PRIO, s_lte_stack, &s_lte_tcb);
}

void lte_send_raw(const char *text)
{
    if (!s_at_out)
        return;
    xStreamBufferSend(s_at_out, text, strlen(text), pdMS_TO_TICKS(100));
    xStreamBufferSend(s_at_out, "\r", 1, pdMS_TO_TICKS(100));
    xTaskNotifyGive(s_cmux_task);
}

void lte_info(lte_info_t *out)
{
    taskENTER_CRITICAL();
    *out = s_info;
    taskEXIT_CRITICAL();
    const cmux_counters_t *c = cmux_counters(&s_mux);
    out->overrun = s_overrun;
    out->bad_fcs = c->bad_fcs;
    out->ppp_in_drop = s_in_drop;
    out->ppp_out_drop = s_out_drop;
    out->uart_errors = lte_uart_errors();
}
