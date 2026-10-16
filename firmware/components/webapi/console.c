// The WebSocket console (spec §6.4, §10.1): the `console` task with its own Mongoose manager on
// system.websocketPort. The browser cannot send headers on a WebSocket, so the login travels as
// ws://host:port/ws?auth=<base64 user:password>, checked before the upgrade.
// LOG mode (websocketTarget 3): the syslog task is the one writer of a MessageBuffer (its tap); this
// task drains it after each poll into text frames. A client whose send buffer is over WEB_SEND_HIGH
// gets no frame (dropped and counted), so one that stops reading costs no memory (Codex 06b r1).
// UART modes (1, 2, Plan 07a): serial_rx[n] is the one writer of the same MessageBuffer, through a
// subscriber; each UART frame becomes one binary WebSocket message. Messages from a client go to the port.
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "blog.h"
#include "message_buffer.h"
#include "heap_newlib.h"
#include "netmgr.h"
#include "serial.h"
#include "now_us.h"
#include "syslog.h"
#include "sysmgr.h"
#include "task.h"
#include "web_core.h"
#include "web_priv.h"

#define CONSOLE_STACK_WORDS 1024  // 4 KB (spec §6.1)
#define CONSOLE_PRIO 5
#define MAX_CLIENTS 4
#define LOG_BUF 2048
#define POLL_MS 20  // spec §6.4
#define MAX_CONNS (MAX_CLIENTS + 1)  // sockets of this listener at once: the clients and one being refused
#define HANDSHAKE_MS 5000u           // a connection not upgraded by then is closed (Codex 06b review)

static StaticTask_t s_tcb;
static StackType_t s_stack[CONSOLE_STACK_WORDS];
static StaticMessageBuffer_t s_mb_mem;
static uint8_t s_mb_store[LOG_BUF];  // 2 KB: one 1460-byte frame and some (each message has a 4-byte length)
static MessageBufferHandle_t s_mb;
static struct mg_mgr s_mgr;
static int s_port, s_target;
static unsigned s_dropped;  // lines not delivered: the buffer was full, or a client was not reading

void console_buffer_use(unsigned *used, unsigned *size)
{
    *size = LOG_BUF;
    *used = s_mb ? (unsigned)(LOG_BUF - xMessageBufferSpacesAvailable(s_mb)) : 0;
}

// serial_rx[n]: the one writer in UART mode (spec §6.4). A frame that does not fit is dropped and counted.
static void uart_frame(int port, const uint8_t *data, size_t len, void *ctx)
{
    (void)port;
    (void)ctx;
    if (xMessageBufferSend(s_mb, data, len, 0) != len)
        s_dropped++;
}

// The syslog task: the one writer (spec §6.4).
static void tap(const char *line, size_t len, void *ctx)
{
    (void)ctx;
    if (len > 200)
        len = 200;
    if (xMessageBufferSend(s_mb, line, len, 0) != len)
        s_dropped++;
}

void console_status_line(char *out, size_t cap)
{
    net_status_t ns;
    netmgr_status(&ns);
    char t[20];
    sysmgr_display_time(t);
    snprintf(out, cap, "firmware %s, up %lus, time %s%s, eth %lu.%lu.%lu.%lu (link %s), free heap %u\n", APP_VERSION,
             (unsigned long)sysmgr_uptime_s(), t, sysmgr_clock_set() ? "" : " (not set)", (unsigned long)(ns.ip >> 24), (unsigned long)(ns.ip >> 16 & 255),
             (unsigned long)(ns.ip >> 8 & 255), (unsigned long)(ns.ip & 255), ns.eth.up ? "up" : "down", (unsigned)heap_free_now());
}

static void command(struct mg_connection *c, const char *cmd, size_t len)
{
    while (len && (*cmd == ' ' || cmd[len - 1] == '\n' || cmd[len - 1] == '\r' || cmd[len - 1] == ' ')) {
        if (*cmd == ' ')
            cmd++, len--;
        else
            len--;
    }
    char out[200];
    if (len == 4 && !memcmp(cmd, "help", 4))
        snprintf(out, sizeof out, "commands: help, status, restart\n");
    else if (len == 6 && !memcmp(cmd, "status", 6))
        console_status_line(out, sizeof out);
    else if (len == 7 && !memcmp(cmd, "restart", 7)) {
        snprintf(out, sizeof out, "restarting\n");
        sysmgr_restart_later(800);
    } else if (len == 0)
        return;
    else
        snprintf(out, sizeof out, "unknown command; type help\n");
    mg_ws_send(c, out, strlen(out), WEBSOCKET_OP_TEXT);
}

static int clients(void)
{
    int n = 0;
    for (struct mg_connection *x = s_mgr.conns; x; x = x->next)
        n += x->is_websocket;
    return n;
}

