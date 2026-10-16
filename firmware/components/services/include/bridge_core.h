// The logic of a port's network bridge (spec §6.4, §5.6, Plan 08a), as pure C, host-tested
// (test_bridge_core): the settings of port<n> and system, the Modbus TCP reassembly, the HTTP client's
// request and response, the non-blocking send of a frame to one connection, and the accept gate.
// bridge.c drives it on the board, one instance per port; the ESP32's bridge.c is the baseline.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "ipacl.h"

enum { BR_UDP_CLIENT, BR_TCP_CLIENT, BR_UDP_SERVER, BR_TCP_SERVER, BR_HTTP_CLIENT };     // workMode
enum { BR_X_NONE, BR_X_MODBUS, BR_X_SHORT, BR_X_MODBUS_SHORT, BR_X_MULTICAST };          // socketMode
enum { BR_REG_ONCE, BR_REG_PREFIX, BR_REG_BOTH };                                        // registrationSend
#define BR_MAX_CLIENTS 16

typedef struct {
    int work, extra;
    char host[65];
    uint16_t remote_port, local_port, listen_port;
    bool any_port;  // udpAnyRemotePort
    int max_clients, limit_action;
    ipacl_t acl;    // allowedClients; empty: anyone
    bool acl_bad;   // the list did not parse (ignored, logged)
    char http_host[65], http_path[101], http_headers[181];
    uint16_t http_port, http_local;
    bool http_post, http_strip;
    int http_timeout_s;
    int reconnect_s, short_s;
    bool poll;
    int poll_timeout_ms;
    bool exception;
    bool hb;
    int hb_type, hb_interval_s;
    bool hb_hex;
    char hb_data[41];
    int reg, reg_send;
    bool reg_hex;
    char reg_data[41];
    int tls, tls_verify;
    bool keep_offline;  // system.bufferOffline
    // derived, as the ESP32's helpers
    bool modbus_gw;      // TCP client or server, socketMode Modbus (with or without short)
    bool modbus_client;  // the gateway as a Modbus TCP client (TCP client)
    bool short_conn;     // TCP client, socketMode short (with or without Modbus)
} bridge_cfg_t;

/** port<n> and system (either may be NULL: the defaults of an empty object). */
void bridge_cfg_parse(const cJSON *port, const cJSON *sys, bridge_cfg_t *out);

/** The sysmgr_payload content for registration 1..5 (custom, MAC, IMEI, SN, ICCID); -1 for none. */
int bridge_reg_content(int registration);

// ---- Modbus TCP reassembly, per connection ----------------------------------------------------

#define MBTCP_MAX 260  // MBAP (7) + PDU (253)
typedef struct {
    uint8_t buf[MBTCP_MAX];
    size_t len;
    uint64_t last_us;  // when the newest held byte came (acc_stamp, Plan 08c)
} mbtcp_acc_t;
typedef void (*mbtcp_fn)(void *ctx, const uint8_t *adu, size_t len);
/** Feeds bytes from the stream; calls fn with each whole ADU. Bytes that are not Modbus TCP resynchronise. */
void mbtcp_push(mbtcp_acc_t *a, const uint8_t *data, size_t len, mbtcp_fn fn, void *ctx);

// ---- HTTP client -------------------------------------------------------------------------------

/** The request head for one serial frame `len` bytes long: POST ... with the frame as the body follows,
 * or "GET <path>" (the frame, then http_tail, follow). Header lines normalised to CRLF. Its length. */
size_t http_head(const bridge_cfg_t *c, size_t len, char *out, size_t max);
/** For GET: " HTTP/1.0\r\nHost: ...\r\n<headers>\r\n" after the data in the URL. 0 for POST. */
size_t http_tail(const bridge_cfg_t *c, char *out, size_t max);
/** The part of a response that goes to the port: all of it, or the body after the blank line. */
const uint8_t *http_body(const uint8_t *resp, size_t len, bool strip, size_t *out_len);

// ---- Sending a frame to one connection (spec §5.6) ---------------------------------------------

enum { NET_AGAIN = -2, NET_FAIL = -1 };
/** errno of a failed send -> NET_AGAIN (EWOULDBLOCK, EAGAIN, ENOMEM, ENOBUFS) or NET_FAIL. */
int net_classify(int err);

