// The `web` task (spec §6.1, §10.1): one Mongoose manager on system.webPort. The UI is served gzip
// with an ETag per file and no login. /api/* needs the Basic login; 401 carries no WWW-Authenticate.
// Body limits are decided at MG_EV_HTTP_HDRS (web_core). OTA, certificate and edgeDevices bodies are
// streamed: Mongoose's HTTP handler is silenced (c->pfn = NULL) and the data is taken at MG_EV_READ.
// Every streamed answer (UI files, the Edge GET, the log) adds a piece only while c->send holds
// less than WEB_SEND_HIGH, so a client that stops reading cannot grow the heap (Codex 06b r1).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "arena.h"
#include "mbedtls/platform_util.h"
#include "blog.h"
#include "flash_spi4.h"
#include "flashmap.h"
#include "now_us.h"
#include "ota_app.h"
#include "store.h"
#include "syslog.h"
#include "sysmgr.h"
#include "task.h"
#include "tls_core.h"
#include "web_core.h"
#include "web_priv.h"
#include "webapi.h"
#include "www_files.h"

#define WEB_STACK_WORDS 2048  // 8 KB, in AXI (spec §5.3, §6.1)
#define WEB_PRIO 5
#define POLL_MS 20

web_boot_t g_web;

// AXI (spec §5.3): the web stack. A certificate's body is on the heap for its upload only (Plan 09c).
static StackType_t s_web_stack[WEB_STACK_WORDS] __attribute__((section(".axi_bss")));
static StaticTask_t s_tcb;
static char s_record[STORE_EDGE_RECORD] __attribute__((section(".axi_bss")));  // one Edge record at a time, for any GET
static ota_stage_t s_ota;
static struct mg_mgr s_mgr;

typedef enum { ST_FREE, ST_IDLE, ST_FILE, ST_EDGE_GET, ST_LOG, ST_UPLOAD, ST_DISCARD } state_t;

#define DISCARD_MAX (64u * 1024u)  // a rejected body is read and dropped up to this, then the connection closes
#define QUIET_MS 300u
#define IDLE_MS 60000u  // a connection with nothing in progress and nothing received for this long is closed

typedef struct {
    state_t st;
    uint64_t last_ms;  // the last byte received
    bool waiting;      // headers seen, a body still to come (plain requests)
    bool until_quiet;  // ST_DISCARD of a body without a length: close once nothing comes for QUIET_MS
    bool hdrs_done;    // this request's headers were handled: Mongoose emits MG_EV_HTTP_HDRS again on every
                       // read and poll while the body is incomplete
    size_t declared;   // this request's Content-Length: a body that came short (the client closed) has no effect
    // ST_FILE
    const www_file_t *file;
    size_t off;
    // ST_EDGE_GET
    store_edge_pin_t pin;
    uint32_t rec;
    // ST_LOG
    int log_phase;
    uint32_t log_sent;
    // ST_UPLOAD
    web_ep_t ep;
    size_t expected, got;
    char scope[8], kind[8];
    bool edge_owner;  // this request's store_edge_begin succeeded: only it writes, commits or aborts (Plan 09c)
    char *cert;       // a certificate's body (heap, WEB_CERT_MAX + 1), for its upload only
} slot_t;

static slot_t s_slots[WEB_MAX_CONNS];
static bool s_body_busy, s_ota_busy;

static uint64_t ms_now(void)
{
    return now_us() / 1000u;
}

static slot_t *slot_of(struct mg_connection *c)
{
    slot_t *s;
    memcpy(&s, c->data, sizeof s);
    return s;
}

// ---- streamed answers -------------------------------------------------------------------------

static void finish_stream(slot_t *s)
{
    if (s->st == ST_EDGE_GET)
        store_edge_unpin(&s->pin);
    s->st = ST_IDLE;
}

typedef struct {
    struct mg_connection *c;
    slot_t *s;
    uint32_t index;  // lines seen in this pass
} log_ctx_t;

