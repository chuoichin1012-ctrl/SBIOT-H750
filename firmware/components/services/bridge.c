// A port's network bridge (spec §6.4, §5.6, Plan 08a): the ESP32's bridge.c made one instance per port,
// without RFC 2217 and PUSR. Serial frames reach it through a MessageBuffer filled by the port's
// subscriber on serial_rx[n] (which never touches the network); one task per port runs the mode:
// 0 UDP client, 1 TCP client, 2 UDP server, 3 TCP server, 4 HTTP client; socketMode 1 Modbus TCP
// gateway, 2 short connection, 3 both, 4 multicast (UDP client).
// Every kept socket is non-blocking: a frame goes to each connection through tx_progress, its own offset
// kept, so a shortage never closes a socket; only 10 s without a byte taken does (bridge_core.h). A pass
// does bounded work and, when it did any, sleeps a tick: bridge[n] (prio 8) never starves the tasks below.
#include "bridge.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "io_mb.h"
#include "message_buffer.h"
#include "netmgr.h"
#include "now_us.h"
#include "serial.h"
#include "services.h"
#include "svc_net.h"
#include "syslog.h"
#include "sysmgr.h"
#include "task.h"

#define AXI_BSS __attribute__((section(".axi_bss")))
#define SRAM23_BSS __attribute__((section(".sram23_bss")))
#define RB_ONLINE (8 * 1024)
#define RB_OFFLINE (32 * 1024)  // bufferOffline: kept while the peer is away (spec §13: 32 KB a port)
#define FRAME_MAX (SERIAL_FRAME_MAX + 48)  // a serial frame and a registration prefix
#define NET_BUF 1500
#define STACK_WORDS 1536  // 6 KB (spec §6.1)
#define PRIO 8
#define PASS_FRAMES 8
#define PASS_READS 8
#define PASS_ACCEPTS 8
#define REPLY_MS 2000u  // a reply to a request (Modbus, polling) has this long to go out

typedef struct {
    conn_t c;
    tx_conn_t tx;
    bool pend;  // the current frame is still to be sent to it
    uint64_t since;
    mbtcp_acc_t acc;
    uint16_t tid;             // Modbus TCP client: the transaction id of the request in flight
    uint64_t mb_deadline_us;  // ... awaited until then
    uint8_t mb_unit, mb_fc;
} client_t;

typedef struct {
    int n;
    bool on;
    bridge_cfg_t C;
    MessageBufferHandle_t mb;
    StaticMessageBuffer_t mb_mem;
    volatile bool online;
    char state[20];
    volatile uint32_t dropped;
    client_t cli[BR_MAX_CLIENTS];
    int ncli;
    int rr;  // where the next pass starts reading: every client gets its turn (Codex code review r1)
    uint8_t frame[FRAME_MAX];
    size_t frame_len;
    bool have;
    int udp;
    struct sockaddr_in peer;
    bool peer_ok;
    uint64_t last_io_us;
    StaticTask_t tcb;
    union {  // one mode a port: the HTTP client's response, or a read and its answer
        uint8_t http[4096];
        struct {
            uint8_t rx[NET_BUF], ans[NET_BUF];
        } io;
    } buf;
} bridge_t;

// The bridges' state and buffers live in SRAM2+3 with their queues (spec §5.3), not in DTCM.
static bridge_t s_br[SERIAL_PORTS] SRAM23_BSS;
static uint8_t s_mb_store[SERIAL_PORTS][RB_OFFLINE + 1] SRAM23_BSS;
static StackType_t s_bridge_stack[SERIAL_PORTS][STACK_WORDS] AXI_BSS;
static volatile int s_reserve = 4;  // spec §5.6; bench Y raises it

static uint32_t now_ms(void) { return (uint32_t)(now_us() / 1000u); }

static void set_state(bridge_t *b, const char *s)
{
    taskENTER_CRITICAL();
    snprintf(b->state, sizeof b->state, "%s", s);
    taskEXIT_CRITICAL();
}

// ---- serial -> the queue (on serial_rx[n]: never blocks) ---------------------------------------

