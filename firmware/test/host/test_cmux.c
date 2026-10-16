// cmux.c (Plan 10 T1, spec §12.1's CMUX row): frames, the opening with MSC, flow control, Test, the queues,
// and a PPP stream cut into UIH frames and put back together. make -C stm32/firmware/test/host
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmux.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

// ---- what the gateway's side delivers ---------------------------------------------------------------

static uint8_t s_got[3][65536];
static size_t s_got_n[3];
static int s_events[8], s_ev_dlc;

static void on_data(void *ctx, int dlc, const uint8_t *p, size_t n)
{
    (void)ctx;
    if (dlc >= 0 && dlc < 3 && s_got_n[dlc] + n <= sizeof s_got[0]) {
        memcpy(s_got[dlc] + s_got_n[dlc], p, n);
        s_got_n[dlc] += n;
    }
}
static void on_event(void *ctx, cmux_event_t ev, int dlc)
{
    (void)ctx;
    s_events[ev]++;
    s_ev_dlc = dlc;
}
static const cmux_io_t IO = {on_data, on_event, NULL};

static void reset_got(void)
{
    memset(s_got_n, 0, sizeof s_got_n);
    memset(s_events, 0, sizeof s_events);
}

// ---- frames the gateway sends, decoded independently -------------------------------------------------

typedef struct {
    int dlc, cr;
    uint8_t ctrl;  // P/F included
    uint8_t info[CMUX_FRAME_MAX];
    size_t n;
} frame_t;

static bool decode(const uint8_t *p, size_t len, frame_t *f)
{
    if (len < 6 || p[0] != 0xF9 || p[len - 1] != 0xF9 || !(p[1] & 1))
        return false;
    f->dlc = p[1] >> 2, f->cr = p[1] >> 1 & 1, f->ctrl = p[2];
    size_t hdr = 3, n = p[3] >> 1;
    if (p[3] & 1) {
        hdr = 4;
    } else {
        n |= (size_t)(p[4] >> 1) << 7;
        hdr = 5;
    }
    if (hdr + n + 2 != len || cmux_fcs(p + 1, hdr - 1) != p[hdr + n])
        return false;
    f->n = n;
    memcpy(f->info, p + hdr, n);
    return true;
}

static frame_t s_out[64];
static int s_nout;

/** Everything the gateway would send now, decoded into s_out. */
static int drain(cmux_t *m)
{
    uint8_t buf[CMUX_FRAME_MAX];
    size_t n;
    s_nout = 0;
    while ((n = cmux_tx_next(m, buf, sizeof buf)) && s_nout < 64)
        if (!decode(buf, n, &s_out[s_nout++]))
            printf("FAIL: an undecodable frame of %zu bytes\n", n), failures++;
    return s_nout;
}

/** The modem's frames: responses with C/R 1, its commands and data with C/R 0 (27.010 §5.2.1.2). */
static void modem(cmux_t *m, int dlc, uint8_t ctrl, bool cr, const uint8_t *info, size_t n, uint32_t now)
{
    uint8_t f[CMUX_FRAME_MAX];
    size_t len = cmux_frame(f, dlc, ctrl, cr, info, n);
    cmux_input(m, f, len, now);
}

static void modem_msg(cmux_t *m, uint8_t type, const uint8_t *val, size_t n, uint32_t now)
{
    uint8_t info[CMUX_N1];
    size_t o = 0;
    info[o++] = type;
    if (n < 128) {
        info[o++] = (uint8_t)(n << 1 | 1);
    } else {
        info[o++] = (uint8_t)((n & 0x7F) << 1);
        info[o++] = (uint8_t)(n >> 7 << 1 | 1);
    }
    if (n)
        memcpy(info + o, val, n);
    modem(m, 0, CMUX_UIH, false, info, o + n, now);
}

/** Through the opening: DLC0, DLC1, DLC2 with their UAs. */
static void open_all(cmux_t *m)
{
    cmux_init(m, &IO);
    cmux_start(m, 0);
    drain(m);
    modem(m, 0, CMUX_UA | 0x10, true, NULL, 0, 10);
    drain(m);
    modem(m, 1, CMUX_UA | 0x10, true, NULL, 0, 20);
    modem(m, 2, CMUX_UA | 0x10, true, NULL, 0, 20);
    drain(m);
}

static bool is_msg(const frame_t *f, uint8_t type)
{
    return f->dlc == 0 && (f->ctrl & ~0x10) == CMUX_UIH && f->n >= 2 && f->info[0] == type;
}

// ---- the tests --------------------------------------------------------------------------------------