static bool log_flash_line(const store_log_meta_t *m, const char *line, size_t len, void *ctx)
{
    log_ctx_t *x = ctx;
    if (x->index++ < x->s->log_sent)
        return true;  // sent by an earlier pass
    if (x->c->send.len >= WEB_SEND_HIGH)
        return false;
    char ts[40];
    if (m->wall_ms > 0) {
        char t[20];
        sysmgr_format_wall(m->wall_ms, t);
        snprintf(ts, sizeof ts, "[%s] ", t);
    } else {
        uint32_t sec = m->uptime_ms / 1000u;
        snprintf(ts, sizeof ts, "[boot %lu +%02lu:%02lu:%02lu] ", (unsigned long)m->boot, (unsigned long)(sec / 3600u),
                 (unsigned long)(sec / 60u % 60u), (unsigned long)(sec % 60u));
    }
    mg_http_printf_chunk(x->c, "%s%.*s\n", ts, (int)len, line);
    x->s->log_sent++;
    return true;
}

static void log_ram_line(const char *line, size_t len, void *ctx)
{
    log_ctx_t *x = ctx;
    if (x->index++ < x->s->log_sent || x->c->send.len >= WEB_SEND_HIGH)
        return;
    mg_http_printf_chunk(x->c, "%.*s\n", (int)len, line);
    x->s->log_sent++;
}

static void pump(struct mg_connection *c, slot_t *s)
{
    while (c->send.len < WEB_SEND_HIGH && !c->is_closing) {
        if (s->st == ST_FILE) {
            size_t n = (size_t)(s->file->end - s->file->start) - s->off;
            if (n == 0) {
                finish_stream(s);
                c->is_resp = 0;  // the response is complete: Mongoose may parse the next request (keep-alive)
                return;
            }
            if (n > 2048)
                n = 2048;
            mg_send(c, s->file->start + s->off, n);
            s->off += n;
        } else if (s->st == ST_EDGE_GET) {
            if (s->rec == s->pin.count) {
                mg_http_printf_chunk(c, "");
                finish_stream(s);
                return;
            }
            size_t n = store_edge_record(&s->pin, s->rec, s_record, sizeof s_record);
            if (!n) {  // the status is out already: cut the answer, the client sees an incomplete body
                SLOG_W("web", "Edge record %lu unreadable during a GET", (unsigned long)s->rec);
                finish_stream(s);
                c->is_closing = 1;
                return;
            }
            mg_http_write_chunk(c, s_record, n);
            s->rec++;
        } else if (s->st == ST_LOG) {
            log_ctx_t x = {.c = c, .s = s};
            uint32_t before = s->log_sent;
            if (s->log_phase == 0)
                store_log_iter(log_flash_line, &x);
            else
                syslog_ram_iter(log_ram_line, &x);
            if (s->log_sent == before) {  // this phase has nothing more
                if (s->log_phase == 0) {
                    s->log_phase = 1;
                    s->log_sent = 0;
                    mg_http_printf_chunk(c, "\n# Since the last start\n");
                } else {
                    mg_http_printf_chunk(c, "");
                    finish_stream(s);
                    return;
                }
            }
        } else {
            return;
        }
    }
}

