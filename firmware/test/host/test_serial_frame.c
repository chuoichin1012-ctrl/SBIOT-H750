// Host tests for the serial frame logic (spec §6.5, Plan 07a): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "serial_frame.h"

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

// ---- timing ---------------------------------------------------------------------------------------

static void test_timing(void)
{
    sf_line_t l8n1 = {9600, 8, 0, 1}, l7n1 = {9600, 7, 0, 1}, l8e2 = {9600, 8, 2, 2}, l7o2 = {600, 7, 1, 2};
    CHECK(sf_char_bits(&l8n1) == 10 && sf_char_bits(&l7n1) == 9 && sf_char_bits(&l8e2) == 12 && sf_char_bits(&l7o2) == 11,
          "character bits 10/9/12/11");
    CHECK(sf_char_us(&l8n1) == 1042, "9600 8N1: 1042 us a character (%u)", (unsigned)sf_char_us(&l8n1));
    sf_line_t r1200 = {1200, 8, 2, 1}, r19200 = {19200, 8, 2, 1}, r115 = {115200, 8, 2, 1};
    CHECK(sf_t15_us(&r1200) == 13750 && sf_t35_us(&r1200) == 32084, "1200 8E1: t1.5 13 750, t3.5 32 084 (%u %u)",
          (unsigned)sf_t15_us(&r1200), (unsigned)sf_t35_us(&r1200));
    CHECK(sf_t15_us(&r19200) == 860 && sf_t35_us(&r19200) == 2006, "19 200 8E1: computed (%u %u)", (unsigned)sf_t15_us(&r19200),
          (unsigned)sf_t35_us(&r19200));
    CHECK(sf_t15_us(&r115) == 750 && sf_t35_us(&r115) == 1750, "above 19 200: fixed 750 / 1750");
    sf_line_t f = {230400, 8, 0, 1};
    CHECK(sf_gap_ms(&f, 0) == 2, "3.5 characters at 230 400: the 2 ms floor");
    CHECK(sf_gap_ms(&l8n1, 0) == 4, "3.5 characters at 9600: 4 ms (rounded up)");
    CHECK(sf_gap_ms(&l8n1, 50) == 50, "packetInterval wins");
    CHECK(sf_rto_bits(&r1200, 13750) == 17, "t1.5 at 1200 as RTO bits: 17 (%u)", (unsigned)sf_rto_bits(&r1200, 13750));
    CHECK(sf_rto_bits(&f, 2000) == 461, "2 ms at 230 400: 461 bits (%u)", (unsigned)sf_rto_bits(&f, 2000));
    static const uint32_t bauds[] = {600, 1200, 9600, 19200, 115200, 230400};
    for (size_t i = 0; i < sizeof bauds / sizeof bauds[0]; i++) {
        uint32_t presc, brr;
        int32_t err;
        bool ok = sf_brr(120000000u, bauds[i], &presc, &brr, &err);
        CHECK(ok && brr >= 16 && brr <= 0xFFFF && err > -20000 && err < 20000, "BRR at %u: presc %u brr %u err %d ppm",
              (unsigned)bauds[i], (unsigned)presc, (unsigned)brr, (int)err);
    }
    uint32_t presc, brr;
    int32_t err;
    sf_brr(120000000u, 600, &presc, &brr, &err);
    CHECK(presc > 0, "600 baud needs the prescaler (%u)", (unsigned)presc);
    sf_brr(120000000u, 230400, &presc, &brr, &err);
    CHECK(presc == 0 && brr == 521, "230 400: no prescaler, BRR 521 (%u %u)", (unsigned)presc, (unsigned)brr);
}

// ---- ring -----------------------------------------------------------------------------------------

static void test_ring(void)
{
    CHECK(sf_ring_wr(0, 2048, 2048, 0) == 0, "empty");
    CHECK(sf_ring_wr(0, 2000, 2048, 0) == 48, "48 bytes in");
    CHECK(sf_ring_wr(1, 2000, 2048, 2040) == 2096, "after a wrap: 2096");
    CHECK(sf_ring_wr(0, 2040, 2048, 2040) == 2056, "TC not yet counted: one lap further (%u)", (unsigned)sf_ring_wr(0, 2040, 2048, 2040));
    CHECK(sf_ring_pending(100, sf_ring_wr(1, 0, 2048, 100)) == 3996, "exactly full is more than size: overrun visible");
    CHECK(sf_ring_pending(0, sf_ring_wr(2, 1000, 2048, 0)) > 2048, "two laps behind: overrun");
}

// ---- RX -------------------------------------------------------------------------------------------

static uint8_t g_last[SF_FRAME_MAX + 8];
static size_t g_last_len;
static int g_frames;

