// Host tests for the hardware-free parts of board and boot: make -C stm32/firmware/test/host
#include <stdio.h>
#include "crc32.h"
#include "jumpcheck.h"
#include "tick64.h"

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

static void test_jumpcheck(void)
{
    CHECK(boot_app_ok(0x20020000u, 0x900051E9u), "MSP at the top of DTCM, Thumb reset vector in the slot");
    CHECK(boot_app_ok(0x24080000u, 0x90000401u), "MSP at the top of AXI, reset at the vector table");
    CHECK(boot_app_ok(0x20000008u, 0x9016FFFFu), "lowest MSP, last address of the slot");
    CHECK(!boot_app_ok(0x20000000u, 0x900051E9u), "MSP at the DTCM base: empty stack");
    CHECK(!boot_app_ok(0x20020008u, 0x900051E9u), "MSP above DTCM");
    CHECK(!boot_app_ok(0x2001FFFCu, 0x900051E9u), "MSP not 8-byte aligned");
    CHECK(!boot_app_ok(0x30000000u, 0x900051E9u), "MSP in SRAM1");
    CHECK(!boot_app_ok(0x20020000u, 0x900051E8u), "reset vector without the Thumb bit");
    CHECK(!boot_app_ok(0x20020000u, 0x900003E1u), "reset vector inside the header");
    CHECK(!boot_app_ok(0x20020000u, 0x90170001u), "reset vector past the slot");
    CHECK(!boot_app_ok(0xFFFFFFFFu, 0xFFFFFFFFu), "erased flash");
    // What the bench QSPI held at 0x90000400 before Plan 02 (the old firmware, vectors at 0x90000000).
    CHECK(!boot_app_ok(0x8809F380u, 0xF380E001u), "old firmware's code read as vectors");
}

int main(void)
{
    test_tick64();
    test_crc32();
    test_jumpcheck();
    printf("test_board: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