static void serve_file(struct mg_connection *c, slot_t *s, struct mg_http_message *hm)
{
    char path[64];
    size_t n = hm->uri.len < sizeof path - 1 ? hm->uri.len : sizeof path - 1;
    memcpy(path, hm->uri.buf, n);
    path[n] = '\0';
    if (!strcmp(path, "/"))
        strcpy(path, "/index.html");
    for (size_t i = 0; i < www_file_count; i++) {
        const www_file_t *f = &www_files[i];
        if (strcmp(f->path, path))
            continue;
        struct mg_str *inm = mg_http_get_header(hm, "If-None-Match");
        if (inm && inm->len == strlen(f->etag) && !memcmp(inm->buf, f->etag, inm->len)) {
            mg_printf(c, "HTTP/1.1 304 Not Modified\r\nETag: %s\r\nCache-Control: no-cache\r\nContent-Length: 0\r\n\r\n", f->etag);
            c->is_resp = 0;  // complete (mg_printf does not end a response the way mg_http_reply does)
            return;
        }
        mg_printf(c,
                  "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Encoding: gzip\r\nContent-Length: %u\r\n"
                  "ETag: %s\r\nCache-Control: no-cache\r\n\r\n",
                  f->type, (unsigned)(f->end - f->start), f->etag);
        s->st = ST_FILE;
        s->file = f;
        s->off = 0;
        pump(c, s);
        return;
    }
    mg_http_reply(c, 404, "Content-Type: text/plain\r\n", "Not found\n");
}

// ---- uploads ------------------------------------------------------------------------------------

static void upload_end(struct mg_connection *c, slot_t *s)
{
    if (s->cert) {  // on every path: done, refused, timed out, closed; a private key is wiped first (Plan 09c)
        if (!strcmp(s->kind, "key"))
            mbedtls_platform_zeroize(s->cert, WEB_CERT_MAX + 1);
        free(s->cert);
        s->cert = NULL;
    }
    if (s->edge_owner) {  // closed, timed out or refused before its commit: the session and its records go
        store_edge_abort();
        s->edge_owner = false;
    }
    if (s->ep == WEB_EP_OTA) {
        ota_stage_release(s);  // nothing after a success: the image waits for the restart
        s_ota_busy = false;
    }
    else
        s_body_busy = false;
    s->st = ST_IDLE;
    c->is_draining = 1;  // HTTP is silenced on this connection: one request, then close
}

