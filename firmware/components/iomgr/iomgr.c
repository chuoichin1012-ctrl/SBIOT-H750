#include "iomgr.h"
#include <string.h>
#include "FreeRTOS.h"
#include "adc_core.h"
#include "adc_drv.h"
#include "blog.h"
#include "io_core.h"
#include "io_mb.h"
#include "main.h"  // the pin names (app.ioc)
#include "now_us.h"
#include "reload.h"
#include "semphr.h"
#include "store.h"
#include "syslog.h"
#include "sysmgr.h"
#include "task.h"

#define IO_STACK_WORDS 768      // 3 KB (spec §6.1)
#define IO_PRIO 6
#define RELOAD_STACK_WORDS 512  // 2 KB
#define RELOAD_PRIO 3
#define RELOAD_POLL_MS 50
#define TIMERS_MS 200

static io_core_t s_core;
static adc_core_t s_adc;
static reload_t s_button;
static SemaphoreHandle_t s_lock;  // s_core and s_adc
static StaticSemaphore_t s_lock_mem;
static uint8_t s_latched;
static int s_tz;
static volatile bool s_factory;
volatile bool g_io_trace;  // bench: log each change of the outputs and each AI window (bench_io.c 'D')
static TaskHandle_t s_io, s_reload;
static StaticTask_t s_io_tcb, s_reload_tcb;
static StackType_t s_io_stack[IO_STACK_WORDS], s_reload_stack[RELOAD_STACK_WORDS];

static uint32_t now_ms(void) { return (uint32_t)(now_us() / 1000u); }
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

// The outputs to the 74HC573: D1/D2, then a pulse on LE (OE is low since board_latch_zero). Under s_lock.
static void latch(void)
{
    uint8_t m = io_core_outputs(&s_core);
    if (m == s_latched)
        return;
    HAL_GPIO_WritePin(DO1_D_GPIO_Port, DO1_D_Pin, (m & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DO2_D_GPIO_Port, DO2_D_Pin, (m & 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_LE_GPIO_Port, LATCH_LE_Pin, GPIO_PIN_SET);  // transparent: Q follows D
    (void)LATCH_LE_GPIO_Port->ODR;
    HAL_GPIO_WritePin(LATCH_LE_GPIO_Port, LATCH_LE_Pin, GPIO_PIN_RESET);  // latched
    s_latched = m;
    if (g_io_trace)
        blog("io: outputs 0x%02x at %lu ms\r\n", m, (unsigned long)now_ms());
}

static uint8_t di_pins(void)
{
    return (uint8_t)((HAL_GPIO_ReadPin(DI1_GPIO_Port, DI1_Pin) == GPIO_PIN_SET ? 1u : 0u) |
                     (HAL_GPIO_ReadPin(DI2_GPIO_Port, DI2_Pin) == GPIO_PIN_SET ? 2u : 0u));
}

static uint32_t seq_read(void *ctx) { return adc_drv_seq(); }

static void io_task(void *arg)
{
    uint32_t last_tick = now_ms(), last_timers = last_tick;
    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(IO_TICK_MS));
        if (bits & ADC_N_FAULT) {
            lock();
            adc_core_fail(&s_adc);  // both channels, until a full new half (spec §6.6)
            unlock();
            adc_drv_recover();
            bits &= ~ADC_N_HALF;  // a half from before the recovery is not a new window
        }
        if (bits & ADC_N_HALF) {
            uint32_t seq = adc_drv_seq();  // first, then the half it belongs to
            const uint16_t (*half)[ADC_CH] = adc_drv_half();
            lock();
            bool pub = adc_core_half(&s_adc, half, seq, seq_read, NULL);
            int32_t ua0 = adc_core_ua(&s_adc, 0), ua1 = adc_core_ua(&s_adc, 1);
            unlock();
            if (pub && g_io_trace)
                blog("io: AI %ld %ld at %lu ms\r\n", (long)ua0, (long)ua1, (unsigned long)now_ms());
        }
        uint32_t now = now_ms();
        if (now - last_tick < IO_TICK_MS)
            continue;  // an ADC wake-up between ticks: the DI and the button keep their 10 ms period
        last_tick = now;
        uint8_t pins = di_pins();
        bool restart = false;
        lock();
        for (int i = 0; i < IO_DI; i++)
            io_core_di(&s_core, i, (pins >> i) & 1, now);
        io_core_tick(&s_core, now);
        if (now - last_timers >= TIMERS_MS) {
            last_timers = now;
            restart = io_core_timers(&s_core, sysmgr_utc_ms() / 1000, s_tz, sysmgr_clock_set(), now);
        }
        latch();
        unlock();
        if (restart) {
            SLOG_W("io", "a timer restarts the gateway");
            sysmgr_restart_later(1000);
        }
        reload_ev_t ev = reload_step(&s_button, HAL_GPIO_ReadPin(RELOAD_GPIO_Port, RELOAD_Pin) == GPIO_PIN_RESET, now);
        if (ev == RELOAD_HINT) {
            blog("io: Reload held 3 s: release now to restore the factory settings\r\n");
        } else if (ev == RELOAD_FACTORY) {
            s_factory = true;
            xTaskNotifyGive(s_reload);
        }
    }
}

// The slow work, off the io task: the factory reset, and the keepOutputs saves (the one writer, always
// with the latest mask).
static void reload_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RELOAD_POLL_MS));
        if (s_factory) {
            s_factory = false;
            SLOG_W("io", "Reload button: restoring the factory settings");
            if (store_factory_reset() != STORE_OK)  // the store restarts the gateway either way
                SLOG_E("io", "some settings could not be erased");
        }
        uint8_t mask;
        lock();
        bool save = io_core_persist_take(&s_core, &mask);
        unlock();
        if (save) {
            bool ok = store_state_set("do", &mask, 1) == STORE_OK;
            lock();
            io_core_persist_done(&s_core, mask, ok);
            unlock();
        }
    }
}