static int conns(void)
{
    int n = 0;
    for (struct mg_connection *x = s_mgr.conns; x; x = x->next)
        n += x->is_accepted;
    return n;
}

static uint64_t ms_now(void)
{
    return now_us() / 1000u;
}

static void handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_ACCEPT) {
        if (conns() > MAX_CONNS)
            c->is_closing = 1;  // a pending socket counts too: they share lwIP's pool
        uint64_t t = ms_now();
        memcpy(c->data, &t, sizeof t);
        return;
    }
    if (ev == MG_EV_POLL && c->is_accepted) {
        uint64_t t;
        memcpy(&t, c->data, sizeof t);
        if (!c->is_websocket && ms_now() - t >= HANDSHAKE_MS)
            c->is_closing = 1;
        c->is_full = c->send.len >= WEB_SEND_HIGH;  // a client that does not read: its commands wait
        return;
    }
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = ev_data;
        if (mg_strcmp(hm->uri, mg_str("/ws")) != 0) {
            mg_http_reply(c, 404, "", "Not found\n");
            return;
        }
        char raw[96], tok[72];
        int n = mg_http_get_var(&hm->query, "auth", raw, sizeof raw);  // Mongoose decodes %XX already
        size_t tl = n > 0 ? strlen(raw) : 0;
        if (tl >= sizeof tok)
            tl = 0;
        memcpy(tok, raw, tl);
        tok[tl] = '\0';
        if (!web_token_eq(g_web.token, tok, tl)) {
            mg_http_reply(c, 401, "", "Sign in required\n");  // before the handshake (spec §10.1)
            c->is_draining = 1;
            return;
        }
        if (clients() >= MAX_CLIENTS) {
            mg_http_reply(c, 503, "", "Too many console clients\n");
            c->is_draining = 1;
            return;
        }
        mg_ws_upgrade(c, hm, NULL);
    } else if (ev == MG_EV_WS_OPEN) {
        char hello[48];
        if (s_target == 3)
            snprintf(hello, sizeof hello, "connected to the gateway log; type help\n");
        else
            snprintf(hello, sizeof hello, "connected to UART%d\n", s_target);
        mg_ws_send(c, hello, strlen(hello), WEBSOCKET_OP_TEXT);
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = ev_data;
        if (c->send.len >= WEB_SEND_HIGH)
            return;  // not reading its answers: no more of them (Codex 06b review)
        if (s_target == 3 && (wm->flags & 15) == WEBSOCKET_OP_TEXT)
            command(c, wm->data.buf, wm->data.len);
        else if (s_target != 3 && wm->data.len)
            serial_write(s_target, (const uint8_t *)wm->data.buf, wm->data.len, 2000);  // text or binary, as typed
    }
}

// After each poll: the log lines (text) or UART frames (binary) to every client that keeps reading.
static void drain(void)
{
    static char line[SERIAL_FRAME_MAX] __attribute__((section(".axi_bss")));
    size_t n;
    while ((n = xMessageBufferReceive(s_mb, line, sizeof line, 0)) > 0) {
        for (struct mg_connection *x = s_mgr.conns; x; x = x->next) {
            if (!x->is_websocket)
                continue;
            if (x->send.len >= WEB_SEND_HIGH) {
                s_dropped++;
                continue;
            }
            if (s_target == 3)
                mg_ws_printf(x, WEBSOCKET_OP_TEXT, "%.*s\n", (int)n, line);
            else
                mg_ws_send(x, line, n, WEBSOCKET_OP_BINARY);
        }
    }
}

static void console_task(void *arg)
{
    (void)arg;
    char url[32];
    snprintf(url, sizeof url, "http://0.0.0.0:%d", s_port);
    mg_mgr_init(&s_mgr);
    if (!mg_http_listen(&s_mgr, url, handler, NULL))
        SLOG_E("console", "cannot listen on port %d", s_port);
    else
        blog("console: WebSocket on port %d (%s)\r\n", s_port, s_target == 3 ? "log" : "UART");
    for (;;) {
        mg_mgr_poll(&s_mgr, POLL_MS);
        drain();
    }
}

int console_start(int port, int target)
{
    s_port = port;
    s_target = target;
    s_mb = xMessageBufferCreateStatic(sizeof s_mb_store, s_mb_store, &s_mb_mem);
    if (target == 3)
        syslog_set_tap(tap, NULL);
    else if (serial_subscribe(target, uart_frame, NULL))
        SLOG_W("console", "UART%d is not running: the console shows nothing", target);
    xTaskCreateStatic(console_task, "console", CONSOLE_STACK_WORDS, NULL, CONSOLE_PRIO, s_stack, &s_tcb);
    return 0;
}