static void test_fcs_and_frames(void)
{
    uint8_t f[CMUX_FRAME_MAX];
    // 27.010's well-known frames: SABM DLC0 from the initiator, and the UA in reply
    size_t n = cmux_frame(f, 0, CMUX_SABM, true, NULL, 0);
    static const uint8_t sabm[] = {0xF9, 0x03, 0x3F, 0x01, 0x1C, 0xF9};
    CHECK(n == 6 && !memcmp(f, sabm, 6), "SABM DLC0: F9 03 3F 01 1C F9");
    n = cmux_frame(f, 0, CMUX_UA, true, NULL, 0);
    static const uint8_t ua[] = {0xF9, 0x03, 0x73, 0x01, 0xD7, 0xF9};
    CHECK(n == 6 && !memcmp(f, ua, 6), "UA DLC0: F9 03 73 01 D7 F9");
    CHECK(cmux_frame(f, 1, CMUX_UIH, true, NULL, CMUX_N1 + 1) == 0, "over N1: no frame");
    // lengths across the 1- and 2-octet field
    const size_t sizes[] = {0, 1, 127, 128, 2048};
    for (size_t i = 0; i < 5; i++) {
        uint8_t info[CMUX_N1];
        for (size_t k = 0; k < sizes[i]; k++)
            info[k] = (uint8_t)(k * 7 + 0xF9);  // 0xF9 inside the information field too
        n = cmux_frame(f, 1, CMUX_UIH, true, info, sizes[i]);
        frame_t d;
        CHECK(n == sizes[i] + (sizes[i] < 128 ? 6 : 7) && decode(f, n, &d) && d.n == sizes[i] && !memcmp(d.info, info, sizes[i]),
              "UIH of %zu bytes: encoded and decoded", sizes[i]);
        // and through a gateway that is running, cut at every byte
        cmux_t m;
        open_all(&m);
        int bad = 0;
        for (size_t cut = 0; cut <= n; cut++) {
            reset_got();
            cmux_input(&m, f, cut, 100);
            cmux_input(&m, f + cut, n - cut, 100);
            bad += s_got_n[1] != sizes[i] || memcmp(s_got[1], info, sizes[i]);
        }
        CHECK(bad == 0, "UIH of %zu bytes cut at every byte: delivered whole (%d bad)", sizes[i], bad);
    }
}

static void test_resync(void)
{
    cmux_t m;
    open_all(&m);
    reset_got();
    uint8_t good[64], bad[64], s[256];
    size_t g = cmux_frame(good, 1, CMUX_UIH, false, (const uint8_t *)"hello", 5);
    size_t b = cmux_frame(bad, 1, CMUX_UIH, false, (const uint8_t *)"broken", 6);
    bad[b - 2] ^= 0x55;  // a wrong FCS
    size_t o = 0;
    memcpy(s + o, "\x12\x34\x56\x78\x9a", 5), o += 5;  // garbage: EA clear, never an address
    memcpy(s + o, bad, b), o += b;
    memcpy(s + o, good, g), o += g;
    memcpy(s + o, good + 1, g - 1), o += g - 1;  // a lost opening flag: the previous closing one serves
    cmux_input(&m, s, o, 100);
    CHECK(s_got_n[1] == 10 && !memcmp(s_got[1], "hellohello", 10), "after garbage and a bad FCS, both good frames (%zu)", s_got_n[1]);
    CHECK(cmux_counters(&m)->bad_fcs == 1, "the bad FCS counted (%u)", cmux_counters(&m)->bad_fcs);
    // a length over N1: dropped, then the next frame
    uint8_t big[8] = {0xF9, 0x05, 0xEF, (uint8_t)((2049 & 0x7F) << 1), (uint8_t)(2049 >> 7 << 1 | 1)};
    reset_got();
    cmux_input(&m, big, 5, 100);
    cmux_input(&m, good, g, 100);
    CHECK(cmux_counters(&m)->too_long == 1 && s_got_n[1] == 5, "over N1 dropped, the next frame delivered");
}