void iomgr_start(const cJSON *io, const cJSON *sys, nor_t *spi4)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_mem);
    io_cfg_t cfg;
    io_cfg_parse(io, &cfg);
    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(sys, "timezone");
    s_tz = cJSON_IsNumber(tz) && tz->valueint >= -12 && tz->valueint <= 12 ? tz->valueint : 0;
    uint32_t now = now_ms();
    io_core_init(&s_core, &cfg, di_pins());
    uint8_t mask = 0;
    bool found = cfg.keep && store_state_get("do", &mask, 1) == 1;
    io_core_restore(&s_core, mask, found, now);
    s_latched = 0;  // board_latch_zero latched zeros
    latch();
    if (found)
        blog("io: outputs restored (0x%02x)\r\n", mask);
    reload_init(&s_button);
    // The IO as a Modbus slave on both ports (io.modbusAddress, spec §6.4; the bridges ask it).
    static const io_mb_ops_t ops = {IO_DO, IO_DI, ADC_CH, iomgr_do_get, iomgr_do_set, iomgr_di_get, iomgr_ai_ua, iomgr_ai_ok};
    io_mb_init(&ops, cfg.mb_addr);

    // The factory record: two copies, the first valid wins; none: nominal (spec §5.2, §6.6).
    static uint8_t a[FREC_SIZE], b[FREC_SIZE];
    bool ra = spi4->read(spi4, FREC_A, a, sizeof a) == NOR_OK, rb = spi4->read(spi4, FREC_B, b, sizeof b) == NOR_OK;
    adc_cal_t cal;
    bool calibrated = frec_choose(ra ? a : NULL, rb ? b : NULL, &cal);
    adc_core_init(&s_adc, &cal, calibrated);
    blog("io: AI %s (gain %d/1000 %d/1000)\r\n", calibrated ? "calibrated" : "with nominal coefficients",
         (int)(cal.gain[0] * 1000), (int)(cal.gain[1] * 1000));

    s_io = xTaskCreateStatic(io_task, "io", IO_STACK_WORDS, NULL, IO_PRIO, s_io_stack, &s_io_tcb);
    s_reload = xTaskCreateStatic(reload_task, "reload", RELOAD_STACK_WORDS, NULL, RELOAD_PRIO, s_reload_stack, &s_reload_tcb);
    if (adc_drv_start(s_io) != 0) {
        lock();
        adc_core_dead(&s_adc);
        unlock();
        SLOG_E("io", "ADC3 could not be started: AI1 and AI2 fail");
    }
}

// ---- the API --------------------------------------------------------------------------------------

int iomgr_do_set(int i, bool on)
{
    lock();
    int r = io_core_do_set(&s_core, i, on, now_ms());
    latch();
    unlock();
    if (r == 0)
        SLOG_I("io", "DO%d %s", i + 1, on ? "on" : "off");
    return r;
}

bool iomgr_do_get(int i)
{
    lock();
    bool v = io_core_do_get(&s_core, i);
    unlock();
    return v;
}

bool iomgr_di_get(int i)
{
    lock();
    bool v = io_core_di_get(&s_core, i);
    unlock();
    return v;
}

int32_t iomgr_ai_ua(int i)
{
    lock();
    int32_t v = adc_core_ua(&s_adc, i);
    unlock();
    return v;
}

bool iomgr_ai_ok(int i)
{
    lock();
    bool v = adc_core_ok(&s_adc, i);
    unlock();
    return v;
}

cJSON *iomgr_state_json(void)
{
    bool d[IO_DO], di[IO_DI];
    int32_t ai[ADC_CH];
    lock();
    for (int i = 0; i < IO_DO; i++)
        d[i] = io_core_do_get(&s_core, i);
    for (int i = 0; i < IO_DI; i++)
        di[i] = io_core_di_get(&s_core, i);
    for (int i = 0; i < ADC_CH; i++)
        ai[i] = adc_core_ua(&s_adc, i);
    unlock();
    cJSON *o = cJSON_CreateObject();
    cJSON *x = cJSON_AddArrayToObject(o, "do");
    for (int i = 0; i < IO_DO; i++)
        cJSON_AddItemToArray(x, cJSON_CreateBool(d[i]));
    x = cJSON_AddArrayToObject(o, "di");
    for (int i = 0; i < IO_DI; i++)
        cJSON_AddItemToArray(x, cJSON_CreateBool(di[i]));
    x = cJSON_AddArrayToObject(o, "ai");
    for (int i = 0; i < ADC_CH; i++)
        cJSON_AddItemToArray(x, cJSON_CreateNumber(ai[i]));
    cJSON_AddArrayToObject(o, "ao");  // no analog outputs on this hardware
    return o;
}

void iomgr_status(io_status_t *s)
{
    adc_drv_info_t d;
    adc_drv_info(&d);
    memset(s, 0, sizeof *s);
    lock();
    s->ai_calibrated = s_adc.calibrated;
    s->adc_late = s_adc.late;
    s->adc_windows = s_adc.windows;
    s->mean[0] = s_adc.mean[0];
    s->mean[1] = s_adc.mean[1];
    unlock();
    s->adc_irqs = d.irqs;
    s->adc_errors = d.errors;
    s->adc_recoveries = d.recoveries;
    s->cr = d.cr;
    s->cfgr = d.cfgr;
    s->cfgr2 = d.cfgr2;
    s->smpr1 = d.smpr1;
}
