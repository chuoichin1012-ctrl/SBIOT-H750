// The web rules that need no network (spec §10.1), host-tested (test_web_core): the login token and
// its checks, the body policy of each endpoint, the stall rule, and the PEM check of /api/cert.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEB_BODY_MAX 10240u      // every endpoint that is not streamed (§9.3)
#define WEB_CERT_MAX 12288u      // a PEM file
#define WEB_EDGE_MAX 96000u      // the edgeDevices body (§9.4, Plan 09d)
#define WEB_STALL_MS (2u * 15000u)  // two 15 s waits without a byte: 408

typedef enum { WEB_EP_PLAIN, WEB_EP_OTA, WEB_EP_CERT, WEB_EP_EDGE } web_ep_t;

/** base64("user:password") into out, NUL-terminated; its length, or 0 when it does not fit. */
size_t web_token_make(const char *user, const char *pass, char *out, size_t cap);
/** got (got_len bytes) equals want, in time that depends on want's length only. */
bool web_token_eq(const char *want, const char *got, size_t got_len);
/** An Authorization header value: "Basic <token>" with the right token. */
bool web_basic_ok(const char *want, const char *hdr, size_t len);
/** %XX and '+' decoding (the browser's ?auth=); the decoded length, or SIZE_MAX when cap is too small. */
size_t web_url_decode(const char *in, size_t len, char *out, size_t cap);
/** "/api/config/<name>[?...]" -> name (NUL-terminated); false when there is none or it does not fit. */
bool web_config_name(const char *uri, size_t len, char *out, size_t cap);
/** Which body rule a request follows (POST only; any GET is PLAIN). */
web_ep_t web_endpoint(bool post, const char *uri, size_t len);
/**
 * 0 when the body may be read, else the HTTP status: 411 without a Content-Length or when chunked
 * (every endpoint: the UI never sends chunked bodies), 413 above the endpoint's limit.
 * slot_max is the OTA staging slot.
 */
int web_body_check(web_ep_t ep, bool has_length, bool chunked, size_t length, size_t slot_max);
/** True when a streamed body has sent nothing for WEB_STALL_MS. */
bool web_stalled(uint64_t last_byte_ms, uint64_t now_ms);

/** A connection's answer waiting to be sent (Plan 10c task 9, Codex review Important 9). */
typedef struct {
    uint64_t since;  // when the queue filled, or the last write that sent bytes
    bool on;         // the queue holds bytes
} web_send_t;
/** At each poll (wrote false) and each write (wrote: bytes went out): true when `queued` bytes have waited
 *  WEB_STALL_MS without a write, a client that stopped reading. The clock starts when the queue fills,
 *  restarts at each write, stops when it is empty. */
bool web_send_poll(web_send_t *t, size_t queued, bool wrote, uint64_t now_ms);

/** The WebSocket console's UART monitor: one frame as a text line, "TX " or "RX ", its bytes in hex, "\n" (the user,
 * 2026-10-17). Room for the largest frame (SERIAL_FRAME_MAX, 1460) and the NUL. Cut on a whole byte when `cap` is
 * smaller; the length, without the NUL. */
#define WEB_MON_LINE_MAX (3 + 3 * 1460 + 1)
size_t web_mon_line(char *out, size_t cap, bool tx, const uint8_t *data, size_t len);
/** fn for each piece of at most `max` bytes, in order (a write longer than a frame shown whole, as several lines); an
 * empty record (len 0: a TX that did not go out) once. */
/** The console's drain, for one client with `queued` bytes not yet sent: true to wait for it (its queue is at `high`
 * or more and it read within WEB_MON_STUCK_MS), so a long write reaches it whole. *full_since: when its queue
 * filled, 0 while it has room. A client stuck longer no longer holds the others: it is skipped, its frames dropped. */
#define WEB_MON_STUCK_MS 2000u
bool web_mon_wait(size_t queued, size_t high, uint64_t *full_since, uint64_t now_ms);
void web_mon_each(const uint8_t *data, size_t len, size_t max, void (*fn)(const uint8_t *d, size_t n, void *ctx), void *ctx);
/**
 * Plan 06b's check of an uploaded PEM, until mbedTLS parses it (Plan 08): one BEGIN/END pair of the
 * kind's label (CERTIFICATE for ca and cert; PRIVATE KEY, RSA PRIVATE KEY or EC PRIVATE KEY for key),
 * base64 lines between, nothing but whitespace around. 0, or -1.
 */
int web_pem_check(const char *kind, const char *pem, size_t len);