static void test_opening(void)
{
    cmux_t m;
    cmux_init(&m, &IO);
    reset_got();
    cmux_start(&m, 0);
    CHECK(cmux_state(&m) == CMUX_OPEN_CTRL && drain(&m) == 1 && s_out[0].dlc == 0 && s_out[0].ctrl == (CMUX_SABM | 0x10),
          "start: SABM on DLC0");
    // data queued before the DLCs are open waits
    CHECK(cmux_send(&m, 1, (const uint8_t *)"ATD*99#\r", 8) == 8 && cmux_send(&m, 2, (const uint8_t *)"AT\r", 3) == 3, "queued early");
    modem(&m, 0, CMUX_UA | 0x10, true, NULL, 0, 10);
    CHECK(cmux_state(&m) == CMUX_OPEN_DLC && drain(&m) == 2 && s_out[0].dlc == 1 && s_out[1].dlc == 2 &&
              s_out[0].ctrl == (CMUX_SABM | 0x10),
          "UA on DLC0: SABM on DLC1 and DLC2, and no data yet");
    modem(&m, 1, CMUX_UA | 0x10, true, NULL, 0, 20);
    CHECK(cmux_state(&m) == CMUX_OPEN_DLC && !s_events[CMUX_EV_RUNNING], "one UA: not running yet");
    modem(&m, 2, CMUX_UA | 0x10, true, NULL, 0, 20);
    CHECK(cmux_state(&m) == CMUX_RUNNING && s_events[CMUX_EV_RUNNING] == 1, "both UAs: running");
    int n = drain(&m), msc1 = -1, msc2 = -1, data1 = -1, data2 = -1;
    for (int i = 0; i < n; i++) {
        if (is_msg(&s_out[i], CMUX_MSG_MSC | 2) && s_out[i].info[2] == (1 << 2 | 3)) {
            msc1 = i;
            CHECK(s_out[i].info[3] == 0x8D, "MSC for DLC1: RTC, RTR, DV (%02x)", s_out[i].info[3]);
        }
        if (is_msg(&s_out[i], CMUX_MSG_MSC | 2) && s_out[i].info[2] == (2 << 2 | 3))
            msc2 = i;
        if (s_out[i].dlc == 1 && (s_out[i].ctrl & ~0x10) == CMUX_UIH)
            data1 = i;
        if (s_out[i].dlc == 2 && (s_out[i].ctrl & ~0x10) == CMUX_UIH)
            data2 = i;
    }
    CHECK(msc1 >= 0 && msc2 >= 0 && data1 > msc1 && data2 > msc2, "each DLC's MSC before its data (%d %d %d %d)", msc1, data1, msc2, data2);
    CHECK(data1 >= 0 && s_out[data1].n == 8 && !memcmp(s_out[data1].info, "ATD*99#\r", 8), "then the data queued early");
}

static void test_retries(void)
{
    cmux_t m;
    cmux_init(&m, &IO);
    reset_got();
    cmux_start(&m, 0);
    drain(&m);
    int sabm = 0;
    for (uint32_t t = 0; t <= 2000; t += 10) {
        cmux_tick(&m, t);
        sabm += drain(&m);
    }
    CHECK(sabm == CMUX_N2 && cmux_state(&m) == CMUX_FAILED && s_events[CMUX_EV_FAILED] == 1, "no UA: %d resends, then failed", sabm);
    // a resend that gets its UA
    cmux_init(&m, &IO);
    reset_got();
    cmux_start(&m, 0);
    drain(&m);
    cmux_tick(&m, 150);
    CHECK(drain(&m) == 0, "not before T1");
    cmux_tick(&m, 210);
    CHECK(drain(&m) == 1, "resent at T1");
    modem(&m, 0, CMUX_UA | 0x10, true, NULL, 0, 250);
    CHECK(cmux_state(&m) == CMUX_OPEN_DLC, "then opened");
}

static void test_close_events(void)
{
    cmux_t m;
    open_all(&m);
    reset_got();
    modem(&m, 2, CMUX_DM | 0x10, true, NULL, 0, 100);
    CHECK(s_events[CMUX_EV_DLC_CLOSED] == 1 && s_ev_dlc == 2, "DM on DLC2: closed");
    open_all(&m);
    reset_got();
    modem(&m, 1, CMUX_DISC | 0x10, false, NULL, 0, 100);
    CHECK(s_events[CMUX_EV_DLC_CLOSED] == 1 && s_ev_dlc == 1 && drain(&m) == 1 && s_out[0].dlc == 1 &&
              (s_out[0].ctrl & ~0x10) == CMUX_UA,
          "DISC on DLC1: closed, and answered with UA");
    open_all(&m);
    cmux_close(&m);
    CHECK(drain(&m) == 1 && is_msg(&s_out[0], CMUX_MSG_CLD | 2), "close: CLD on DLC0");
}

