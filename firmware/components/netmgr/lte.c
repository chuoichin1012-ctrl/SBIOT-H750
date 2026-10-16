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
#include "lwip/tcpip.h"
#include "netif/ppp/pppapi.h"
#include "netmgr.h"
#include "main.h"  // LTE_PWR, LTE_RESET (through Q1/Q2: high is pressed)
#include "now_us.h"
#include "semphr.h"
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
enum { REQ_MUX_START = 1, REQ_MUX_CLOSE = 2, REQ_PLAIN_PPP = 4, REQ_DISC2 = 8 };
enum { EV_RUNNING = 1 << CMUX_EV_RUNNING, EV_CLOSED = 1 << CMUX_EV_DLC_CLOSED, EV_FAILED = 1 << CMUX_EV_FAILED, EV_PPP_DOWN = 1 << 8 };

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
static volatile uint32_t s_overrun, s_in_drop, s_out_drop, s_at_drop;
// PPP (Plan 10 T4)
static ppp_pcb *s_ppp;
static struct netif s_ppp_netif;
static volatile bool s_ppp_running;  // the data channel (DLC1, or the plain UART) carries PPP
static volatile bool s_ppp_up;       // IPCP is up: the cell link has an address
static at_lines_t s_dial_lines;      // DLC1's lines before PPP, in cmux_rx
static volatile int s_dial_result;   // at_final_t of the dial
static lte_info_t s_info = {.csq = 99, .state = "disconnected", .modem = "UNKNOWN", .mux = "AT", .sim = ""};
static uint32_t s_release_ms;  // the last PWRKEY or RESET_N release, for ready_ms
// the shutdown before a gateway restart, and priority 0 (Plan 10 T5)
static volatile lte_power_t s_power = LTE_UNKNOWN;
static volatile bool s_shutdown_req, s_powered_down;
static volatile bool s_bench_drop;  // bench: PPP's input dropped, so PPP never gets an address (the CFUN case)
static SemaphoreHandle_t s_shutdown_done;
static StaticSemaphore_t s_shutdown_mem;
static at_lines_t s_lines;  // the AT channel's lines (the lte task)

static uint32_t ms(void) { return (uint32_t)(now_us() / 1000u); }

// ---- cmux_rx ----------------------------------------------------------------------------------------

