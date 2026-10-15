// The WebSocket console (spec §6.4, §10.1): the `console` task with its own Mongoose manager on
// system.websocketPort. The browser cannot send headers on a WebSocket, so the login travels as
// ws://host:port/ws?auth=<base64 user:password>, checked before the upgrade.
// LOG mode (websocketTarget 3): the syslog task is the one writer of a MessageBuffer (its tap); this
// task drains it after each poll into text frames. A client whose send buffer is over WEB_SEND_HIGH
// gets no frame (dropped and counted), so one that stops reading costs no memory (Codex 06b r1).
// UART modes (1, 2): the serial driver comes with Plan 07.
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "blog.h"
#include "message_buffer.h"
#include "heap_newlib.h"
#include "netmgr.h"
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

static StaticTask_t s_tcb;
static StackType_t s_stack[CONSOLE_STACK_WORDS];
static StaticMessageBuffer_t s_mb_mem;
static uint8_t s_mb_store[LOG_BUF];
static MessageBufferHandle_t s_mb;
static struct mg_mgr s_mgr;
static int s_port, s_target;
static unsigned s_dropped;  // lines not delivered: the buffer was full, or a client was not reading

void console_buffer_use(unsigned *used, unsigned *size)
{
    *size = LOG_BUF;
    *used = s_mb ? (unsigned)(LOG_BUF - xMessageBufferSpacesAvailable(s_mb)) : 0;
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
    sysmgr_time_str(t);
    snprintf(out, cap, "firmware %s, up %lus, time %s, eth %lu.%lu.%lu.%lu (link %s), free heap %u\n", APP_VERSION,
             (unsigned long)sysmgr_uptime_s(), t[0] ? t : "not set", (unsigned long)(ns.ip >> 24), (unsigned long)(ns.ip >> 16 & 255),
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

static void handler(struct mg_connection *c, int ev, void *ev_data)
{
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
        const char *hello = s_target == 3 ? "connected to the gateway log; type help\n"
                                          : "connected; the UART console starts with the serial driver (a later firmware)\n";
        mg_ws_send(c, hello, strlen(hello), WEBSOCKET_OP_TEXT);
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = ev_data;
        if (s_target == 3 && (wm->flags & 15) == WEBSOCKET_OP_TEXT)
            command(c, wm->data.buf, wm->data.len);
    }
}

// After each poll: the log lines to every client that keeps reading.
static void drain(void)
{
    static char line[200];
    size_t n;
    while ((n = xMessageBufferReceive(s_mb, line, sizeof line, 0)) > 0) {
        for (struct mg_connection *x = s_mgr.conns; x; x = x->next) {
            if (!x->is_websocket)
                continue;
            if (x->send.len >= WEB_SEND_HIGH) {
                s_dropped++;
                continue;
            }
            mg_ws_printf(x, WEBSOCKET_OP_TEXT, "%.*s\n", (int)n, line);
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
        if (s_target == 3)
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
    xTaskCreateStatic(console_task, "console", CONSOLE_STACK_WORDS, NULL, CONSOLE_PRIO, s_stack, &s_tcb);
    return 0;
}