static void test_flow_control(void)
{
    cmux_t m;
    open_all(&m);
    modem_msg(&m, CMUX_MSG_FCOFF | 2, NULL, 0, 100);
    cmux_send(&m, 1, (const uint8_t *)"ppp-1", 5);
    cmux_send(&m, 2, (const uint8_t *)"AT+CSQ\r", 7);
    int n = drain(&m);
    CHECK(n == 1 && is_msg(&s_out[0], CMUX_MSG_FCOFF), "FCoff: answered, and no data frame (%d frames)", n);
    // DLC0 still works during FCoff: a Test of 2000 bytes gets its whole pattern back
    static uint8_t pat[2000];
    for (int i = 0; i < 2000; i++)
        pat[i] = (uint8_t)(i * 13);
    modem_msg(&m, CMUX_MSG_TEST | 2, pat, sizeof pat, 110);
    n = drain(&m);
    CHECK(n == 1 && is_msg(&s_out[0], CMUX_MSG_TEST) && s_out[0].n == 2003 && !memcmp(s_out[0].info + 3, pat, 2000),
          "a Test of 2000 bytes during FCoff: the whole pattern back (%d frames, %zu bytes)", n, n ? s_out[0].n : 0);
    modem_msg(&m, CMUX_MSG_FCON | 2, NULL, 0, 120);
    n = drain(&m);
    CHECK(n == 3 && is_msg(&s_out[0], CMUX_MSG_FCON) && s_out[1].dlc == 1 && s_out[1].n == 5 && s_out[2].dlc == 2,
          "FCon: answered, then DLC1 and DLC2 resume (%d frames)", n);
    // MSC with FC on DLC2 stops DLC2 alone
    uint8_t stop2[] = {2 << 2 | 3, 0x8D | 0x02}, go2[] = {2 << 2 | 3, 0x8D};
    modem_msg(&m, CMUX_MSG_MSC | 2, stop2, 2, 130);
    cmux_send(&m, 1, (const uint8_t *)"a", 1);
    cmux_send(&m, 2, (const uint8_t *)"b", 1);
    n = drain(&m);
    CHECK(n == 2 && is_msg(&s_out[0], CMUX_MSG_MSC) && s_out[0].info[2] == stop2[0] && s_out[1].dlc == 1,
          "MSC FC on DLC2: answered, DLC1 still goes (%d frames)", n);
    modem_msg(&m, CMUX_MSG_MSC | 2, go2, 2, 140);
    n = drain(&m);
    CHECK(n == 2 && is_msg(&s_out[0], CMUX_MSG_MSC) && s_out[1].dlc == 2 && s_out[1].info[0] == 'b', "MSC clears FC: DLC2 resumes");
    // an unsupported command: NSC with its type
    modem_msg(&m, 0x83 | 2, NULL, 0, 150);  // PN, not supported here
    n = drain(&m);
    CHECK(n == 1 && is_msg(&s_out[0], CMUX_MSG_NSC) && s_out[0].info[2] == (0x83 | 2) && cmux_counters(&m)->nsc == 1,
          "an unsupported command: NSC");
}

// ---- PPP over DLC1 -------------------------------------------------------------------------------------

static uint16_t fcs16(uint16_t f, const uint8_t *p, size_t n)
{
    while (n--) {
        f ^= *p++;
        for (int k = 0; k < 8; k++)
            f = f & 1 ? (uint16_t)(f >> 1 ^ 0x8408) : (uint16_t)(f >> 1);
    }
    return f;
}

/** An HDLC-like PPP frame (RFC 1662): flag, escaped payload and FCS, flag. Its length in out. */
static size_t ppp_frame(const uint8_t *p, size_t n, uint8_t *out)
{
    uint16_t f = (uint16_t)~fcs16(0xFFFF, p, n);
    uint8_t raw[1100];
    memcpy(raw, p, n);
    raw[n] = (uint8_t)f, raw[n + 1] = (uint8_t)(f >> 8);
    size_t o = 0;
    out[o++] = 0x7E;
    for (size_t i = 0; i < n + 2; i++) {
        if (raw[i] == 0x7E || raw[i] == 0x7D || raw[i] < 0x20)
            out[o++] = 0x7D, out[o++] = raw[i] ^ 0x20;
        else
            out[o++] = raw[i];
    }
    out[o++] = 0x7E;
    return o;
}

/** The modem's PPP: the good frames in a received stream (a bad FCS or a cut frame is dropped). */
static int ppp_good(const uint8_t *s, size_t n, uint8_t first_bytes[64])
{
    int good = 0;
    uint8_t f[2200];
    size_t fl = 0;
    bool esc = false;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == 0x7E) {
            if (fl >= 3 && fcs16(0xFFFF, f, fl) == 0xF0B8 && good < 64)
                first_bytes[good++] = f[0];
            fl = 0, esc = false;
        } else if (s[i] == 0x7D) {
            esc = true;
        } else if (fl < sizeof f) {
            f[fl++] = esc ? s[i] ^ 0x20 : s[i];
            esc = false;
        }
    }
    return good;
}