// F20 (spec §10.2, Plan 08b): the PEM parsed by mbedTLS, reported, then stored. A client pair that does not
// match is reported, not refused (as the ESP32). Ports read their files at each connection; MQTT at boot.
static void cert_upload(struct mg_connection *c, slot_t *s)
{
    bool is_key = !strcmp(s->kind, "key");
    const char *bad = is_key ? "This is not a PEM private key (encrypted keys are not supported)." : "This is not a PEM certificate.";
    s->cert[s->got] = '\0';
    if (web_pem_check(s->kind, s->cert, s->got)) {
        api_error(c, 400, bad);
        return;
    }
    char *other = strcmp(s->kind, "ca") ? store_cert_get(s->scope, is_key ? "cert" : "key") : NULL;
    static cert_info_t info;  // the web task only
    int r = cert_check(s->kind, s->cert, s->got, other, sysmgr_utc_ms() / 1000, tls_port_rng, &info);
    bool paired = other != NULL;
    free(other);
    if (paired) {
        static bool s_logged;  // the bench reads this once per boot (8 KB stack, an RSA private operation)
        if (!s_logged) {
            s_logged = true;
            SLOG_I("web", "stack headroom after the certificate check: %u bytes",
                   (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
        }
    }
    if (r != 0) {
        api_error(c, 400, bad);
        return;
    }
    if (store_cert_set(s->scope, s->kind, s->cert, s->got) != STORE_OK) {
        api_error(c, 500, "The certificate could not be stored.");
        return;
    }
    cJSON *o = cJSON_CreateObject();
    if (info.subject[0])
        cJSON_AddStringToObject(o, "subject", info.subject);
    if (info.valid_to[0])
        cJSON_AddStringToObject(o, "validTo", info.valid_to);
    if (info.expired >= 0)
        cJSON_AddBoolToObject(o, "expired", info.expired);
    if (info.key_type[0])
        cJSON_AddStringToObject(o, "keyType", info.key_type);
    if (info.pair >= 0)
        cJSON_AddBoolToObject(o, "pairMatches", info.pair);
    cJSON_AddBoolToObject(o, "restartNeeded", !strcmp(s->scope, "mqtt"));
    api_json(c, 200, o);
}

static void upload_complete(struct mg_connection *c, slot_t *s)
{
    char err[160];
    if (s->ep == WEB_EP_OTA) {
        int r = ota_stage_finish(&s_ota);
        if (r == OTA_OK) {
            SLOG_W("web", "firmware uploaded: restarting to install it");
            api_ok(c);
            sysmgr_restart_later(1500);
        } else {
            api_error(c, r == OTA_EFLASH ? 500 : 400, r == OTA_EFLASH ? "The staging flash failed."
                                                                       : "This file is not a valid firmware image for this gateway.");
        }
    } else if (s->ep == WEB_EP_CERT) {
        cert_upload(c, s);
    } else {
        store_err_t e = store_edge_commit(err, sizeof err);
        s->edge_owner = false;  // committed or refused: the session is over either way
        if (e == STORE_OK)
            api_ok(c);
        else
            api_error(c, e == STORE_EINVALID ? 400 : e == STORE_ETOOLARGE ? 413 : 500, err[0] ? err : "The Edge model could not be saved.");
    }
    upload_end(c, s);
}

// Takes what c->recv holds for the upload in progress.
static void upload_take(struct mg_connection *c, slot_t *s)
{
    if (c->recv.len == 0)
        return;
    size_t n = c->recv.len, left = s->expected - s->got;
    if (n > left)
        n = left;  // anything past the announced length is ignored
    s->last_ms = ms_now();
    if (s->ep == WEB_EP_OTA) {
        int r = ota_stage_write(&s_ota, c->recv.buf, (uint32_t)n);
        if (r != OTA_OK) {
            c->recv.len = 0;
            api_error(c, r == OTA_ETOOBIG ? 413 : r == OTA_EFLASH ? 500 : 400,
                      r == OTA_EFLASH ? "The staging flash failed." : "This file is not a valid firmware image for this gateway.");
            size_t rest = s->expected - s->got - n;
            upload_end(c, s);
            if (rest) {  // the rest of the body is still coming: read and drop it, then close (see reject)
                c->is_draining = 0;
                s->st = ST_DISCARD;
                s->expected = rest < DISCARD_MAX ? rest : DISCARD_MAX;
                s->got = 0;
            }
            return;
        }
    } else if (s->ep == WEB_EP_CERT) {
        memcpy(s->cert + s->got, c->recv.buf, n);
    } else {
        store_edge_write(c->recv.buf, n);  // straight into the store's records, checked as it streams (Plan 09c)
    }
    s->got += n;
    c->recv.len = 0;
    if (s->got == s->expected)
        upload_complete(c, s);
}

// Where this request's body starts in c->recv: after earlier pipelined requests, its headers are not
// at offset 0 (Codex 06b review).
static size_t body_offset(struct mg_connection *c, struct mg_http_message *hm)
{
    return (size_t)((const uint8_t *)hm->body.buf - c->recv.buf);
}

// An answer before the body (an error at the headers): the body is still read and dropped, so the
// close that follows is a FIN and not a reset (lwIP resets a socket closed with unread data), and the
// client reads the answer. Chunked or huge bodies: up to DISCARD_MAX, or until they stall.
static void reject(struct mg_connection *c, slot_t *s, struct mg_http_message *hm, int status, const char *msg)
{
    api_error(c, status, msg);
    struct mg_str *cl = mg_http_get_header(hm, "Content-Length");
    s->st = ST_DISCARD;
    s->expected = cl && hm->body.len < DISCARD_MAX ? hm->body.len : DISCARD_MAX;
    s->got = 0;
    s->last_ms = ms_now();
    mg_iobuf_del(&c->recv, 0, body_offset(c, hm));
    c->pfn = NULL;  // what follows is not parsed
    size_t n = c->recv.len < s->expected ? c->recv.len : s->expected;
    s->got += n;
    c->recv.len = 0;
    s->until_quiet = !cl;
    if (s->got >= s->expected)
        c->is_draining = 1;
}

static bool query_var(struct mg_http_message *hm, const char *name, char *out, size_t cap)
{
    return mg_http_get_var(&hm->query, name, out, cap) > 0;
}

// At MG_EV_HTTP_HDRS: takes over an OTA, certificate or edgeDevices POST. True when it did (or
// answered an error), false for a request Mongoose goes on with.
static bool upload_start(struct mg_connection *c, slot_t *s, struct mg_http_message *hm, web_ep_t ep)
{
    if (ep == WEB_EP_CERT) {
        if (!query_var(hm, "scope", s->scope, sizeof s->scope) || !query_var(hm, "kind", s->kind, sizeof s->kind) ||
            (strcmp(s->scope, "mqtt") && strcmp(s->scope, "port1") && strcmp(s->scope, "port2")) ||
            (strcmp(s->kind, "ca") && strcmp(s->kind, "cert") && strcmp(s->kind, "key"))) {
            reject(c, s, hm, 400, "Upload a PEM file for scope mqtt, port1 or port2, kind ca, cert or key.");
            return true;
        }
    }
    if (hm->body.len == 0) {
        reject(c, s, hm, 400, "The file is empty.");
        return true;
    }
    bool *busy = ep == WEB_EP_OTA ? &s_ota_busy : &s_body_busy;
    if (*busy) {
        reject(c, s, hm, ep == WEB_EP_OTA ? 409 : 503, "Another upload is in progress. Try again in a moment.");
        return true;
    }
    if (ep == WEB_EP_CERT && !(s->cert = malloc(WEB_CERT_MAX + 1))) {  // after the checks above: scope, kind, length
        reject(c, s, hm, 503, "Not enough free memory for the upload. Try again in a moment.");
        return true;
    }
    if (ep == WEB_EP_EDGE) {
        char err[96];
        store_err_t e = store_edge_begin(hm->body.len, err, sizeof err);
        if (e != STORE_OK) {
            reject(c, s, hm, e == STORE_EBUSY ? 503 : e == STORE_ETOOLARGE ? 413 : 500, err);
            return true;
        }
        s->edge_owner = true;
    }
    if (ep == WEB_EP_OTA) {
        if (ota_stage_claim(s) != OTA_OK) {  // a UART7 upload, or an image waiting for the restart (Plan 10c)
            reject(c, s, hm, 409, "A firmware image is waiting for the restart, or another upload is in progress.");
            return true;
        }
        int r = ota_stage_begin(&s_ota, flash_spi4(), (uint32_t)hm->body.len);
        if (r != OTA_OK) {
            ota_stage_release(s);
            reject(c, s, hm, r == OTA_ETOOBIG ? 413 : 500, r == OTA_ETOOBIG ? "The file is larger than the firmware slot." : "The update could not start.");
            return true;
        }
    }
    *busy = true;
    s->st = ST_UPLOAD;
    s->ep = ep;
    s->expected = hm->body.len;
    s->got = 0;
    s->last_ms = ms_now();
    mg_iobuf_del(&c->recv, 0, body_offset(c, hm));  // up to this request's body (Codex 06b review)
    c->pfn = NULL;                            // silence Mongoose's HTTP handler: MG_EV_READ from here
    upload_take(c, s);
    return true;
}

// ---- requests -----------------------------------------------------------------------------------

static bool is_api(struct mg_http_message *hm)
{
    return hm->uri.len >= 5 && !memcmp(hm->uri.buf, "/api/", 5);
}

static bool authorized(struct mg_http_message *hm)
{
    struct mg_str *a = mg_http_get_header(hm, "Authorization");
    return a && web_basic_ok(g_web.token, a->buf, a->len);
}

static void on_headers(struct mg_connection *c, slot_t *s, struct mg_http_message *hm)
{
    if (s->hdrs_done)
        return;
    s->hdrs_done = true;
    s->declared = hm->body.len;  // for every request, before any branch returns (Codex 06b review r2)
    bool post = mg_strcmp(hm->method, mg_str("POST")) == 0;
    web_ep_t ep = web_endpoint(post, hm->uri.buf, hm->uri.len);
    struct mg_str *cl = mg_http_get_header(hm, "Content-Length");
    struct mg_str *te = mg_http_get_header(hm, "Transfer-Encoding");
    bool chunked = te && mg_strcasecmp(*te, mg_str("chunked")) == 0;
    int code = web_body_check(ep, cl != NULL, chunked, hm->body.len, SLOT_SIZE);
    if (code) {
        reject(c, s, hm, code, code == 411 ? "Send the body with a Content-Length." : "The request is too large.");
        return;
    }
    if (is_api(hm) && !authorized(hm)) {
        if (hm->body.len)  // do not wait for a body that will not be used
            reject(c, s, hm, 401, "Sign in required.");
        return;  // answered at MG_EV_HTTP_MSG otherwise
    }
    if (ep != WEB_EP_PLAIN) {
        upload_start(c, s, hm, ep);
        return;
    }
    s->waiting = hm->body.len > 0;
    s->last_ms = ms_now();
}

static void on_message(struct mg_connection *c, slot_t *s, struct mg_http_message *hm)
{
    s->waiting = false;
    s->hdrs_done = false;  // the next request on this connection
    if (c->is_draining)
        return;  // answered at the headers
    if (hm->body.len != s->declared)
        return;  // the client closed before its whole body came (Mongoose still delivers it): no effect
    // A client that sends requests without reading the answers: close it rather than queue them all.
    if (c->send.len >= 2 * WEB_SEND_HIGH) {
        c->is_closing = 1;
        return;
    }
    if (!is_api(hm)) {
        serve_file(c, s, hm);
        return;
    }
    if (!authorized(hm)) {
        api_error(c, 401, "Sign in required.");
        return;
    }
    bool get = mg_strcmp(hm->method, mg_str("GET")) == 0;
    if (get && mg_strcmp(hm->uri, mg_str("/api/config/edgeDevices")) == 0) {
        store_edge_pin(&s->pin);  // first: a pin that failed gets an error, not a 200 (Codex 06b review)
        if (s->pin.count == 0) {
            api_error(c, 503, s->pin.gen ? "The Edge model is being read by too many clients; try again."
                                         : "The Edge model storage is unavailable; restart the gateway.");
            return;
        }
        mg_printf(c, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n"
                     "Transfer-Encoding: chunked\r\n\r\n");
        s->st = ST_EDGE_GET;
        s->rec = 0;
        pump(c, s);
        return;
    }
    if (get && mg_strcmp(hm->uri, mg_str("/api/log")) == 0) {
        mg_printf(c, "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\n"
                     "Transfer-Encoding: chunked\r\n\r\n");
        mg_http_printf_chunk(c, "# Warnings and errors kept in flash\n");
        s->st = ST_LOG;
        s->log_phase = 0;
        s->log_sent = 0;
        pump(c, s);
        return;
    }
    api_handle(c, hm);  // cJSON on the heap: every handler frees what it builds (Plan 09c)
}

static void handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_ACCEPT) {
        slot_t *free_slot = NULL;
        for (int i = 0; i < WEB_MAX_CONNS && !free_slot; i++)
            if (s_slots[i].st == ST_FREE)
                free_slot = &s_slots[i];
        if (!free_slot) {
            c->is_closing = 1;  // spec §5.6: the web takes at most WEB_MAX_CONNS connections
            return;
        }
        memset(free_slot, 0, sizeof *free_slot);
        free_slot->st = ST_IDLE;
        free_slot->last_ms = ms_now();
        memcpy(c->data, &free_slot, sizeof free_slot);
        return;
    }
    if (c->is_listening)
        return;
    slot_t *s = slot_of(c);
    if (!s)
        return;
    switch (ev) {
    case MG_EV_HTTP_HDRS:
        on_headers(c, s, ev_data);
        break;
    case MG_EV_HTTP_MSG:
        on_message(c, s, ev_data);
        break;
    case MG_EV_READ:
        s->last_ms = ms_now();
        if (s->st == ST_UPLOAD)
            upload_take(c, s);
        else if (s->st == ST_DISCARD) {
            s->got += c->recv.len;
            c->recv.len = 0;
            if (s->got >= s->expected)
                c->is_draining = 1;
        }
        break;
    case MG_EV_POLL:
        c->is_full = c->send.len >= WEB_SEND_HIGH;  // no more requests read while answers wait (Codex 06b review)
        if (s->st == ST_IDLE && !s->waiting && c->send.len == 0 && ms_now() - s->last_ms >= IDLE_MS)
            c->is_closing = 1;  // an idle connection gives its slot back
        if (s->st == ST_DISCARD && (web_stalled(s->last_ms, ms_now()) || (s->until_quiet && ms_now() - s->last_ms >= QUIET_MS)))
            c->is_draining = 1;  // the rest never came (or a chunked body ended): close
        if ((s->st == ST_UPLOAD || s->waiting) && web_stalled(s->last_ms, ms_now())) {
            SLOG_W("web", "a request body stopped arriving (%lu ms silent)", (unsigned long)(ms_now() - s->last_ms));
            api_error(c, 408, "The request body stopped arriving.");
            if (s->st == ST_UPLOAD)
                upload_end(c, s);
            s->waiting = false;
            c->is_draining = 1;
        }
        // a stream may go on
        __attribute__((fallthrough));
    case MG_EV_WRITE:
        if (s->st == ST_FILE || s->st == ST_EDGE_GET || s->st == ST_LOG)
            pump(c, s);
        break;
    case MG_EV_CLOSE:
        if (s->st == ST_UPLOAD)
            upload_end(c, s);
        finish_stream(s);  // unpins an Edge GET on every path (Codex 06b r1)
        s->st = ST_FREE;
        memset(c->data, 0, sizeof s);
        break;
    default:
        break;
    }
}