static void on_serial(int port, const uint8_t *data, size_t len, void *ctx)
{
    bridge_t *b = ctx;
    // Without a peer the data is dropped, unless it is kept for later (bufferOffline) or opens a short connection.
    if (!b->online && !b->C.keep_offline && !b->C.short_conn)
        return;
    if (xMessageBufferSend(b->mb, data, len, 0) != len)
        b->dropped++;
}

// The next frame into b->frame (with the registration prefix where configured); false when none.
static bool take_frame(bridge_t *b, bool reg_prefix)
{
    if (b->have)
        return true;
    size_t pre = 0;
    if (reg_prefix && b->C.reg && (b->C.reg_send == BR_REG_PREFIX || b->C.reg_send == BR_REG_BOTH))
        pre = sysmgr_payload(bridge_reg_content(b->C.reg), b->C.reg_data, b->C.reg_hex, b->frame, 48);
    size_t n = xMessageBufferReceive(b->mb, b->frame + pre, sizeof b->frame - pre, 0);
    if (!n)
        return false;
    b->frame_len = pre + n;
    b->have = true;
    return true;
}

static size_t registration(bridge_t *b, uint8_t *out, size_t max)
{
    return b->C.reg ? sysmgr_payload(bridge_reg_content(b->C.reg), b->C.reg_data, b->C.reg_hex, out, max) : 0;
}

static size_t heartbeat(bridge_t *b, uint8_t *out, size_t max)
{
    return sysmgr_payload(b->C.hb_type, b->C.hb_data, b->C.hb_hex, out, max);
}

// ---- network -> serial ------------------------------------------------------------------------

typedef void (*reply_fn)(void *ctx, const uint8_t *data, size_t len);

static void reply_tcp(void *ctx, const uint8_t *data, size_t len)
{
    client_t *cl = ctx;
    if (!conn_send_all(&cl->c, data, len, REPLY_MS))
        conn_close(&cl->c);  // the caller drops it
}

// The Modbus TCP gateway: one ADU -> RTU on this port's bus -> the ADU reply (the IO slave from the IO map).
static void modbus_request(bridge_t *b, const uint8_t *adu, size_t len, reply_fn reply, void *ctx)
{
    uint8_t rtu[260], resp[260], out[MBTCP_MAX];
    uint16_t tid;
    size_t n = mb_tcp_to_rtu(adu, len, rtu, sizeof rtu, &tid);
    if (!n)
        return;
    size_t m = 0;
    if (rtu[0] == io_mb_address()) {  // the gateway's own IO, on either port (spec §6.4)
        size_t r = mb_serve(io_mb_map(), rtu, n - 2, resp, sizeof resp);
        m = mb_tcp_wrap(resp, r, tid, out, sizeof out);
    } else {
        int got = serial_transact(b->n, rtu, n, resp, sizeof resp, rtu[0], (uint32_t)(b->C.poll ? b->C.poll_timeout_ms : 1000));
        m = got > 0 ? mb_rtu_to_tcp(resp, (size_t)got, tid, out, sizeof out) : 0;
        if (!m && b->C.exception) {  // no (valid) answer: tell the master at once
            const uint8_t ex[] = {rtu[0], (uint8_t)(rtu[1] | 0x80), MB_EX_TARGET_NO_RESPONSE};
            m = mb_tcp_wrap(ex, sizeof ex, tid, out, sizeof out);
        }
    }
    if (m)
        reply(ctx, out, m);
}

typedef struct {
    bridge_t *b;
    client_t *cl;
    reply_fn reply;
    void *ctx;
} adu_ctx_t;

static void on_adu(void *ctx, const uint8_t *adu, size_t len)
{
    adu_ctx_t *a = ctx;
    client_t *cl = a->cl;
    if (!a->b->C.modbus_client) {
        modbus_request(a->b, adu, len, a->reply, a->ctx);
        return;
    }
    // The Ethernet slave answered: back to the serial master as RTU, only for the request in flight.
    uint16_t tid = (uint16_t)(adu[0] << 8 | adu[1]);
    size_t body = len - 6;
    uint8_t rtu[262];
    if (cl->mb_deadline_us && tid == cl->tid && body + 2 <= sizeof rtu && adu[6] == cl->mb_unit) {
        memcpy(rtu, adu + 6, body);
        serial_write(a->b->n, rtu, mb_rtu_seal(rtu, body), 2000);
        cl->mb_deadline_us = 0;
    }
}

