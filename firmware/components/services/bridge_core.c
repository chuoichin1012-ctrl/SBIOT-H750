#include "bridge_core.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "modbus.h"

static int num(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valueint : 0;
}

static bool flag(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valueint != 0);
}

static void str(const cJSON *o, const char *k, char *out, size_t len)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    snprintf(out, len, "%s", cJSON_IsString(v) ? v->valuestring : "");
}

void bridge_cfg_parse(const cJSON *p, const cJSON *sys, bridge_cfg_t *out)
{
    bridge_cfg_t c;
    memset(&c, 0, sizeof c);
    c.work = num(p, "workMode");
    c.extra = num(p, "socketMode");
    str(p, "remoteHost", c.host, sizeof c.host);
    c.remote_port = (uint16_t)num(p, "remotePort");
    c.local_port = (uint16_t)num(p, "localPort");
    c.any_port = num(p, "udpAnyRemotePort") == 1;
    c.listen_port = (uint16_t)num(p, "listenPort");
    c.max_clients = num(p, "maxClients");
    if (c.max_clients < 1 || c.max_clients > BR_MAX_CLIENTS)
        c.max_clients = BR_MAX_CLIENTS;
    c.limit_action = num(p, "clientLimitAction");
    char acl[144];
    str(p, "allowedClients", acl, sizeof acl);
    c.acl_bad = !ipacl_parse(acl, &c.acl);
    if (c.acl_bad)
        ipacl_parse("", &c.acl);  // ignored: anyone (logged by the bridge)
    str(p, "httpHost", c.http_host, sizeof c.http_host);
    c.http_port = (uint16_t)num(p, "httpPort");
    c.http_local = (uint16_t)num(p, "httpLocalPort");
    c.http_post = num(p, "httpMethod") == 1;
    str(p, "httpPath", c.http_path, sizeof c.http_path);
    str(p, "httpHeaders", c.http_headers, sizeof c.http_headers);
    c.http_strip = flag(p, "httpStripHeaders");
    c.http_timeout_s = num(p, "httpTimeout");
    c.reconnect_s = num(p, "reconnectInterval");
    c.short_s = num(p, "shortConnectionTime");
    c.poll = flag(p, "modbusPoll");
    c.poll_timeout_ms = num(p, "modbusPollTimeout");
    c.exception = flag(p, "modbusException");
    c.hb = flag(p, "netHeartbeat");
    c.hb_type = num(p, "netHeartbeatContent");
    c.hb_hex = num(p, "netHeartbeatHex") == 1;
    str(p, "netHeartbeatData", c.hb_data, sizeof c.hb_data);
    c.hb_interval_s = num(p, "netHeartbeatInterval");
    c.reg = num(p, "registration");
    c.reg_send = num(p, "registrationSend");
    c.reg_hex = num(p, "registrationHex") == 1;
    str(p, "registrationData", c.reg_data, sizeof c.reg_data);
    c.tls = num(p, "tls");
    c.tls_verify = num(p, "tlsVerify");
    c.keep_offline = num(sys, "bufferOffline") == 1;
    // The UI only offers these where they apply; stale combinations are ignored (as the ESP32).
    if (c.work != BR_TCP_CLIENT && c.work != BR_HTTP_CLIENT)
        c.tls = 0;
    if (c.work != BR_UDP_CLIENT && c.work != BR_TCP_CLIENT)
        c.reg = 0;
    if (c.work == BR_TCP_SERVER || c.work == BR_HTTP_CLIENT)
        c.hb = false;
    bool tcp = c.work == BR_TCP_CLIENT || c.work == BR_TCP_SERVER;
    c.modbus_gw = tcp && (c.extra == BR_X_MODBUS || c.extra == BR_X_MODBUS_SHORT);
    c.modbus_client = c.modbus_gw && c.work == BR_TCP_CLIENT;
    c.short_conn = c.work == BR_TCP_CLIENT && (c.extra == BR_X_SHORT || c.extra == BR_X_MODBUS_SHORT);
    *out = c;
}

int bridge_reg_content(int registration)
{
    // registration 1 custom, 2 MAC, 3 IMEI, 4 SN, 5 ICCID -> sysmgr_payload 0 custom, 4 MAC, 1 IMEI, 2 SN, 3 ICCID
    static const int content[] = {-1, 0, 4, 1, 2, 3};
    return registration >= 1 && registration <= 5 ? content[registration] : -1;
}

// ---- Modbus TCP reassembly ----------------------------------------------------------------------