static void web_task(void *arg)
{
    (void)arg;
    char url[32];
    snprintf(url, sizeof url, "http://0.0.0.0:%d", g_web.web_port);
    mg_mgr_init(&s_mgr);
    if (!mg_http_listen(&s_mgr, url, handler, NULL))
        SLOG_E("web", "cannot listen on port %d", g_web.web_port);
    else
        blog("web: UI and API on port %d\r\n", g_web.web_port);
    for (;;)
        mg_mgr_poll(&s_mgr, POLL_MS);
}

static void str_into(char *out, size_t cap, const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    snprintf(out, cap, "%s", cJSON_IsString(v) ? v->valuestring : "");
}

static int int_of(const cJSON *o, const char *k, int dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valueint : dflt;
}

int webapi_start(const cJSON *system)
{
    char user[17], pass[17];
    str_into(user, sizeof user, system, "username");
    str_into(pass, sizeof pass, system, "password");
    str_into(g_web.hostname, sizeof g_web.hostname, system, "hostname");
    web_token_make(user, pass, g_web.token, sizeof g_web.token);
    g_web.web_port = int_of(system, "webPort", 80);
    g_web.ws_port = int_of(system, "websocketPort", 6432);
    g_web.ws_target = int_of(system, "websocketTarget", 0);
    json_alloc_install();
    xTaskCreateStatic(web_task, "web", WEB_STACK_WORDS, NULL, WEB_PRIO, s_web_stack, &s_tcb);
    if (g_web.ws_target != 0)
        return console_start(g_web.ws_port, g_web.ws_target);
    return 0;
}
