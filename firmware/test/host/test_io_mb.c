// Host tests for the IO slave (Plan 08a): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "io_mb.h"

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

static bool g_do[2], g_di[2] = {true, false}, g_ok[2] = {true, false};
static int32_t g_ua[2] = {12500, 0};
static bool do_get(int i) { return g_do[i]; }
static int do_set(int i, bool on) { g_do[i] = on; return 0; }
static bool di_get(int i) { return g_di[i]; }
static int32_t ai_ua(int i) { return g_ua[i]; }
static bool ai_ok(int i) { return g_ok[i]; }
static const io_mb_ops_t OPS = {2, 2, 2, do_get, do_set, di_get, ai_ua, ai_ok};

static size_t ask(const uint8_t *req, size_t n, uint8_t *rep)
{
    uint8_t r[16];
    memcpy(r, req, n);
    n = mb_rtu_seal(r, n);
    return io_mb_remote(r, n, rep, 64);
}

int main(void)
{
    io_mb_init(&OPS, 200);
    uint8_t rep[64];
    CHECK(io_mb_address() == 200, "io.modbusAddress");
    // FC 5: coil 0 on -> DO1
    size_t n = ask((const uint8_t[]){200, 5, 0, 0, 0xFF, 0}, 6, rep);
    CHECK(n == 8 && g_do[0] && !memcmp(rep, (const uint8_t[]){200, 5, 0, 0, 0xFF, 0}, 6) && mb_rtu_valid(rep, n),
          "FC 5 coil 0 on: DO1 on, echoed");
    // FC 1: read 2 coils
    n = ask((const uint8_t[]){200, 1, 0, 0, 0, 2}, 6, rep);
    CHECK(n == 6 && rep[1] == 1 && rep[2] == 1 && rep[3] == 0x01, "FC 1: coils 01");
    // FC 15: both coils, 10b -> DO1 off, DO2 on
    n = ask((const uint8_t[]){200, 15, 0, 0, 0, 2, 1, 0x02}, 8, rep);
    CHECK(n == 8 && !g_do[0] && g_do[1], "FC 15: DO1 off, DO2 on");
    // FC 2: discrete inputs
    n = ask((const uint8_t[]){200, 2, 0, 0, 0, 2}, 6, rep);
    CHECK(n == 6 && rep[3] == 0x01, "FC 2: DI1 active, DI2 not (%02x)", rep[3]);
    // FC 4: AI1 as float32 ABCD in mA: 12.5 = 0x41480000
    n = ask((const uint8_t[]){200, 4, 0, 0, 0, 2}, 6, rep);
    CHECK(n == 9 && rep[2] == 4 && !memcmp(rep + 3, (const uint8_t[]){0x41, 0x48, 0, 0}, 4), "FC 4: AI1 12.5 mA ABCD");
    // FC 4 on AI2, failed: exception 4
    n = ask((const uint8_t[]){200, 4, 0, 2, 0, 2}, 6, rep);
    CHECK(n == 5 && rep[1] == 0x84 && rep[2] == MB_EX_DEVICE_FAILURE, "FC 4 on a failed AI: exception 4");
    // An illegal address
    n = ask((const uint8_t[]){200, 1, 0, 2, 0, 1}, 6, rep);
    CHECK(n == 5 && rep[1] == 0x81 && rep[2] == MB_EX_ILLEGAL_ADDRESS, "FC 1 coil 2: illegal address");
    // FC 15 across a missing coil changes nothing
    g_do[0] = g_do[1] = false;
    n = ask((const uint8_t[]){200, 15, 0, 1, 0, 2, 1, 0x03}, 8, rep);
    CHECK(n == 5 && rep[1] == 0x8F && !g_do[0] && !g_do[1], "FC 15 over coils 1..2: refused, nothing written");
    // Not for us
    CHECK(ask((const uint8_t[]){201, 1, 0, 0, 0, 1}, 6, rep) == 0, "another unit: 0");
    uint8_t bad[8] = {200, 1, 0, 0, 0, 1, 0, 0};
    CHECK(io_mb_remote(bad, 8, rep, sizeof rep) == 0, "a bad CRC: 0");
    CHECK(io_mb_map() != NULL, "the map for the TCP gateway");
    printf("test_io_mb: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