static void emit(void *ctx, const uint8_t *f, size_t n)
{
    (void)ctx;
    memcpy(g_last, f, n);
    g_last_len = n;
    g_frames++;
}

static void reset(void)
{
    g_frames = 0;
    g_last_len = 0;
}

static void test_transparent(void)
{
    sf_rx_t r;
    uint8_t buf[SF_FRAME_MAX * 2];
    for (unsigned i = 0; i < sizeof buf; i++)
        buf[i] = (uint8_t)i;
    reset();
    sf_rx_init(&r, SF_TRANSPARENT, SF_FRAME_MAX, false, emit, NULL);
    sf_rx_bytes(&r, buf, 10);
    CHECK(g_frames == 0, "no frame before the gap");
    sf_rx_end(&r);
    CHECK(g_frames == 1 && g_last_len == 10 && !memcmp(g_last, buf, 10), "RTO ends the frame");
    sf_rx_end(&r);
    CHECK(g_frames == 1, "RTO with no bytes: no frame");
    sf_rx_bytes(&r, buf, SF_FRAME_MAX + 5);
    CHECK(g_frames == 2 && g_last_len == SF_FRAME_MAX, "cut at 1460");
    sf_rx_end(&r);
    CHECK(g_frames == 3 && g_last_len == 5 && g_last[0] == (uint8_t)SF_FRAME_MAX, "the rest at the gap");
    reset();
    sf_rx_init(&r, SF_TRANSPARENT, 1, false, emit, NULL);
    sf_rx_bytes(&r, buf, 3);
    CHECK(g_frames == 3 && g_last_len == 1 && g_last[0] == 2, "packetLength 1: a frame per byte");
}

static void test_rtu(void)
{
    sf_rx_t r;
    const uint8_t ok[] = {1, 3, 2, 0, 7, 0xF9, 0x86};
    reset();
    sf_rx_init(&r, SF_RTU, SF_FRAME_MAX, false, emit, NULL);
    sf_rx_bytes(&r, ok, sizeof ok);
    sf_rx_end(&r);  // a clean expiry
    CHECK(g_frames == 1 && g_last_len == sizeof ok, "a clean RTU frame");

    // A byte inside t1.5..t3.5: violation, the frame dropped; the tail is not a frame.
    sf_rx_bytes(&r, ok, 4);
    CHECK(sf_rtu_violation(100, 101, false), "the position moved: a violation");
    CHECK(sf_rtu_violation(100, 100, true), "a character still arriving (BUSY): a violation");
    CHECK(!sf_rtu_violation(100, 100, false), "nothing: clean");
    sf_rx_bytes(&r, ok + 4, 1);
    sf_rx_violation(&r);
    CHECK(g_frames == 1 && r.violations == 1 && r.resync, "the frame dropped, resync");
    sf_rx_bytes(&r, ok + 5, 2);  // the tail
    sf_rx_end(&r);               // this end only proves t3.5 of silence: resync is over, nothing emitted
    CHECK(g_frames == 1 && !r.resync, "the tail is not a frame; t3.5 of silence ends the resync");
    sf_rx_bytes(&r, ok, sizeof ok);
    sf_rx_end(&r);
    CHECK(g_frames == 2 && g_last_len == sizeof ok, "the next clean frame is received");

    // A violation at the tail end, then more bytes before silence: still nothing until a clean end.
    sf_rx_bytes(&r, ok, 3);
    sf_rx_violation(&r);
    sf_rx_bytes(&r, ok, 3);
    sf_rx_violation(&r);
    sf_rx_bytes(&r, ok, 2);
    sf_rx_end(&r);
    CHECK(g_frames == 2 && r.violations == 3, "violations in a row: nothing emitted");
    // 1000 timeouts in a row, frames still received
    reset();
    for (int i = 0; i < 1000; i++) {
        sf_rx_bytes(&r, ok, sizeof ok);
        sf_rx_end(&r);
    }
    CHECK(g_frames == 1000, "1000 RTOs in a row: 1000 frames (%d)", g_frames);
}

static void test_errors(void)
{
    sf_rx_t r;
    const uint8_t a[] = {1, 2, 3, 4};
    reset();
    sf_rx_init(&r, SF_TRANSPARENT, SF_FRAME_MAX, false, emit, NULL);
    sf_rx_bytes(&r, a, 2);
    sf_rx_line_error(&r);
    sf_rx_bytes(&r, a + 2, 2);
    sf_rx_end(&r);
    CHECK(g_frames == 1 && g_last_len == 2 && g_last[0] == 3 && r.line_errors == 1 && r.dropped == 1,
          "a parity error mid-frame: what came before is dropped, what follows is a frame");
    sf_rx_bytes(&r, a, 4);
    sf_rx_overrun(&r);
    sf_rx_end(&r);
    CHECK(g_frames == 1 && r.overruns == 1 && r.dropped == 2, "an overrun drops the frame being built");
    sf_rx_bytes(&r, a, 2);
    sf_rx_mode(&r, SF_RTU);
    sf_rx_end(&r);
    CHECK(g_frames == 1 && r.mode == SF_RTU, "a mode switch drops the half frame");
}

