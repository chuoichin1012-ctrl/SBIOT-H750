#include "board_rng.h"
#include <stdbool.h>
#include <string.h>
#include "FreeRTOS.h"
#include "blog.h"
#include "now_us.h"
#include "rng.h"  // hrng (CubeMX)
#include "rng_core.h"
#include "semphr.h"

static StaticSemaphore_t s_mutex_mem;
static SemaphoreHandle_t s_mutex;
static bool s_ok;
static uint32_t s_xs;  // xorshift state, 0 until first needed

int board_rng_init(void)
{
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_mem);
    // MX_RNG_Init is not called (its failure would spin in Error_Handler): the handle is filled here.
    hrng.Instance = RNG;
    hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
    s_ok = HAL_RNG_Init(&hrng) == HAL_OK;
    return s_ok ? 0 : -1;
}

static uint32_t reg_sr(void *ctx)
{
    (void)ctx;
    return RNG->SR;
}

static uint32_t reg_dr(void *ctx)
{
    (void)ctx;
    return RNG->DR;
}

static void reg_clear_seis(void *ctx)
{
    (void)ctx;
    RNG->SR = ~RNG_SR_SEIS & (RNG_SR_SEIS | RNG_SR_CEIS);  // rc_w0: write 0 to SEIS, 1 keeps CEIS
}

// HAL_RNG_GenerateRandomNumber is not used: on the H750 it never checks the seed error.
// 200 000 polls of SR: well over the first word after enable, a few ms at most.
static const rng_io_t k_io = {.sr = reg_sr, .dr = reg_dr, .clear_seis = reg_clear_seis, .spins = 200000};

int board_rng(void *buf, size_t len)
{
    if (!s_mutex)
        return -1;
    uint8_t *p = buf;
    int r = 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    while (len) {
        uint32_t w;
        if (!s_ok || rng_word(&k_io, &w)) {
            r = -1;  // nothing more is written to buf
            break;
        }
        size_t n = len < sizeof w ? len : sizeof w;
        memcpy(p, &w, n);
        p += n;
        len -= n;
    }
    xSemaphoreGive(s_mutex);
    return r;
}

uint32_t board_rand32(void)
{
    uint32_t w;
    if (board_rng(&w, sizeof w) == 0)
        return w;
    if (!s_xs) {
        const uint32_t *uid = (const uint32_t *)UID_BASE;
        s_xs = (uid[0] ^ uid[1] ^ uid[2] ^ (uint32_t)now_us()) | 1u;
        blog("board: RNG FAILED, LWIP_RAND falls back to xorshift\r\n");
    }
    s_xs ^= s_xs << 13;
    s_xs ^= s_xs >> 17;
    s_xs ^= s_xs << 5;
    return s_xs;
}
