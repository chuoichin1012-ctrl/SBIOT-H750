// Host tests for the hardware-free parts of board and boot: make -C stm32/firmware/test/host
#include <stdio.h>
#include "crc32.h"
#include "tick64.h"
#include "pace.h"

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

static void test_tick64(void)
{
    tick64_t t = {0};
    CHECK(tick64_update(&t, 0) == 0, "start");
    CHECK(tick64_update(&t, 1000) == 1000, "plain step");
    CHECK(tick64_update(&t, 1000) == 1000, "same raw value: same time");
    CHECK(tick64_update(&t, 0xFFFFFFF0u) == 0xFFFFFFF0u, "just before the wrap");
    CHECK(tick64_update(&t, 0x10) == 0x100000010ull, "across the wrap");
    CHECK(tick64_update(&t, 0xFFFFFFFFu) == 0x1FFFFFFFFull, "last value of the second period");
    CHECK(tick64_update(&t, 0) == 0x200000000ull, "wrap to exactly 0");
    // Calls 200 ms apart (the wdog period) over 3 counter periods: exact and strictly increasing.
    tick64_t u = {0};
    uint64_t prev = 0, truth = 0;
    for (int i = 0; i < 3 * 21475; i++) {
        truth += 200000;
        uint64_t v = tick64_update(&u, (uint32_t)truth);
        CHECK(v == truth && v > prev, "step %d: %llu != %llu", i, (unsigned long long)v, (unsigned long long)truth);
        if (v != truth)
            break;
        prev = v;
    }
}

static void test_crc32(void)
{
    CHECK(crc32_update(0, "123456789", 9) == 0xCBF43926u, "check value of CRC-32/ISO-HDLC");
    CHECK(crc32_update(0, "", 0) == 0, "empty");
    uint32_t c = crc32_update(0, "1234", 4);
    CHECK(crc32_update(c, "56789", 5) == 0xCBF43926u, "continued over two calls");
}

// Plan 11a task 0 (the user): LED WORK (= WDI) toggles 500 ms apart when running, 100 ms in an OTA, an
// install or a restore, 250 ms halted. The wdog loop calls every 50 ms; the bootloader at its own pace.
static int toggles_apart(uint32_t half, uint32_t step, uint32_t from, int n, uint32_t *gaps)
{
    uint32_t last = from, prev = from;
    int k = 0;
    for (uint32_t t = from; k < n; t += step)
        if (pace_due(t, &last, half)) {
            gaps[k++] = t - prev;
            prev = t;
        }
    return k;
}

static void test_pace(void)
{
    uint32_t last = 1000;
    CHECK(!pace_due(1499, &last, 500) && last == 1000, "before the half period: not due, last kept");
    CHECK(pace_due(1500, &last, 500) && last == 1500, "at it: due, last moved");
    const uint32_t halves[] = {PACE_RUN_MS, PACE_BUSY_MS, PACE_HALT_MS};
    for (int h = 0; h < 3; h++) {
        uint32_t gaps[8];
        toggles_apart(halves[h], 50, 0xFFFFFF00u, 8, gaps);  // across the uint32_t wrap too
        int exact = 1;
        for (int i = 0; i < 8; i++) exact &= gaps[i] == halves[h];
        CHECK(exact, "every 50 ms: toggles exactly %u ms apart (%u %u %u)", (unsigned)halves[h], (unsigned)gaps[0],
              (unsigned)gaps[1], (unsigned)gaps[7]);
    }
    // The bootloader: calls every 10 ms, then a 300 ms gap (a sector erase): never closer than the half
    // period, never later than the half period plus the longest gap between calls.
    last = 0;
    uint32_t prev = 0, worst_close = 1000, worst_late = 0, t = 0;
    for (int i = 0; i < 400; i++) {
        t += (i % 50 == 49) ? 300 : 10;
        if (pace_due(t, &last, PACE_BUSY_MS)) {
            uint32_t g = t - prev;
            if (g < worst_close) worst_close = g;
            if (g > worst_late) worst_late = g;
            prev = t;
        }
    }
    CHECK(worst_close >= PACE_BUSY_MS && worst_late <= PACE_BUSY_MS + 300, "irregular calls: %u..%u ms apart",
          (unsigned)worst_close, (unsigned)worst_late);
}

int main(void)
{
    test_pace();
    test_tick64();
    test_crc32();
    printf("test_board: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
