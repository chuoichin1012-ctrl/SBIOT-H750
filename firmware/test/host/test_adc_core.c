// Host tests for the AI logic and the factory record (spec §6.6, §12.1, Plan 07b): make -C stm32/firmware/test/host
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adc_core.h"
#include "crc32.h"

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

static const adc_cal_t NOMINAL = {{1, 1}, {0, 0}};
static uint16_t g_half[ADC_HALF][ADC_CH];

static void fill(uint16_t c0, uint16_t c1)
{
    for (int i = 0; i < ADC_HALF; i++) {
        g_half[i][0] = c0;
        g_half[i][1] = c1;
    }
}

// seq_read: the ISR's counter as the sum ends.
static uint32_t g_seq;
static uint32_t seq_same(void *ctx) { return g_seq; }
static uint32_t seq_moved(void *ctx) { return g_seq + 1; }  // the ISR ran during the sum

static void test_handoff(void)
{
    adc_core_t a;
    adc_core_init(&a, &NOMINAL, false);
    CHECK(!adc_core_ok(&a, 0) && !adc_core_ok(&a, 1) && adc_core_ua(&a, 0) == 0, "nothing published before the first window");
    fill(26162, 5232);
    g_seq = 7;
    CHECK(!adc_core_half(&a, g_half, 7, seq_moved, NULL) && a.late == 1 && !adc_core_ok(&a, 0),
          "seq moved during the sum: dropped, late 1, still nothing published");
    CHECK(adc_core_half(&a, g_half, 7, seq_same, NULL) && adc_core_ok(&a, 0) && adc_core_ok(&a, 1),
          "seq unchanged: published");
    int32_t ua0 = adc_core_ua(&a, 0), ua1 = adc_core_ua(&a, 1);
    CHECK(ua0 > 19990 && ua0 < 20010, "code 26 162 is 20 mA (%ld µA)", (long)ua0);
    CHECK(ua1 > 3990 && ua1 < 4010, "code 5 232 is 4 mA (%ld µA)", (long)ua1);
    // The mean of a window, not one sample.
    for (int i = 0; i < ADC_HALF; i++) {
        g_half[i][0] = (uint16_t)(i % 2 ? 26000 + 100 : 26000 - 100);
        g_half[i][1] = 0;
    }
    g_seq = 8;
    CHECK(adc_core_half(&a, g_half, 8, seq_same, NULL) && fabsf(a.mean[0] - 26000.0f) < 0.01f, "the window's mean (%f)",
          (double)a.mean[0]);
    CHECK(adc_core_ok(&a, 1) && adc_core_ua(&a, 1) == 0, "0 mA is a value, not a failure");
    // A fault fails both channels until a full new half.
    adc_core_fail(&a);
    CHECK(!adc_core_ok(&a, 0) && !adc_core_ok(&a, 1) && adc_core_ua(&a, 0) == 0, "a fault: both failed, nothing published");
    fill(13081, 13081);
    g_seq = 9;
    CHECK(!adc_core_half(&a, g_half, 9, seq_moved, NULL) && !adc_core_ok(&a, 0), "a late half after the fault publishes nothing");
    CHECK(adc_core_half(&a, g_half, 9, seq_same, NULL) && adc_core_ok(&a, 0) && adc_core_ok(&a, 1), "a full new half: back");
    // A fault before the very first window.
    adc_core_init(&a, &NOMINAL, false);
    adc_core_fail(&a);
    CHECK(!adc_core_ok(&a, 0), "a fault before the first window: nothing");
    CHECK(adc_core_half(&a, g_half, 9, seq_same, NULL) && adc_core_ok(&a, 0), "then the first full half publishes");
    // Dead: never published.
    adc_core_dead(&a);
    CHECK(!adc_core_half(&a, g_half, 9, seq_same, NULL) && !adc_core_ok(&a, 0), "dead: nothing, ever");
}