void mbtcp_push(mbtcp_acc_t *a, const uint8_t *data, size_t len, mbtcp_fn fn, void *ctx)
{
    for (size_t i = 0; i < len; i++) {
        a->buf[a->len++] = data[i];
        for (;;) {
            int n = mb_tcp_frame_len(a->buf, a->len);
            if (n > 0) {
                fn(ctx, a->buf, (size_t)n);
                a->len = 0;
                break;
            }
            if (n == 0 && a->len < sizeof a->buf)
                break;  // more to come
            // Not Modbus TCP from here: drop one byte and look again, so the next frame is found.
            memmove(a->buf, a->buf + 1, --a->len);
            if (!a->len)
                break;
        }
    }
}

// ---- HTTP -----------------------------------------------------------------------------------------

// The settings' header lines, one per line, each ended with CRLF.
static size_t header_lines(const char *src, char *out, size_t max)
{
    size_t n = 0;
    for (const char *p = src; *p;) {
        size_t l = strcspn(p, "\r\n");
        if (l && n + l + 2 < max) {
            memcpy(out + n, p, l);
            n += l;
            out[n++] = '\r';
            out[n++] = '\n';
        }
        p += l;
        while (*p == '\r' || *p == '\n')
            p++;
    }
    out[n] = '\0';
    return n;
}

size_t http_head(const bridge_cfg_t *c, size_t len, char *out, size_t max)
{
    char hdrs[200];
    header_lines(c->http_headers, hdrs, sizeof hdrs);
    int n = c->http_post ? snprintf(out, max, "POST %s HTTP/1.0\r\nHost: %s:%u\r\nContent-Length: %u\r\n%s\r\n", c->http_path,
                                    c->http_host, (unsigned)c->http_port, (unsigned)len, hdrs)
                         : snprintf(out, max, "GET %s", c->http_path);
    return n > 0 && (size_t)n < max ? (size_t)n : 0;
}

size_t http_tail(const bridge_cfg_t *c, char *out, size_t max)
{
    if (c->http_post)
        return 0;
    char hdrs[200];
    header_lines(c->http_headers, hdrs, sizeof hdrs);
    int n = snprintf(out, max, " HTTP/1.0\r\nHost: %s:%u\r\n%s\r\n", c->http_host, (unsigned)c->http_port, hdrs);
    return n > 0 && (size_t)n < max ? (size_t)n : 0;
}

const uint8_t *http_body(const uint8_t *resp, size_t len, bool strip, size_t *out_len)
{
    *out_len = len;
    if (!strip)
        return resp;
    for (size_t i = 0; i + 4 <= len; i++)
        if (!memcmp(resp + i, "\r\n\r\n", 4)) {
            *out_len = len - i - 4;
            return resp + i + 4;
        }
    return resp;  // no blank line: all of it (as the ESP32)
}

// ---- sending ----------------------------------------------------------------------------------------

int net_classify(int err)
{
    // lwIP reports its own shortage on a non-blocking write as EWOULDBLOCK too (api_msg.c): one class.
    return err == EWOULDBLOCK || err == EAGAIN || err == ENOMEM || err == ENOBUFS ? NET_AGAIN : NET_FAIL;
}

int tx_finish(tx_conn_t *c, const uint8_t *frame, size_t len, tx_send_fn send, void *ctx, uint32_t (*now_ms)(void),
              void (*wait)(void *ctx), uint32_t limit_ms)
{
    uint32_t t0 = now_ms();
    while (c->busy) {
        int r = tx_progress(c, frame, len, send, ctx, now_ms());
        if (r != TX_PENDING)
            return r;
        if (now_ms() - t0 >= limit_ms)
            return TX_LOST;
        wait(ctx);
    }
    return TX_DONE;
}

int tx_progress(tx_conn_t *c, const uint8_t *frame, size_t len, tx_send_fn send, void *ctx, uint32_t now_ms)
{
    if (!c->busy) {
        c->busy = true;
        c->off = 0;
        c->progress_ms = now_ms;
    }
    int r = send(ctx, frame + c->off, len - c->off);  // one attempt a call: the work per pass stays bounded
    if (r > 0) {
        c->off += (size_t)r;
        c->progress_ms = now_ms;
    } else if (r == NET_FAIL || now_ms - c->progress_ms >= TX_STALL_MS) {
        c->busy = false;
        c->off = 0;
        return TX_LOST;
    }
    if (c->off < len)
        return TX_PENDING;
    c->busy = false;
    c->off = 0;
    return TX_DONE;
}

bool idle_due(uint64_t now_us, uint64_t last_us, uint64_t limit_us) { return now_us >= last_us && now_us - last_us >= limit_us; }