static void test_xonxoff(void)
{
    sf_rx_t r;
    const uint8_t d[] = {'a', SF_XOFF, 'b', SF_XON, 'c'};
    reset();
    sf_rx_init(&r, SF_TRANSPARENT, SF_FRAME_MAX, true, emit, NULL);
    sf_rx_bytes(&r, d, 2);
    CHECK(r.tx_paused, "XOFF pauses our TX");
    sf_rx_bytes(&r, d + 2, 3);
    sf_rx_end(&r);
    CHECK(!r.tx_paused && g_frames == 1 && g_last_len == 3 && !memcmp(g_last, "abc", 3), "XON resumes; both taken out of the data");
    sf_rx_init(&r, SF_TRANSPARENT, SF_FRAME_MAX, false, emit, NULL);
    reset();
    sf_rx_bytes(&r, d, 5);
    sf_rx_end(&r);
    CHECK(!r.tx_paused && g_last_len == 5, "flow control off: XON/XOFF are data");
}

// ---- TX -------------------------------------------------------------------------------------------

static void test_tx(void)
{
    sf_tx_t t;
    sf_tx_step_t s = sf_tx_begin(&t, 5000, 2048, 1000000, false);
    CHECK(s.action == SF_TX_START && s.off == 0 && s.n == 2048, "the first chunk");
    s = sf_tx_xoff(&t);
    CHECK(s.action == SF_TX_PAUSE, "XOFF in a running chunk: pause the DMA");
    s = sf_tx_xon(&t);
    CHECK(s.action == SF_TX_RESUME, "XON: resume it");
    s = sf_tx_chunk_done(&t);
    CHECK(s.action == SF_TX_START && s.off == 2048 && s.n == 2048, "the second chunk");
    s = sf_tx_chunk_done(&t);
    CHECK(s.action == SF_TX_START && s.off == 4096 && s.n == 904, "the last, short");
    s = sf_tx_usart_tc(&t);
    CHECK(s.action == SF_TX_NONE, "a USART TC before the last chunk is done does not count");
    s = sf_tx_chunk_done(&t);
    CHECK(s.action == SF_TX_NONE && t.draining, "all chunks done: wait for the last bit");
    s = sf_tx_usart_tc(&t);
    CHECK(s.action == SF_TX_DONE && t.result == 0, "done");

    // XOFF between chunks: the next one waits for XON.
    sf_tx_begin(&t, 3000, 2048, 1000000, false);
    sf_tx_xoff(&t);
    s = sf_tx_chunk_done(&t);
    CHECK(s.action == SF_TX_NONE, "paused: no new chunk");
    s = sf_tx_xon(&t);
    CHECK(s.action == SF_TX_START && s.off == 2048, "XON starts the next chunk");

    // Already paused when the write begins (the device said XOFF before).
    s = sf_tx_begin(&t, 10, 2048, 1000, true);
    CHECK(s.action == SF_TX_NONE, "XOFF in force: nothing starts");
    s = sf_tx_tick(&t, 999);
    CHECK(s.action == SF_TX_NONE, "before the deadline: wait");
    s = sf_tx_tick(&t, 1000);
    CHECK(s.action == SF_TX_ABORT && t.result == -1, "no XON by the deadline: abort, error");
    s = sf_tx_begin(&t, 10, 2048, 5000, false);
    CHECK(s.action == SF_TX_START && t.result == 0, "the next write starts clean");

    // A DMA error.
    s = sf_tx_dma_error(&t);
    CHECK(s.action == SF_TX_ABORT && t.result == -1, "a DMA error aborts");
    s = sf_tx_begin(&t, 10, 2048, 5000, false);
    s = sf_tx_chunk_done(&t);
    s = sf_tx_usart_tc(&t);
    CHECK(s.action == SF_TX_DONE && t.result == 0, "and the next write works");
    s = sf_tx_tick(&t, 999999);
    CHECK(s.action == SF_TX_NONE, "after done the deadline means nothing");
    // A TC that never comes
    sf_tx_begin(&t, 10, 2048, 100, false);
    sf_tx_chunk_done(&t);
    s = sf_tx_tick(&t, 100);
    CHECK(s.action == SF_TX_ABORT, "no USART TC by the deadline: abort");
}

int main(void)
{
    test_timing();
    test_ring();
    test_transparent();
    test_rtu();
    test_errors();
    test_xonxoff();
    test_tx();
    printf("test_serial_frame: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