#define TX_STALL_MS 10000u  // no byte taken for this long: the connection is lost (a deliberate exception to §5.6)
typedef struct {
    size_t off;          // bytes of the current frame already taken
    uint32_t progress_ms;  // when a byte was last taken (or the frame began)
    bool busy;           // a frame is in flight
} tx_conn_t;
enum { TX_DONE, TX_PENDING, TX_LOST };
/** Sends what may be sent; >0 bytes taken, NET_AGAIN or NET_FAIL. */
typedef int (*tx_send_fn)(void *ctx, const uint8_t *data, size_t len);
/** Starts or continues sending `frame` (the same frame until TX_DONE or TX_LOST). After NET_AGAIN the next
 * call passes the same remaining bytes (frame + off, len - off): mbedtls_ssl_write requires exactly that
 * of a TLS connection (Plan 08b), so keep it if this changes. */
int tx_progress(tx_conn_t *c, const uint8_t *frame, size_t len, tx_send_fn send, void *ctx, uint32_t now_ms);

/** Before anything else goes on a connection whose frame is in flight (a Modbus or polling reply), the frame is
 * finished: one byte stream must not interleave, and after NET_AGAIN mbedtls_ssl_write must get the same data
 * again (Codex 08b code review). wait(ctx) runs between tries; limit_ms bounds it. TX_DONE (nothing was in
 * flight, or it went through) or TX_LOST. */
int tx_finish(tx_conn_t *c, const uint8_t *frame, size_t len, tx_send_fn send, void *ctx, uint32_t (*now_ms)(void),
              void (*wait)(void *ctx), uint32_t limit_ms);

/** idleRestart: true when `limit_us` passed since the last activity. The activity is read before
 * `now_us`, so an update in between (last > now) is no expiry (Codex code review r1). */
bool idle_due(uint64_t now_us, uint64_t last_us, uint64_t limit_us);
/** A wait until `at_us` (0: no wait) is over: 64-bit time, no wrap in the gateway's life. */
static inline bool wait_over(uint64_t now_us, uint64_t at_us) { return now_us >= at_us; }

/** spec §5.6: a TCP server's listener accepts only while at least `reserve` netconns are free. */
static inline bool accept_allowed(int netconn_free, int reserve) { return netconn_free >= reserve; }

/** What the bridge's wait() returns (Plan 08b): select's count, plus the TLS clients whose bytes are already
 * inside mbedTLS (`pending`, not counted by select), so read_clients runs even when the socket is silent. */
static inline int wait_ready(int selected, int pending) { return (selected > 0 ? selected : 0) + pending; }

// ---- Edge remote access and reports (Plan 08c) --------------------------------------------------

#define ACC_STALE_US 200000u  // a partial request held this long without a byte was not one (the ESP32's)
/** After bytes were added to a: the time of the newest one. */
static inline void acc_stamp(mbtcp_acc_t *a, uint64_t now_us)
{
    if (a->len)
        a->last_us = now_us;
}
/** Held bytes with no newer byte for ACC_STALE_US: they go on as they are. */
static inline bool acc_stale(const mbtcp_acc_t *a, uint64_t now_us) { return a->len && now_us - a->last_us >= ACC_STALE_US; }

/** True: Edge answered the request. */
typedef bool (*frame_req_fn)(void *ctx, const uint8_t *req, size_t len);
typedef void (*frame_pass_fn)(void *ctx, const uint8_t *data, size_t len);
/** Edge remote access over a TCP stream: requests cut out by their MBAP length (mbap) or RTU function code
 * (mb_rtu_request_len) go to req; one req did not answer, and bytes that are not a request, go to pass whole. */
void frame_push(mbtcp_acc_t *a, const uint8_t *data, size_t len, bool mbap, frame_req_fn req, frame_pass_fn pass, void *ctx);

/** bridge_send's rule: the bridge runs, it is not a Modbus TCP client, and it has a peer or dials on demand
 * (an HTTP client; a TCP client with a short connection). */
bool report_accept(const bridge_cfg_t *c, bool running, bool online);
enum { RPT_NONE, RPT_OFFERED, RPT_SENDING };
/** The report slot is taken when offered and no frame is in flight, once. */
static inline bool report_take(int state, bool have) { return state == RPT_OFFERED && !have; }
