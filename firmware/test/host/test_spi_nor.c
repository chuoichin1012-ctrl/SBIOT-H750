// spi_nor.c over a fake HAL and a fake GD25Q32E: the deadlines of a busy flash. make -C stm32/firmware/test/host
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "spi_nor.h"

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

GPIO_TypeDef g_fake_gpio;
static SPI_HandleTypeDef s_h;
static uint32_t s_tick;
static int s_busy_polls;  // status reads that still find a write in progress
static uint8_t s_cmd;     // the command of the transfer under chip select
static bool s_header;     // the next transmit is a command header

uint32_t HAL_GetTick(void) { return s_tick; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (state == GPIO_PIN_RESET)
        s_header = true;  // chip select low: a new command
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *p, uint16_t n, uint32_t timeout)
{
    if (s_header)
        s_cmd = p[0], s_header = false;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *h, uint8_t *p, uint16_t n, uint32_t timeout)
{
    if (s_cmd == 0x05) {
        p[0] = s_busy_polls > 0 ? 0x01 : 0x00;
        if (s_busy_polls > 0)
            s_busy_polls--;
    } else if (s_cmd == 0x9F && n == 3) {
        p[0] = 0xC8, p[1] = 0x40, p[2] = 0x16;
    } else {
        memset(p, 0xFF, n);
    }
    return HAL_OK;
}

static uint32_t s_kick_ms;
static void kick(void) { s_tick += s_kick_ms; }  // what the waiting task loses to the others

static void test_preempted_after_a_busy_poll(void)
{
    spi_nor_t s;
    s_kick_ms = 1;
    s_busy_polls = 0;
    CHECK(spi_nor_init(&s, &s_h, kick, NULL) == NOR_OK, "init");
    uint8_t page[256];
    memset(page, 0x5A, sizeof page);
    // The flash is still programming at the first poll; the task then loses the CPU for 10 ms (TLS handshakes
    // of higher priority tasks). The program ended long before the deadline (6 ms): no timeout.
    s_busy_polls = 1, s_kick_ms = 10;
    CHECK(s.nor.program(&s.nor, 0, page, sizeof page) == NOR_OK, "a program polled busy, then 10 ms away: done, not a timeout");
    s_busy_polls = 1, s_kick_ms = 700;
    CHECK(s.nor.erase_sector(&s.nor, 0) == NOR_OK, "an erase polled busy, then 700 ms away: done, not a timeout");
}

static void test_a_flash_that_stays_busy(void)
{
    spi_nor_t s;
    s_kick_ms = 1;
    s_busy_polls = 0;
    spi_nor_init(&s, &s_h, kick, NULL);
    uint8_t page[16] = {0};
    s_busy_polls = 1000000, s_kick_ms = 1;
    uint32_t t0 = s_tick;
    CHECK(s.nor.program(&s.nor, 0, page, sizeof page) == NOR_ETIMEOUT && s_tick - t0 <= 10,
          "still busy after its deadline: a timeout (%u ms)", (unsigned)(s_tick - t0));
    s_busy_polls = 1000000;
    t0 = s_tick;
    CHECK(s.nor.erase_sector(&s.nor, 0) == NOR_ETIMEOUT && s_tick - t0 <= 610, "an erase: a timeout (%u ms)", (unsigned)(s_tick - t0));
    s_busy_polls = 0;
}

int main(void)
{
    test_preempted_after_a_busy_poll();
    test_a_flash_that_stays_busy();
    printf("test_spi_nor: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