static void test_ppp_stream(void)
{
    cmux_t gw, rx;
    open_all(&gw);
    cmux_init(&rx, &IO);  // a second parser plays the modem's receiver
    reset_got();
    uint8_t pkt[1000], fr[2100], buf[CMUX_FRAME_MAX];
    // escape-heavy packets (almost double after escaping) cut into frames of at most N1, then back together
    size_t sent = 0;
    uint8_t want[8];
    for (int k = 0; k < 6; k++) {
        for (int i = 0; i < 1000; i++)
            pkt[i] = i % 2 ? 0x7E : 0x7D;
        pkt[0] = (uint8_t)k;
        want[k] = (uint8_t)k;
        size_t fl = ppp_frame(pkt, sizeof pkt, fr);
        CHECK(cmux_send(&gw, 1, fr, fl) == fl, "frame %d queued (%zu bytes)", k, fl);
        sent += fl;
        size_t n;
        while ((n = cmux_tx_next(&gw, buf, sizeof buf))) {
            frame_t d;
            CHECK(decode(buf, n, &d) && d.n <= CMUX_N1, "each UIH at most N1 (%zu)", d.n);
            cmux_input(&rx, buf, n, 100);
        }
    }
    uint8_t firsts[64];
    CHECK(s_got_n[1] == sent && ppp_good(s_got[1], s_got_n[1], firsts) == 6 && !memcmp(firsts, want, 6),
          "an escape-heavy PPP stream through DLC1, byte-exact (%zu of %zu)", s_got_n[1], sent);

    // the queue full at the second call of a frame: 0, out_drop, the cut frame dropped by its FCS, the next whole
    open_all(&gw);
    cmux_init(&rx, &IO);
    reset_got();
    for (int i = 0; i < 1000; i++)
        pkt[i] = (uint8_t)(i * 3);
    pkt[0] = 'A';
    size_t fl = ppp_frame(pkt, 1000, fr);  // about 1100 bytes
    // fill DLC1's queue so that a first piece fits and the second does not
    static uint8_t filler[4096];
    size_t room = CMUX_DLC1_Q - 600;
    filler[0] = 0x7E;
    for (size_t i = 1; i < room; i++)
        filler[i] = 'f';
    CHECK(cmux_send(&gw, 1, filler, room) == room, "filler queued");
    uint32_t drop0 = cmux_counters(&gw)->out_drop;
    size_t a = cmux_send(&gw, 1, fr, 500), b = cmux_send(&gw, 1, fr + 500, fl - 500);
    CHECK(a == 500 && b == 0 && cmux_counters(&gw)->out_drop == drop0 + 1, "the second call of a frame finds no room: 0 (%zu %zu)", a, b);
    size_t n;
    while ((n = cmux_tx_next(&gw, buf, sizeof buf)))
        cmux_input(&rx, buf, n, 100);
    pkt[0] = 'B';
    fl = ppp_frame(pkt, 1000, fr);
    CHECK(cmux_send(&gw, 1, fr, fl) == fl, "the next frame queued once there is room");
    while ((n = cmux_tx_next(&gw, buf, sizeof buf)))
        cmux_input(&rx, buf, n, 100);
    int good = ppp_good(s_got[1], s_got_n[1], firsts);
    CHECK(good == 1 && firsts[0] == 'B', "the cut frame dropped by its FCS, the next one whole (%d good)", good);
}

static void test_queues(void)
{
    cmux_t m;
    open_all(&m);
    static uint8_t big[CMUX_DLC1_Q + 1];
    CHECK(cmux_room(&m, 1) == CMUX_DLC1_Q && cmux_room(&m, 0) == 0, "room: DLC1 empty, none on DLC0");
    CHECK(cmux_send(&m, 1, big, CMUX_DLC1_Q) == CMUX_DLC1_Q && cmux_send(&m, 1, big, 1) == 0 && cmux_room(&m, 1) == 0,
          "DLC1: 4 KB, then no room");
    CHECK(cmux_send(&m, 2, big, CMUX_DLC2_Q + 1) == 0 && cmux_send(&m, 2, big, CMUX_DLC2_Q) == CMUX_DLC2_Q, "DLC2: 512 B, all or nothing");
    cmux_input(&m, (const uint8_t *)"", 0, 5000);
    CHECK(cmux_last_rx_ms(&m) == 20, "the last frame's time, not the last call's (%u)", (unsigned)cmux_last_rx_ms(&m));
}

int main(void)
{
    test_fcs_and_frames();
    test_resync();
    test_opening();
    test_retries();
    test_close_events();
    test_flow_control();
    test_ppp_stream();
    test_queues();
    printf("test_cmux: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
