#include "flash_spi4.h"
#include <stdbool.h>
#include <string.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "spi.h"

#define BOUNCE 4096u     // the largest data phase: a 4 KB read chunk (spi_nor.c CHUNK)
#define DATA_MS 50u      // 4 KB at 30 MHz is 1.1 ms: a phase over 50 ms is aborted

static spi_nor_t s_spi;
static StaticSemaphore_t s_lock_mem, s_done_mem;
static SemaphoreHandle_t s_lock, s_done;
static volatile bool s_failed;
static unsigned s_dma_errors;
static uint8_t s_bounce[BOUNCE] __attribute__((section(".sram1_bss"), aligned(32)));  // MPU: non-cacheable

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static void busy_wait(void) { vTaskDelay(1); }

static void done_from_isr(SPI_HandleTypeDef *h, bool failed)
{
    if (h != &hspi4 || !s_done)
        return;
    s_failed = failed;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    portYIELD_FROM_ISR(woken);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *h) { done_from_isr(h, false); }
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *h) { done_from_isr(h, false); }
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *h) { done_from_isr(h, true); }

static int data(SPI_HandleTypeDef *h, const uint8_t *tx, uint8_t *rx, uint32_t n)
{
    if (n > BOUNCE)
        return NOR_EIO;
    xSemaphoreTake(s_done, 0);  // a completion left over from an aborted phase
    s_failed = false;
    if (tx)
        memcpy(s_bounce, tx, n);
    // s_bounce is non-cacheable (MPU region 2): no cache maintenance, only the order of the copy
    // against the DMA's start (spec §5.4)
    __DMB();
    HAL_StatusTypeDef st = tx ? HAL_SPI_Transmit_DMA(h, s_bounce, (uint16_t)n) : HAL_SPI_Receive_DMA(h, s_bounce, (uint16_t)n);
    int r = NOR_EIO;
    if (st == HAL_OK) {
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(DATA_MS)) != pdTRUE) {
            HAL_SPI_Abort(h);  // stops the DMA and the SPI: the buffer is ours again
            xSemaphoreTake(s_done, 0);
            r = NOR_ETIMEOUT;
        } else {
            r = s_failed ? NOR_EIO : NOR_OK;
        }
    }
    __DMB();  // the DMA's writes before the copy out
    if (r == NOR_OK && rx)
        memcpy(rx, s_bounce, n);
    if (r != NOR_OK)
        s_dma_errors++;
    return r;
}

static const spi_nor_port_t k_port = {lock, unlock, busy_wait, data};

// Stands in for a flash that failed its init: never used, even if the bus answers later (as in
// the bootloader, Plan 04b). Its users (OTA, FlashDB) see every operation fail.
static int dead_read(nor_t *n, uint32_t off, void *buf, uint32_t len) { return NOR_EIO; }
static int dead_erase(nor_t *n, uint32_t off) { return NOR_EIO; }
static int dead_program(nor_t *n, uint32_t off, const void *buf, uint32_t len) { return NOR_EIO; }
static nor_t s_dead = {dead_read, dead_erase, dead_program, 0};
static int s_init = NOR_EIO;

int flash_spi4_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_mem);
    s_done = xSemaphoreCreateBinaryStatic(&s_done_mem);
    s_init = spi_nor_init(&s_spi, &hspi4, NULL, &k_port);  // the wdog task kicks meanwhile
    return s_init;
}

nor_t *flash_spi4(void) { return s_init == NOR_OK ? &s_spi.nor : &s_dead; }
uint32_t flash_spi4_jedec(void) { return spi_nor_jedec(&s_spi); }
unsigned flash_spi4_dma_errors(void) { return s_dma_errors; }