// Data from a network peer. `cl` carries the per-connection state (NULL for UDP).
static void from_network(bridge_t *b, client_t *cl, const uint8_t *data, size_t len, reply_fn reply, void *ctx)
{
    uint8_t *answer = b->buf.io.ans;
    svc_net_activity();
    b->last_io_us = now_us();
    if (b->C.modbus_gw) {
        if (cl) {
            adu_ctx_t a = {b, cl, reply, ctx};
            mbtcp_push(&cl->acc, data, len, on_adu, &a);
        }
        return;
    }
    if (b->C.poll && (b->C.work == BR_TCP_CLIENT || b->C.work == BR_TCP_SERVER)) {
        // Polling: one request at a time; the answer goes to whoever asked.
        size_t n = io_mb_remote(data, len, answer, NET_BUF);
        if (n) {
            reply(ctx, answer, n);
            return;
        }
        bool rtu = mb_rtu_valid(data, len);
        int got = serial_transact(b->n, data, len, answer, NET_BUF, rtu ? data[0] : -1, (uint32_t)b->C.poll_timeout_ms);
        if (got > 0)
            reply(ctx, answer, (size_t)got);
        return;
    }
    serial_write(b->n, data, len, 2000);
}

// ---- sending a frame to TCP connections --------------------------------------------------------

static int send_cl(void *ctx, const uint8_t *data, size_t len) { return conn_send_some(&((client_t *)ctx)->c, data, len); }

// An RTU request from the serial master -> Modbus TCP (the gateway as a client); the frame becomes the ADU.
static bool frame_to_adu(bridge_t *b, client_t *cl)
{
    uint8_t adu[MBTCP_MAX];
    if (!mb_rtu_valid(b->frame, b->frame_len)) {
        SLOG_W("bridge", "UART%d: %u serial bytes are not an RTU frame, dropped", b->n, (unsigned)b->frame_len);
        return false;
    }
    size_t n = mb_tcp_wrap(b->frame, b->frame_len - 2, ++cl->tid, adu, sizeof adu);
    cl->mb_unit = b->frame[0];
    cl->mb_fc = b->frame[1];
    cl->mb_deadline_us = now_us() + (uint64_t)(b->C.poll ? b->C.poll_timeout_ms : 1000) * 1000u;
    memcpy(b->frame, adu, n);
    b->frame_len = n;
    return n > 0;
}

static void drop_client(bridge_t *b, int i)
{
    conn_close(&b->cli[i].c);
    b->cli[i] = b->cli[--b->ncli];
}