// The data channel's bytes: PPP once it runs, the dial's reply before (CONNECT, NO CARRIER, ERROR).
static void ppp_input(const uint8_t *p, size_t n)
{
    if (s_bench_drop)
        return;
    if (s_ppp_running && s_ppp) {
        for (int i = 0; i < 3; i++) {  // ERR_MEM: 3 tries, 1 ms apart, then dropped (spec §7.2)
            if (pppos_input_tcpip(s_ppp, p, (int)n) != ERR_MEM)
                return;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        s_in_drop++;
        return;
    }
    for (size_t i = 0; i < n; i++) {
        const char *line = at_feed(&s_dial_lines, p[i]);
        at_final_t f = line ? at_final(line) : AT_NONE;
        if (f != AT_NONE) {
            s_dial_result = f;
            xTaskNotifyGive(s_lte_task);
        }
    }
}

static void at_input(const uint8_t *p, size_t n)
{
    if (xStreamBufferSend(s_at_in, p, n, 0) != n)
        s_at_drop++;  // the lte task does not read: lines are lost, its command times out
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
        if (req & REQ_MUX_CLOSE) {
            if (s_mode == M_MUX) {
                cmux_close(&s_mux);
                size_t n;
                while ((n = cmux_tx_next(&s_mux, lte_uart_txbuf(), LTE_UART_TXBUF)))
                    lte_uart_write(lte_uart_txbuf(), n, 100);
            }
            s_mode = M_AT;
        }
        if (req & REQ_PLAIN_PPP)
            s_mode = M_PLAIN_PPP;
        if (req & REQ_DISC2 && s_mode == M_MUX)
            cmux_disc(&s_mux, 2);  // the bench's recovery case
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
    at_lines_t *lines = &s_lines;
    uint8_t b[64];
    while (xStreamBufferReceive(s_at_in, b, sizeof b, 0)) {
    }  // whatever is left is late
    lines->n = 0;
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
            const char *line = at_feed(lines, b[i]);
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

/** Waits up to timeout_ms for a line whose final result is want (an URC such as POWERED DOWN). */
static bool at_wait(at_final_t want, uint32_t timeout_ms)
{
    uint8_t b[64];
    uint32_t t0 = ms();
    for (;;) {
        uint32_t spent = ms() - t0;
        if (spent >= timeout_ms)
            return false;
        size_t n = xStreamBufferReceive(s_at_in, b, sizeof b, pdMS_TO_TICKS(timeout_ms - spent));
        for (size_t i = 0; i < n; i++) {
            const char *line = at_feed(&s_lines, b[i]);
            if (line && at_final(line) == want)
                return true;
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
    for (int i = 0; i < 10 && !ready; i++) {  // just after a reset the SIM takes a few seconds
        if (i)
            vTaskDelay(pdMS_TO_TICKS(1000));
        at_cmd("AT+CPIN?", 5000, on_cpin, &ready);
    }
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

static void ppp_stop(void);

/** AT+QPOWD=1 on the plain UART (CLD first), then POWERED DOWN within 60 s: only that is OFF (spec §7.2). */
static bool power_down(void)
{
    ppp_stop();
    if (s_mode == M_MUX) {
        request(REQ_MUX_CLOSE);  // CLD: the modem goes back to AT on its UART
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    SLOG_I("lte", "AT+QPOWD=1");
    at_final_t f = at_cmd("AT+QPOWD=1", 3000, NULL, NULL);
    bool down = f == AT_POWERED_DOWN || (f == AT_OK && at_wait(AT_POWERED_DOWN, 60000));
    s_power = down ? LTE_OFF : LTE_UNKNOWN;
    info_set(&s_info.modem, down ? "OFF" : "UNKNOWN");
    info_set(&s_info.mux, "AT");
    info_set(&s_info.state, "disconnected");
    if (down)
        SLOG_I("lte", "POWERED DOWN");
    else
        SLOG_W("lte", "no POWERED DOWN within 60 s: the module stays UNKNOWN (PWRKEY not pressed)");
    return down;
}

/** A shutdown asked by lte_shutdown: done here, in the lte task, which then stops for the restart. */
static void check_shutdown(void)
{
    if (!s_shutdown_req)
        return;
    s_powered_down = s_power == LTE_ON ? power_down() : true;
    xSemaphoreGive(s_shutdown_done);
    for (;;)
        vTaskDelay(portMAX_DELAY);
}

/** Waits up to ms for one of the CMUX events in mask; the events seen. */
static uint32_t wait_events(uint32_t mask, uint32_t wait_ms)
{
    uint32_t t0 = ms();
    for (;;) {
        check_shutdown();
        taskENTER_CRITICAL();
        uint32_t e = s_events & mask;
        s_events &= ~mask;
        taskEXIT_CRITICAL();
        if (e || ms() - t0 >= wait_ms)
            return e;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
}

// ---- PPP -------------------------------------------------------------------------------------------

// PPPoS output, in the tcpip thread: all of it into the data channel's stream, or 0 when there is no room
// (spec §7.2: a frame cut across calls is dropped by the modem's FCS; TCP sends again).
static u32_t ppp_out(ppp_pcb *pcb, const void *data, u32_t len, void *ctx)
{
    (void)pcb;
    (void)ctx;
    if (xStreamBufferSpacesAvailable(s_ppp_out) < len) {
        s_out_drop++;
        return 0;
    }
    xStreamBufferSend(s_ppp_out, data, len, 0);
    xTaskNotifyGive(s_cmux_task);
    return len;
}

// PPP's status, in the tcpip thread. The cell DNS comes from IPCP's result, never from lwIP's servers (ruling 5).
static void ppp_status(ppp_pcb *pcb, int err, void *ctx)
{
    (void)ctx;
    uint32_t dns[2] = {0, 0};
    if (err == PPPERR_NONE) {
        dns[0] = lwip_ntohl(pcb->ipcp_gotoptions.dnsaddr[0]);
        dns[1] = lwip_ntohl(pcb->ipcp_gotoptions.dnsaddr[1]);
        s_ppp_up = true;
        s_info.ip = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(&s_ppp_netif)));
        s_info.dns1 = s_cfg.cell_dns_manual ? s_cfg.cell_dns1 : dns[0];
        s_info.dns2 = s_cfg.cell_dns_manual ? s_cfg.cell_dns2 : dns[1];
        s_info.state = "connected";
        SLOG_I("lte", "PPP up: %lu.%lu.%lu.%lu", (unsigned long)(s_info.ip >> 24), (unsigned long)(s_info.ip >> 16 & 255),
               (unsigned long)(s_info.ip >> 8 & 255), (unsigned long)(s_info.ip & 255));
        netmgr_cell(&s_ppp_netif, true, dns);
        return;
    }
    bool was_up = s_ppp_up;
    s_ppp_up = false;
    s_ppp_running = false;
    s_info.ip = s_info.dns1 = s_info.dns2 = 0;
    s_info.state = "disconnected";
    if (was_up || err != PPPERR_USER)
        SLOG_W("lte", "PPP down (%d)", err);
    netmgr_cell(&s_ppp_netif, false, dns);
    s_events |= EV_PPP_DOWN;
    if (s_lte_task)
        xTaskNotifyGive(s_lte_task);
}

static void ppp_begin(void)
{
    if (!s_ppp) {
        s_ppp = pppapi_pppos_create(&s_ppp_netif, ppp_out, ppp_status, NULL);
        if (!s_ppp) {
            SLOG_E("lte", "no PPP control block");
            return;
        }
        static const u8_t auth[] = {PPPAUTHTYPE_NONE, PPPAUTHTYPE_PAP, PPPAUTHTYPE_CHAP, PPPAUTHTYPE_PAP | PPPAUTHTYPE_CHAP};
        LOCK_TCPIP_CORE();
        if (s_cfg.apn_auth >= 1 && s_cfg.apn_auth <= 3)
            ppp_set_auth(s_ppp, auth[s_cfg.apn_auth], s_cfg.apn_user, s_cfg.apn_pass);
        ppp_set_usepeerdns(s_ppp, 1);  // IPCP asks for the servers; netmgr chooses which pair lwIP uses
        UNLOCK_TCPIP_CORE();
    }
    s_ppp_running = true;
    pppapi_connect(s_ppp, 0);
}

/** PPP stopped at once (a recovery, a CFUN, a shutdown): its link callback reports it down. */
static void ppp_stop(void)
{
    if (s_ppp && (s_ppp_running || s_ppp_up))
        pppapi_close(s_ppp, 1);
    s_ppp_running = false;
}

static void on_creg(const char *line, void *ctx)
{
    (void)ctx;
    if (!strncmp(line, "+CEREG:", 7) || !strncmp(line, "+CGREG:", 7) || !strncmp(line, "+COPS:", 6))
        SLOG_W("lte", "registration %s", line);  // n,1 home, n,5 roaming, n,2 searching, n,3 denied; COPS: the operator
}

static void on_reg(const char *line, void *ctx)
{
    int st;
    if (at_creg(line, &st) && (st == 1 || st == 5))
        *(bool *)ctx = true;
}

/** On the network (LTE or 2G/3G packet domain): home or roaming. Otherwise logged, and nothing is dialled. */
static bool registered(void)
{
    bool reg = false;
    at_cmd("AT+CEREG?", 2000, on_reg, &reg);
    if (!reg)
        at_cmd("AT+CGREG?", 2000, on_reg, &reg);
    if (!reg) {
        at_cmd("AT+CEREG?", 2000, on_creg, NULL);
        at_cmd("AT+COPS?", 2000, on_creg, NULL);
    }
    return reg;
}

/** ATD*99# on DLC1, then PPP. False: no CONNECT (logged with the registration). */
static bool dial_mux(void)
{
    info_set(&s_info.state, "connecting");
    s_dial_lines.n = 0;
    s_dial_result = AT_NONE;
    xStreamBufferSend(s_ppp_out, "ATD*99#\r", 8, pdMS_TO_TICKS(100));
    xTaskNotifyGive(s_cmux_task);
    uint32_t t0 = ms();
    while (s_dial_result == AT_NONE && ms() - t0 < 30000) {
        check_shutdown();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
    }
    if (s_dial_result == AT_CONNECT) {
        SLOG_I("lte", "CONNECT on DLC1: PPP starts");
        ppp_begin();
        return true;
    }
    info_set(&s_info.state, "disconnected");
    SLOG_W("lte", "dial: %s", s_dial_result == AT_NO_CARRIER ? "NO CARRIER" : s_dial_result == AT_ERROR ? "ERROR" : "no reply");
    at_cmd("AT+CEREG?", 2000, on_creg, NULL);
    at_cmd("AT+CGREG?", 2000, on_creg, NULL);
    at_cmd("AT+COPS?", 2000, on_creg, NULL);
    return false;
}

/** AT+CMUX refused: PPP on the plain UART (spec §7.2's fallback); the signal is read before only. */
static void plain_ppp(void)
{
    info_set(&s_info.mux, "PLAIN");
    if (!sim_ready()) {
        SLOG_W("lte", "NO SIM");
        return;
    }
    at_cmd("AT+QCCID", 2000, on_iccid, NULL);
    bool alive = false;
    at_cmd("AT+CSQ", 2000, on_csq, &alive);
    at_cmd("AT+QNWINFO", 2000, on_nwinfo, NULL);
    info_set(&s_info.state, "connecting");
    if (at_cmd("ATD*99#", 30000, NULL, NULL) != AT_CONNECT) {
        info_set(&s_info.state, "disconnected");
        SLOG_W("lte", "dial on the plain UART: no CONNECT");
        return;
    }
    request(REQ_PLAIN_PPP);
    ppp_begin();
    uint32_t last_addr = ms();
    for (;;) {  // until PPP goes down, or 2 minutes without an address
        uint32_t e = wait_events(EV_PPP_DOWN, POLL_MS);
        if (s_ppp_up)
            last_addr = ms();
        if (e || ms() - last_addr >= LTE_NO_ADDR_MS)
            return;  // the caller recovers with RESET_N (leaving data mode needs +++ and ATH; a reset is simpler)
    }
}

/** CMUX up: the command, then DLC0, DLC1, DLC2 and their MSCs. 1 running, 0 refused (the plain fallback), -1 failed. */
static int open_mux(void)
{
    at_final_t f = at_cmd(CMUX_CMD, 3000, NULL, NULL);
    if (f != AT_OK) {
        SLOG_W("lte", "AT+CMUX refused");
        return f == AT_ERROR ? 0 : -1;
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
        return 1;
    }
    info_set(&s_info.mux, "FAILED");
    SLOG_E("lte", "CMUX did not open");
    return -1;
}

/** RECOVER: CLD, 1 s, RESET_N, back to AT. */
static void recover(void)
{
    SLOG_W("lte", "recovering: CLD, RESET_N");
    ppp_stop();
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
    if (s_cfg.priority == 0) {  // Ethernet only: a module that answers is powered down; one that does not is left alone
        if (hw_at_ok(NULL, LTE_PROBE_MS)) {
            s_power = LTE_ON;
            power_down();
        }
        for (;;)
            wait_events(0, LTE_RETRY_MS);  // nothing more; lte_shutdown has nothing to do
    }
    for (;;) {
        check_shutdown();
        s_power = lte_power_up(&k_hw, s_power);
        info_set(&s_info.modem, s_power == LTE_ON ? "ON" : "UNKNOWN");
        if (s_power != LTE_ON) {
            wait_events(0, LTE_RETRY_MS);  // MODEM NOT RESPONDING: 60 s, then again from UNKNOWN
            continue;
        }
        identity();
        char cmd[64];
        snprintf(cmd, sizeof cmd, "AT+CGDCONT=1,\"IP\",\"%s\"", s_cfg.apn);
        at_cmd(cmd, 2000, NULL, NULL);
        int mux = open_mux();
        if (mux <= 0) {
            if (mux == 0)
                plain_ppp();
            recover();
            s_power = LTE_UNKNOWN;
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
            if (no_sim || s_ppp_up)
                last_addr = ms();  // NO SIM: nothing dialled, nothing missed
            else if (!s_ppp_running && registered())
                dial_mux();  // again at each poll until PPP is up, once the module is on the network
            else {  // PPP negotiating without an address yet: the registration, for the log
                at_cmd("AT+CEREG?", 2000, on_creg, NULL);
                at_cmd("AT+COPS?", 2000, on_creg, NULL);
            }
            taskENTER_CRITICAL();
            s_events &= ~EV_PPP_DOWN;  // PPP down: the next poll dials again
            taskEXIT_CRITICAL();
            lte_action_t act = lte_supervise(ms(), last_addr, s_last_rx_ms, alive, no_sim);
            if (wait_events(EV_CLOSED | EV_FAILED, 0) || act == LTE_ACT_RECOVER) {
                recover();
                s_power = LTE_UNKNOWN;
                break;
            }
            if (act == LTE_ACT_CFUN) {
                SLOG_W("lte", "no PPP address for 2 minutes: AT+CFUN=1,1");
                ppp_stop();
                at_cmd("AT+CFUN=1,1", 5000, NULL, NULL);
                vTaskDelay(pdMS_TO_TICKS(5000));
                if (at_cmd("AT", 2000, NULL, NULL) != AT_OK) {  // the module restarted, CMUX with it: RECOVER now (spec §7.2)
                    recover();
                    s_power = LTE_UNKNOWN;
                    break;
                }
                last_addr = ms();
            }
            if (wait_events(EV_CLOSED | EV_FAILED, POLL_MS)) {
                recover();
                s_power = LTE_UNKNOWN;
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
    s_shutdown_done = xSemaphoreCreateBinaryStatic(&s_shutdown_mem);
    if (cfg->priority == 0)
        SLOG_I("lte", "network priority 0: Ethernet only; a module that answers is powered down");
    s_lte_task = xTaskCreateStatic(lte_task, "lte", LTE_STACK_WORDS, NULL, LTE_PRIO, s_lte_stack, &s_lte_tcb);
}

bool lte_shutdown(uint32_t timeout_ms)
{
    if (!s_lte_task || s_power != LTE_ON)
        return true;  // OFF, or never seen ON: nothing to do (PWRKEY is never pressed here)
    s_shutdown_req = true;
    xTaskNotifyGive(s_lte_task);
    return xSemaphoreTake(s_shutdown_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE && s_powered_down;
}

void lte_bench_disc(void) { request(REQ_DISC2); }

bool lte_bench_drop(void)
{
    s_bench_drop = !s_bench_drop;
    return s_bench_drop;
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
    memcpy(out->last_bad, c->last_bad, sizeof out->last_bad);
    out->ppp_in_drop = s_in_drop;
    out->ppp_out_drop = s_out_drop;
    out->uart_errors = lte_uart_errors();
}
