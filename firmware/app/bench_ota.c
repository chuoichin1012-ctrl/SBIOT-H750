#include "bench_ota.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "bind.h"
#include "bind_key.h"
#include "blog.h"
#include "flashmap.h"
#include "ota_app.h"
#include "ota_image.h"
#include "spi.h"
#include "flash_spi4.h"
#include "usart.h"

#define BLOCK 4096u

static int s_spi_err = NOR_EIO;

bool app_bind_ok(void)
{
    uint8_t uid[BIND_UID_LEN];
    memcpy(uid, (const void *)UID_BASE, sizeof uid);
    return bind_valid((const uint8_t *)(QSPI_XIP_BASE + QSPI_BIND), BIND_PUBKEY, uid);
}

void bench_ota_init(void)
{
    s_spi_err = flash_spi4_init();
    blog("app: spi4 id %06lx%s\r\n", (unsigned long)flash_spi4_jedec(), s_spi_err ? " FAILED" : "");
}

static void report_slot(const char *name, uint32_t off)
{
    img_hdr_t h;
    int r = s_spi_err ? IMG_EREAD : img_check(flash_spi4(), off, NULL, &h);
    if (r == IMG_OK)
        blog("app: %s v%u.%u.%u size %lu ok\r\n", name, h.version[0], h.version[1], h.version[2], (unsigned long)h.size);
    else
        blog("app: %s none (%d)\r\n", name, r);
}

void bench_ota_report(void)
{
    img_hdr_t h;
    if (img_parse((const uint8_t *)QSPI_XIP_BASE, &h) == IMG_OK)
        blog("app: qspi v%u.%u.%u size %lu\r\n", h.version[0], h.version[1], h.version[2], (unsigned long)h.size);
    else
        blog("app: qspi header invalid\r\n");
    report_slot("staging", SPI_STAGING);
    report_slot("factory", SPI_FACTORY);
}

static bool rx(uint8_t *p, uint32_t n, uint32_t ms)
{
    return HAL_UART_Receive(&huart7, p, (uint16_t)n, ms) == HAL_OK;  // n <= BLOCK
}

void bench_ota_upload(bool reset)
{
    static uint8_t buf[BLOCK] __attribute__((section(".axi_bss")));  // not DTCM (spec §5.3)
    static ota_stage_t st;
    uint32_t total;
    if (!rx(buf, 4, 2000)) {
        blog("ota: no length\r\n");
        return;
    }
    memcpy(&total, buf, 4);
    int r = s_spi_err ? OTA_EFLASH : ota_stage_claim(&st);  // one owner of staging, with the web (Plan 10c)
    if (!r && (r = ota_stage_begin(&st, flash_spi4(), total)))
        ota_stage_release(&st);
    blog("ota: ready %lu %d\r\n", (unsigned long)total, r);
    if (r)
        return;
    for (uint32_t got = 0; got < total;) {
        uint32_t n = total - got < BLOCK ? total - got : BLOCK;
        if (!rx(buf, n, 5000)) {
            blog("ota: timeout at %lu\r\n", (unsigned long)got);
            ota_stage_release(&st);
            return;
        }
        r = ota_stage_write(&st, buf, n);
        got += n;
        if (r) {
            blog("ota: done %d\r\n", r);
            ota_stage_release(&st);
            return;
        }
        blog("ota: %lu\r\n", (unsigned long)got);
    }
    r = ota_stage_finish(&st);
    if (r)
        ota_stage_release(&st);  // a success keeps it: the image waits for the reset
    blog("ota: done %d\r\n", r);
    if (!r && reset) {
        vTaskDelay(pdMS_TO_TICKS(50));
        NVIC_SystemReset();  // the bootloader installs the new image (spec §11.3)
    }
}