static void test_conversion(void)
{
    CHECK(fabsf(adc_nominal_ma(26161.6f) - 20.0f) < 0.001f, "20 mA is code 26 161.6 (%f)", (double)adc_nominal_ma(26161.6f));
    CHECK(fabsf(adc_nominal_ma(0) - 0.0f) < 1e-6f, "code 0 is 0 mA");
    // The two-point calibration: the channel reads 4.1 at 4 mA and 20.3 at 20 mA.
    float g = 16.0f / (20.3f - 4.1f), o = 4.0f - g * 4.1f;
    adc_cal_t cal = {{g, 1}, {o, 0}};
    adc_core_t a;
    adc_core_init(&a, &cal, true);
    float code = 12.2f / adc_nominal_ma(1.0f);  // nominally 12.2 mA, which is 12 mA once calibrated
    fill((uint16_t)lroundf(code), (uint16_t)lroundf(code));
    adc_core_half(&a, g_half, g_seq, seq_same, NULL);
    int32_t ua0 = adc_core_ua(&a, 0), ua1 = adc_core_ua(&a, 1);
    CHECK(labs(ua0 - 12000) < 5, "calibrated channel 1: 12 000 µA (%ld)", (long)ua0);
    CHECK(labs(ua1 - 12200) < 5, "channel 2 keeps its own coefficients: 12 200 µA (%ld)", (long)ua1);
    // A calibration that goes below 0 gives 0, still ok.
    adc_cal_t neg = {{1, 1}, {-0.5f, 0}};
    adc_core_init(&a, &neg, true);
    fill(0, 0);
    adc_core_half(&a, g_half, g_seq, seq_same, NULL);
    CHECK(adc_core_ok(&a, 0) && adc_core_ua(&a, 0) == 0, "below 0: 0 µA");
}

static void test_record(void)
{
    adc_cal_t cal = {{1.01f, 0.99f}, {-0.02f, 0.03f}}, out;
    uint8_t r[FREC_SIZE];
    frec_encode(&cal, r);
    CHECK(!memcmp(r, "SBFR", 4) && r[4] == 1 && r[5] == 0 && r[6] == 64 && r[7] == 0, "magic, version 1, length 64");
    uint32_t crc;
    memcpy(&crc, r + 60, 4);
    CHECK(crc == crc32_update(0, r, 60) && r[30] == 0xFF, "CRC-32 over 0..59; reserved bytes 0xFF");
    CHECK(frec_parse(r, sizeof r, &out) && !memcmp(&out, &cal, sizeof cal), "a round trip");
    CHECK(!frec_parse(r, 32, &out), "too short");
    uint8_t bad[FREC_SIZE];
    memcpy(bad, r, sizeof r);
    bad[9] ^= 1;
    CHECK(!frec_parse(bad, sizeof bad, &out), "a wrong CRC");
    memcpy(bad, r, sizeof r);
    bad[0] = 'X';
    CHECK(!frec_parse(bad, sizeof bad, &out), "a wrong magic");
    memset(bad, 0xFF, sizeof bad);
    CHECK(!frec_parse(bad, sizeof bad, &out), "an erased sector");
    // A good CRC around absurd coefficients: rejected.
    static const adc_cal_t absurd[] = {{{0, 1}, {0, 0}}, {{1e9f, 1}, {0, 0}}, {{1, 1}, {0, 2}}, {{1, NAN}, {0, 0}}, {{1, 1}, {INFINITY, 0}}};
    for (size_t i = 0; i < sizeof absurd / sizeof absurd[0]; i++) {
        frec_encode(&absurd[i], bad);
        CHECK(!frec_parse(bad, sizeof bad, &out), "absurd coefficients %u: rejected", (unsigned)i);
    }
    // Choosing between the copies.
    uint8_t b[FREC_SIZE], broken[FREC_SIZE];
    adc_cal_t cal_b = {{1.02f, 1.0f}, {0, 0}};
    frec_encode(&cal_b, b);
    memset(broken, 0xFF, sizeof broken);
    CHECK(frec_choose(r, b, &out) && out.gain[0] == cal.gain[0], "both valid: A wins");
    CHECK(frec_choose(broken, b, &out) && out.gain[0] == cal_b.gain[0], "A broken: B");
    CHECK(frec_choose(NULL, b, &out) && out.gain[0] == cal_b.gain[0], "A unreadable: B");
    CHECK(!frec_choose(broken, NULL, &out) && out.gain[0] == 1 && out.gain[1] == 1 && out.offset_ma[0] == 0,
          "neither: the nominal coefficients, not calibrated");
}

int main(void)
{
    test_handoff();
    test_conversion();
    test_record();
    printf("test_adc_core: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