// A lost connection (10 s without progress) is reset, so lwIP frees its queued data at once.
static void abort_client(bridge_t *b, int i)
{
    struct linger lg = {.l_onoff = 1, .l_linger = 0};
    lwip_setsockopt(b->cli[i].c.fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
    conn_close(&b->cli[i].c);
    b->cli[i] = b->cli[--b->ncli];
}

// Up to PASS_FRAMES frames to every connection; true when it tried anything. Lost connections are dropped.
static bool send_frames(bridge_t *b, bool reg_prefix)
{
    bool work = false;
    for (int k = 0; k < PASS_FRAMES && b->ncli; k++) {
        if (!b->have) {
            if (!take_frame(b, reg_prefix))
                break;
            if (b->C.modbus_client && !frame_to_adu(b, &b->cli[0])) {
                b->have = false;
                continue;
            }
            for (int i = 0; i < b->ncli; i++)
                b->cli[i].pend = true;
        }
        bool all = true;
        uint32_t now = now_ms();
        for (int i = b->ncli - 1; i >= 0; i--) {
            client_t *cl = &b->cli[i];
            if (!cl->pend)
                continue;
            work = true;
            int r = tx_progress(&cl->tx, b->frame, b->frame_len, send_cl, cl, now);
            if (r == TX_DONE) {
                cl->pend = false;
                svc_net_activity();
                b->last_io_us = now_us();
            } else if (r == TX_LOST) {
                SLOG_W("bridge", "UART%d: a peer took nothing for 10 s or failed: dropped", b->n);
                abort_client(b, i);
            } else {
                all = false;
            }
        }
        if (!all)
            break;
        b->have = false;
    }
    if (!b->ncli)
        b->have = false;  // nobody left to send it to
    return work;
}

// select() for the clients (and the listener when given): readable, and writable for a frame in flight.
// A TLS client whose bytes are already inside mbedTLS counts as readable: select cannot see them, so
// the wait is 0 and its fd is added to rd (Plan 08b, wait_ready).
static int wait(bridge_t *b, int lfd, fd_set *rd, uint32_t ms)
{
    fd_set wr;
    FD_ZERO(rd);
    FD_ZERO(&wr);
    int maxfd = -1;
    bool held = false;
    if (lfd >= 0) {
        FD_SET(lfd, rd);
        maxfd = lfd;
    }
    for (int i = 0; i < b->ncli; i++) {
        FD_SET(b->cli[i].c.fd, rd);
        if (b->cli[i].pend)
            FD_SET(b->cli[i].c.fd, &wr);
        if (b->cli[i].c.fd > maxfd)
            maxfd = b->cli[i].c.fd;
        held |= conn_pending(&b->cli[i].c);
    }
    if (maxfd < 0) {
        vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
        return 0;
    }
    struct timeval tv = {.tv_sec = 0, .tv_usec = held ? 0 : (long)ms * 1000};
    int n = lwip_select(maxfd + 1, rd, &wr, NULL, &tv), extra = 0;
    if (n < 0)
        FD_ZERO(rd);
    for (int i = 0; held && i < b->ncli; i++)
        if (conn_pending(&b->cli[i].c) && !FD_ISSET(b->cli[i].c.fd, rd)) {
            FD_SET(b->cli[i].c.fd, rd);
            extra++;
        }
    return wait_ready(n, extra);
}

// Reads what the clients sent, at most PASS_READS reads; drops those that closed. True when it read.
static bool read_clients(bridge_t *b, fd_set *rd)
{
    uint8_t *buf = b->buf.io.rx;
    bool work = false, gone[BR_MAX_CLIENTS] = {false};
    int reads = 0, n = b->ncli, start = n ? b->rr % n : 0, k = 0;
    for (; k < n && reads < PASS_READS; k++) {  // round robin from where the last pass stopped
        int i = (start + k) % n;
        client_t *cl = &b->cli[i];
        if (!FD_ISSET(cl->c.fd, rd))
            continue;
        int r = conn_recv(&cl->c, buf, NET_BUF);
        reads++;
        work = true;
        if (r > 0)
            from_network(b, cl, buf, (size_t)r, reply_tcp, cl);
        if (r == 0 || r == NET_FAIL || cl->c.fd < 0)
            gone[i] = true;
    }
    b->rr = start + k;
    for (int i = n - 1; i >= 0; i--)  // dropped after the pass: the indices above stay valid during it
        if (gone[i]) {
            SLOG_I("bridge", "UART%d: a client left", b->n);
            drop_client(b, i);
        }
    return work;
}

// ---- TLS (Plan 08b) ----------------------------------------------------------------------------

static const char *const SCOPE[] = {"port1", "port2"};  // the certificates' scope (store_cert_get)

// The bench reads this once per boot and kind (6 KB stacks, RSA-2048 handshakes: spec §6.1, Plan 08b).
static void headroom(bridge_t *b, int kind, const char *what)
{
    static bool s_done[SERIAL_PORTS][2];
    if (s_done[b->n - 1][kind])
        return;
    s_done[b->n - 1][kind] = true;
    SLOG_I("bridge", "UART%d: stack headroom after %s: %u bytes", b->n, what,
           (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
}

// ---- TCP server --------------------------------------------------------------------------------

static void tcp_server(bridge_t *b)
{
    int lfd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), one = 1;
    lwip_setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in me = {.sin_family = AF_INET, .sin_port = lwip_htons(b->C.listen_port)};
    if (lfd < 0 || lwip_bind(lfd, (struct sockaddr *)&me, sizeof me) != 0 || lwip_listen(lfd, DEFAULT_ACCEPTMBOX_SIZE) != 0) {
        SLOG_E("bridge", "UART%d: cannot listen on port %u", b->n, b->C.listen_port);
        set_state(b, "ERROR");
        return;
    }
    net_nonblock(lfd);
    SLOG_I("bridge", "UART%d: listening on port %u (up to %d clients)", b->n, b->C.listen_port, b->C.max_clients);
    for (;;) {
        char st[20];
        snprintf(st, sizeof st, b->ncli ? "CONNECTED(%d)" : "LISTEN", b->ncli);
        set_state(b, st);
        b->online = b->ncli > 0;
        bool work = send_frames(b, false);
        fd_set rd;
        bool gate = accept_allowed(netmgr_netconn_free(), s_reserve);
        int n = wait(b, gate ? lfd : -1, &rd, work ? 0 : 10);
        for (int k = 0; n > 0 && gate && FD_ISSET(lfd, &rd) && k < PASS_ACCEPTS; k++) {  // every pending one (Codex r1)
            struct sockaddr_in from;
            socklen_t fl = sizeof from;
            int fd = lwip_accept(lfd, (struct sockaddr *)&from, &fl);
            if (fd < 0)
                break;
            work = true;
            uint32_t ip = lwip_ntohl(from.sin_addr.s_addr);
            if (fd >= 0 && !ipacl_allows(&b->C.acl, ip)) {
                SLOG_W("bridge", "UART%d: client %s not in allowedClients: refused", b->n, inet_ntoa(from.sin_addr));
                closesocket(fd);
                fd = -1;
            }
            if (fd >= 0 && b->ncli >= b->C.max_clients) {
                if (b->C.limit_action == 1) {  // keep the existing clients
                    SLOG_W("bridge", "UART%d: client limit reached, refusing %s", b->n, inet_ntoa(from.sin_addr));
                    closesocket(fd);
                    fd = -1;
                } else {  // drop the oldest
                    int old = 0;
                    for (int i = 1; i < b->ncli; i++)
                        if (b->cli[i].since < b->cli[old].since)
                            old = i;
                    SLOG_W("bridge", "UART%d: client limit reached, dropping the oldest", b->n);
                    drop_client(b, old);
                }
            }
            if (fd >= 0) {
                net_nonblock(fd);
                lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                client_t *cl = &b->cli[b->ncli++];
                memset(cl, 0, sizeof *cl);
                cl->c.fd = fd;
                cl->since = now_us();
                SLOG_I("bridge", "UART%d: client %s connected (%d)", b->n, inet_ntoa(from.sin_addr), b->ncli);
            }
        }
        if (n > 0 && read_clients(b, &rd))
            work = true;
        if (work)
            vTaskDelay(1);  // bounded work done: give way (Codex r2)
    }
}

// ---- TCP client --------------------------------------------------------------------------------

static void tcp_client(bridge_t *b)
{
    client_t *cl = &b->cli[0];
    uint64_t next_try = 0;  // now_us of the next dial; 64 bits: no wrap (Codex code review r1)
    uint32_t last_hb = 0;
    cl->c.fd = -1;
    for (;;) {
        if (b->ncli == 0) {
            b->online = false;
            if (b->C.short_conn && xMessageBufferIsEmpty(b->mb) == pdTRUE) {  // short connection: dial only on data
                set_state(b, "WAITING");
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (!netmgr_online() || !wait_over(now_us(), next_try)) {
                set_state(b, "CONNECTING");
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            set_state(b, "CONNECTING");
            int fd = net_tcp_connect(b->C.host, b->C.remote_port, b->C.local_port, 10000);
            tls_conn_t *tls = NULL;
            if (fd >= 0 && b->C.tls) {  // the session before CONNECTED: a failed handshake is a failed connect
                tls = tls_open(fd, SCOPE[b->n - 1], b->C.host, b->C.tls_verify, 10000, NULL);
                if (!tls) {
                    closesocket(fd);
                    fd = -1;
                } else {
                    headroom(b, 0, "the TLS handshake");
                }
            }
            if (fd < 0) {
                next_try = now_us() + (uint64_t)(b->C.reconnect_s > 0 ? b->C.reconnect_s : 1) * 1000000u;
                continue;
            }
            memset(cl, 0, sizeof *cl);
            cl->c.fd = fd;
            cl->c.tls = tls;
            b->ncli = 1;
            b->have = false;
            SLOG_I("bridge", "UART%d: connected to %s:%u", b->n, b->C.host, b->C.remote_port);
            set_state(b, "CONNECTED");
            b->online = true;
            last_hb = now_ms();
            b->last_io_us = now_us();
            uint8_t reg[48];
            size_t n = b->C.reg_send == BR_REG_ONCE || b->C.reg_send == BR_REG_BOTH ? registration(b, reg, sizeof reg) : 0;
            if (n && !conn_send_all(&cl->c, reg, n, REPLY_MS))
                goto lost;
        }
        bool work = send_frames(b, !b->C.modbus_client);  // a Modbus request carries no registration prefix
        if (b->ncli == 0)
            goto lost;
        if (work)
            last_hb = now_ms();  // heartbeats only fill silences
        fd_set rd;
        int n = wait(b, -1, &rd, work ? 0 : 10);
        if (n > 0 && read_clients(b, &rd))
            work = true;
        if (b->ncli == 0)
            goto lost;
        if (b->C.hb && !b->have && now_ms() - last_hb >= (uint32_t)b->C.hb_interval_s * 1000u) {
            uint8_t hb[48];
            size_t hn = heartbeat(b, hb, sizeof hb);
            if (hn && !conn_send_all(&cl->c, hb, hn, REPLY_MS))
                goto lost;
            last_hb = now_ms();
        }
        if (cl->mb_deadline_us && now_us() > cl->mb_deadline_us) {
            cl->mb_deadline_us = 0;
            if (b->C.exception) {  // the Ethernet slave did not answer: tell the serial master
                uint8_t ex[5] = {cl->mb_unit, (uint8_t)(cl->mb_fc | 0x80), MB_EX_TARGET_NO_RESPONSE};
                serial_write(b->n, ex, mb_rtu_seal(ex, 3), 1000);
            }
        }
        if (b->C.short_conn && !b->have && now_us() - b->last_io_us >= (uint64_t)b->C.short_s * 1000000u) {
            SLOG_I("bridge", "UART%d: short connection idle, closing", b->n);
            drop_client(b, 0);
            next_try = 0;
            continue;
        }
        if (work)
            vTaskDelay(1);
        continue;
    lost:
        SLOG_W("bridge", "UART%d: connection to %s:%u lost", b->n, b->C.host, b->C.remote_port);
        if (b->ncli)
            drop_client(b, 0);
        b->have = false;
        next_try = now_us() + (uint64_t)(b->C.reconnect_s > 0 ? b->C.reconnect_s : 1) * 1000000u;
    }
}

// ---- UDP client / server ----------------------------------------------------------------------

typedef struct {
    bridge_t *b;
    struct sockaddr_in to;
} udp_reply_t;

static void reply_udp(void *ctx, const uint8_t *data, size_t len)
{
    udp_reply_t *u = ctx;
    lwip_sendto(u->b->udp, data, len, MSG_DONTWAIT, (const struct sockaddr *)&u->to, sizeof u->to);
}

static void udp(bridge_t *b)
{
    bool server = b->C.work == BR_UDP_SERVER, mcast = !server && b->C.extra == BR_X_MULTICAST;
    uint16_t port = mcast && !b->C.local_port ? b->C.remote_port : b->C.local_port;
    int one = 1;
    b->udp = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    lwip_setsockopt(b->udp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in me = {.sin_family = AF_INET, .sin_port = lwip_htons(port)};
    if (b->udp < 0 || lwip_bind(b->udp, (struct sockaddr *)&me, sizeof me) != 0) {
        SLOG_E("bridge", "UART%d: cannot bind UDP port %u", b->n, port);
        set_state(b, "ERROR");
        return;
    }
    net_nonblock(b->udp);
    uint32_t last_hb = now_ms();
    uint64_t next_resolve = 0;
    bool joined = false;
    for (;;) {
        bool work = false;
        // The default peer is resolved once the network is up (and again while DNS fails).
        if (!server && !b->peer_ok && wait_over(now_us(), next_resolve) && netmgr_online()) {
            b->peer_ok = net_resolve(b->C.host, b->C.remote_port, &b->peer);
            next_resolve = now_us() + 5000000u;
            uint8_t reg[48];
            size_t n = b->peer_ok && b->C.reg_send != BR_REG_PREFIX ? registration(b, reg, sizeof reg) : 0;
            if (n)
                lwip_sendto(b->udp, reg, n, MSG_DONTWAIT, (struct sockaddr *)&b->peer, sizeof b->peer);
        }
        if (mcast && b->peer_ok && !joined) {
            struct ip_mreq m = {.imr_multiaddr = b->peer.sin_addr, .imr_interface.s_addr = lwip_htonl(INADDR_ANY)};
            joined = lwip_setsockopt(b->udp, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m) == 0;
            if (joined)
                SLOG_I("bridge", "UART%d: joined multicast group %s", b->n, b->C.host);
        }
        b->online = b->peer_ok;
        set_state(b, b->peer_ok ? "CONNECTED" : server ? "LISTEN" : "CONNECTING");
        for (int k = 0; k < PASS_FRAMES && b->peer_ok && take_frame(b, !server); k++) {
            int r = lwip_sendto(b->udp, b->frame, b->frame_len, MSG_DONTWAIT, (struct sockaddr *)&b->peer, sizeof b->peer);
            work = true;
            if (r < 0 && net_classify(errno) == NET_AGAIN)
                break;  // kept for the next pass, however long (spec §5.6)
            b->have = false;
            if (r > 0) {
                svc_net_activity();
                last_hb = now_ms();  // heartbeats only fill silences
            }
        }
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(b->udp, &rd);
        struct timeval tv = {.tv_sec = 0, .tv_usec = work ? 0 : 10000};
        if (lwip_select(b->udp + 1, &rd, NULL, NULL, &tv) > 0) {
            uint8_t *buf = b->buf.io.rx;
            for (int k = 0; k < PASS_READS; k++) {
                udp_reply_t u = {.b = b};
                socklen_t fl = sizeof u.to;
                int r = lwip_recvfrom(b->udp, buf, NET_BUF, MSG_DONTWAIT, (struct sockaddr *)&u.to, &fl);
                if (r < 0)
                    break;
                work = true;  // an empty datagram is work too: the pass yields (Codex code review r1)
                if (r == 0)
                    continue;
                bool accept = true;
                if (server && !ipacl_allows(&b->C.acl, lwip_ntohl(u.to.sin_addr.s_addr))) {
                    accept = false;  // not in allowedClients: ignored, and it does not become the peer
                } else if (server) {
                    b->peer = u.to;  // replies go to whoever spoke last
                    b->peer_ok = true;
                } else if (!mcast) {
                    accept = b->peer_ok && u.to.sin_addr.s_addr == b->peer.sin_addr.s_addr &&
                             (b->C.any_port || u.to.sin_port == b->peer.sin_port);
                }
                if (accept)
                    from_network(b, NULL, buf, (size_t)r, reply_udp, &u);
            }
        }
        if (b->C.hb && b->peer_ok && now_ms() - last_hb >= (uint32_t)b->C.hb_interval_s * 1000u) {
            uint8_t hb[48];
            size_t n = heartbeat(b, hb, sizeof hb);
            if (n)
                lwip_sendto(b->udp, hb, n, MSG_DONTWAIT, (struct sockaddr *)&b->peer, sizeof b->peer);
            last_hb = now_ms();
        }
        if (work)
            vTaskDelay(1);
    }
}

// ---- HTTP client -------------------------------------------------------------------------------

// One request per serial frame; the response (the body only, or all of it) goes to the port.
static void http_request(bridge_t *b)
{
    uint8_t *rb = b->buf.http;
    set_state(b, "CONNECTING");
    uint32_t timeout_ms = (uint32_t)b->C.http_timeout_s * 1000u;
    conn_t c = {.fd = net_tcp_connect(b->C.http_host, b->C.http_port, b->C.http_local, timeout_ms)};
    if (c.fd >= 0 && b->C.tls) {  // HTTPS
        c.tls = tls_open(c.fd, SCOPE[b->n - 1], b->C.http_host, b->C.tls_verify, timeout_ms, NULL);
        if (!c.tls)
            conn_close(&c);
    }
    if (c.fd < 0) {
        SLOG_W("bridge", "UART%d: HTTP server %s:%u unreachable", b->n, b->C.http_host, b->C.http_port);
        set_state(b, "WAITING");
        return;
    }
    char head[512], tail[256];
    size_t hn = http_head(&b->C, b->frame_len, head, sizeof head), tn = http_tail(&b->C, tail, sizeof tail);
    bool ok = hn && conn_send_all(&c, head, hn, timeout_ms) && conn_send_all(&c, b->frame, b->frame_len, timeout_ms) &&
              (!tn || conn_send_all(&c, tail, tn, timeout_ms));
    size_t got = 0;
    uint64_t deadline = now_us() + (uint64_t)timeout_ms * 1000u;
    while (ok && got < sizeof b->buf.http && now_us() < deadline) {  // until the server closes, or the timeout
        int r = conn_recv(&c, rb + got, sizeof b->buf.http - got);
        if (r == NET_AGAIN) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    if (c.tls)
        headroom(b, 1, "HTTPS");
    conn_close(&c);
    if (!got) {
        SLOG_W("bridge", "UART%d: no HTTP response from %s", b->n, b->C.http_host);
        set_state(b, "WAITING");
        return;
    }
    svc_net_activity();
    size_t blen;
    const uint8_t *body = http_body(rb, got, b->C.http_strip, &blen);
    if (blen)
        serial_write(b->n, body, blen, 2000);
    set_state(b, "CONNECTED");
}

static void http_client(bridge_t *b)
{
    set_state(b, "WAITING");
    b->online = true;  // requests are made on demand
    for (;;) {
        size_t n = xMessageBufferReceive(b->mb, b->frame, sizeof b->frame, pdMS_TO_TICKS(1000));
        if (!n)
            continue;
        b->frame_len = n;
        if (netmgr_online())
            http_request(b);
    }
}

// ---- task, API -----------------------------------------------------------------------------------

static void bridge_task(void *arg)
{
    bridge_t *b = arg;
    switch (b->C.work) {
    case BR_UDP_CLIENT:
    case BR_UDP_SERVER:
        udp(b);
        break;
    case BR_TCP_CLIENT:
        tcp_client(b);
        break;
    case BR_TCP_SERVER:
        tcp_server(b);
        break;
    case BR_HTTP_CLIENT:
        http_client(b);
        break;
    }
    for (;;)
        vTaskDelay(portMAX_DELAY);  // a mode that could not start (state ERROR)
}

int bridge_start(int port, const cJSON *pcfg, const cJSON *sys)
{
    if (port < 1 || port > SERIAL_PORTS)
        return -1;
    bridge_t *b = &s_br[port - 1];
    memset(b, 0, sizeof *b);
    b->n = port;
    b->udp = -1;
    bridge_cfg_parse(pcfg, sys, &b->C);
    if (b->C.acl_bad)
        SLOG_W("bridge", "UART%d: allowedClients ignored: not a valid list", port);
    b->on = true;
    size_t size = b->C.keep_offline ? RB_OFFLINE : RB_ONLINE;
    b->mb = xMessageBufferCreateStatic(size, s_mb_store[port - 1], &b->mb_mem);
    serial_subscribe(port, on_serial, b);
    static const char *const MODES[] = {"UDP client", "TCP client", "UDP server", "TCP server", "HTTP client"};
    SLOG_I("bridge", "UART%d socket: %s%s%s%s", port, MODES[b->C.work % 5], b->C.modbus_gw ? ", Modbus TCP gateway" : "",
           b->C.short_conn ? ", short connection" : "", b->C.keep_offline ? ", bufferOffline" : "");
    char name[12];
    snprintf(name, sizeof name, "bridge%d", port);
    xTaskCreateStatic(bridge_task, name, STACK_WORDS, b, PRIO, s_bridge_stack[port - 1], &b->tcb);
    return 0;
}

void bridge_state(int port, char *out, size_t len)
{
    if (port < 1 || port > SERIAL_PORTS || !s_br[port - 1].on) {
        snprintf(out, len, "NOT STARTED");
        return;
    }
    taskENTER_CRITICAL();
    snprintf(out, len, "%s", s_br[port - 1].state);
    taskEXIT_CRITICAL();
}

uint32_t bridge_dropped(int port) { return port >= 1 && port <= SERIAL_PORTS ? s_br[port - 1].dropped : 0; }

void bridge_set_accept_reserve(int n) { s_reserve = n; }
