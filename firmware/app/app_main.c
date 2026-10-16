// Plan 02 app (spec §6.1, §6.3): board_early, then the wdog and init tasks and the scheduler.
// init reports the boot, checks the anti-clone record (spec §11.5) and then serves as the UART7
// bench console: 'i' info, 'r' software reset, 'w' stop the watchdog kicks, 'c' CRC-32 of the
// image, 'o' the OTA images, 'u' upload an image into staging and reset ('U' without the reset).
// Plan 06a adds the network: 'n' its state, 'N' save a `network` config line, 'L' drop the link for
// 3 s. Later plans replace it.
#include "app_main.h"
#include "bench_ota.h"
#include "bench_serial.h"
#include "heap_newlib.h"
#include "board_rng.h"
#include "cfgmodel.h"
#include "netmgr.h"
#include "serial.h"
#include "uart_drv.h"
#include "sysmgr.h"
#include "webapi.h"
#include "flash_spi4.h"
#include "store.h"
#include "store_port.h"
#include "iomgr.h"
#include "bridge.h"
#include "services.h"
#include "bench_io.h"
#include "syslog.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "blog.h"
#include "board.h"
#include "crc32.h"
#include "main.h"
#include "now_us.h"
#include "startup_app.h"
#include "usart.h"
#include "wdog.h"

#define WDOG_STACK_WORDS 512   // 2 KB (spec §6.1)
#define INIT_STACK_WORDS 1536  // 6 KB

static StaticTask_t s_wdog_tcb, s_init_tcb;
static StackType_t s_wdog_stack[WDOG_STACK_WORDS], s_init_stack[INIT_STACK_WORDS];

extern const uint8_t __image_start__[], __image_end__[];  // app.ld: the whole QSPI image

static void report_boot(void)
{
    uint32_t rsr = RCC->RSR;
    RCC->RSR |= RCC_RSR_RMVF;  // the next boot reports only its own cause
    blog("\r\nSBIOT-H750 app " APP_VERSION " rsr=%08lx sysclk=%lu\r\n", (unsigned long)rsr,
         (unsigned long)SystemCoreClock);
    blog("app: handoff %s (%02lx), mpu %s\r\n", g_handoff_faults ? "FAULT" : "ok", (unsigned long)g_handoff_faults,
         g_mpu_boot_match ? "regions 0-1 match boot" : "MISMATCH with boot");
    blog("app: itcm vTaskSwitchContext=%p memcpy=%p\r\n", (void *)vTaskSwitchContext, (void *)memcpy);
}

static void info(void)
{
    // newlib-nano printf has no %llu: report milliseconds, which fit 32 bits for 49 days.
    unsigned long ms = (unsigned long)(now_us() / 1000u);
    blog("app: uptime_ms=%lu ticks=%lu wdog_max_gap_us=%lu\r\n", ms, (unsigned long)xTaskGetTickCount(),
         (unsigned long)wdog_max_gap_us());
}

// The `network` and `system` configs, read once (spec §9.1), or their defaults.
static void net_start(const cfg_env_t *env)
{
    cJSON *n = store_get("network"), *sys = store_get("system");
    if (!n)
        n = cfg_default(env, "network");
    if (!sys)
        sys = cfg_default(env, "system");
    net_cfg_t nc;
    netcfg_from_json(n, sys, &nc);
    cJSON_Delete(n);
    cJSON_Delete(sys);
    uint8_t uid[12], mac[6];
    memcpy(uid, (const void *)UID_BASE, sizeof uid);
    netcfg_mac(uid, mac);
    if (board_rng_init())
        blog("app: RNG FAILED\r\n");
    netmgr_init(&nc, mac);
}

static const char *ip_str(uint32_t a, char *buf)
{
    snprintf(buf, 16, "%lu.%lu.%lu.%lu", (unsigned long)(a >> 24), (unsigned long)(a >> 16 & 255),
             (unsigned long)(a >> 8 & 255), (unsigned long)(a & 255));
    return buf;
}

static void net_report(void)
{
    net_status_t st;
    netmgr_status(&st);
    char a[16], b[16], c[16], d[16], e[16];
    blog("app: net %s link %s %d %s, phy %d id %08lx, %s %s mask %s gw %s dns %s %s\r\n", st.added ? "added" : "DOWN",
         st.eth.up ? "up" : "down", st.eth.speed, st.eth.full_duplex ? "full" : "half", st.eth.phy_addr,
         (unsigned long)st.eth.phy_id, st.dhcp ? "dhcp" : "static", ip_str(st.ip, a), ip_str(st.mask, b), ip_str(st.gw, c),
         ip_str(st.dns1, d), ip_str(st.dns2, e));
    blog("app: net rx_in_use %u rx_starved %u tx_busy_drops %u link_changes %u\r\n", st.eth.rx_in_use, st.eth.rx_starved,
         st.eth.tx_busy_drops, st.eth.link_changes);
    blog("app: net pools heap %u/%u err %u, netconn %u/%u, tcp_pcb %u/%u, tcp_seg %u/%u, pbuf_pool %u/%u, udp_pcb %u/%u\r\n",
         st.heap.used, st.heap.max, st.heap.err, st.netconn.used, st.netconn.max, st.tcp_pcb.used, st.tcp_pcb.max,
         st.tcp_seg.used, st.tcp_seg.max, st.pbuf_pool.used, st.pbuf_pool.max, st.udp_pcb.used, st.udp_pcb.max);
}

// Bench: one line (CR or LF ends it, 2 s per byte at most) into buf; its length.
static size_t read_line(char *buf, size_t cap)
{
    size_t n = 0;
    uint8_t c;
    while (n < cap - 1 && HAL_UART_Receive(&huart7, &c, 1, 2000) == HAL_OK && c != '\r' && c != '\n')
        buf[n++] = (char)c;
    buf[n] = '\0';
    return n;
}

static int hexv(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1;
}

// Bench: "T<port> <hex request>": a Modbus transaction; the answer in hex, or why there is none.
static void bench_transact(void)
{
    static char line[600];
    static uint8_t req[256], resp[300];
    read_line(line, sizeof line);
    int port = line[0] - '0';
    size_t n = 0;
    for (const char *p = line + 1; *p && n < sizeof req;) {
        if (*p == ' ') {
            p++;
            continue;
        }
        if (hexv(p[0]) < 0 || hexv(p[1]) < 0)
            break;
        req[n++] = (uint8_t)(hexv(p[0]) << 4 | hexv(p[1]));
        p += 2;
    }
    uint64_t t0 = now_us();
    int r = n ? serial_transact(port, req, n, resp, sizeof resp, req[0], 1000) : -1;
    unsigned long ms = (unsigned long)((now_us() - t0) / 1000u);
    serial_counters_t sc;
    serial_counters(port, &sc);
    if (r > 0) {
        char hx[2 * 64 + 1];
        int q = 0;
        for (int i = 0; i < r && i < 64; i++)
            q += snprintf(hx + q, sizeof hx - q, "%02x", resp[i]);
        blog("app: T%d answer %d bytes in %lu ms: %s\r\n", port, r, ms, hx);
    } else {
        blog("app: T%d %s in %lu ms (violations %lu)\r\n", port,
             r == SERIAL_DROPPED ? "dropped: t1.5 violation" : r == 0 ? "no answer" : "error", ms, (unsigned long)sc.violations);
    }
}

// Bench: one line of JSON (ended by CR or LF, 2 s per byte at most) saved as `network`. Send it
// after 'N' has been read: the console polls every 10 ms, longer than the FIFO's 16 bytes last.
static void net_save_line(void)
{
    static char line[512];
    size_t n = 0;
    uint8_t c;
    while (n < sizeof line - 1 && HAL_UART_Receive(&huart7, &c, 1, 2000) == HAL_OK && c != '\r' && c != '\n')
        line[n++] = (char)c;
    line[n] = '\0';
    char err[160];
    store_err_t r = store_set_text("network", line, n, err, sizeof err);
    blog("app: network saved: %d %s (applies after a restart)\r\n", r, r ? err : "");
}

static bool print_log_line(const store_log_meta_t *m, const char *line, size_t len, void *ctx)
{
    (void)ctx;
    blog("app: log boot %lu +%lu ms: %.*s\r\n", (unsigned long)m->boot, (unsigned long)m->uptime_ms, (int)len, line);
    return true;
}

static void init_task(void *arg)
{
    report_boot();
    if (!app_bind_ok()) {  // spec §11.5: no service starts on a board this image is not bound to
        blog("app: anti-clone record INVALID, nothing started\r\n");
        for (;;)
            vTaskDelay(pdMS_TO_TICKS(1000));
    }
    bench_ota_init();
    // The configs are read once, here; a saved config applies after a restart (spec §9.1).
    static const cfg_env_t env = {.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};
    uint64_t t0 = now_us();
    store_err_t se = store_init(&env, flash_spi4());
    blog("app: store %s in %lu ms, boot %lu, heap free %lu\r\n", se == STORE_OK ? "mounted" : "FAILED",
         (unsigned long)((now_us() - t0) / 1000u), (unsigned long)store_boot_count(), (unsigned long)heap_free_now());
    net_start(&env);
    // The web (spec §6.3 step 7), then sysmgr last (step 8); both read `system` once (spec §9.1).
    cJSON *sys = store_get("system");
    if (!sys)
        sys = cfg_default(&env, "system");
    // The IO (spec §6.3 step 4): keepOutputs restored, the factory record read, the ADC started.
    cJSON *ioc = store_get("io");
    if (!ioc)
        ioc = cfg_default(&env, "io");
    iomgr_start(ioc, sys, flash_spi4());
    cJSON_Delete(ioc);
    // The serial ports (spec §6.3 step 5): before anything that subscribes to them.
    for (int port = 1; port <= SERIAL_PORTS; port++) {
        char name[8];
        snprintf(name, sizeof name, "port%d", port);
        cJSON *pc = store_get(name);
        if (!pc)
            pc = cfg_default(&env, name);
        serial_start(port, pc, sys);
        cJSON_Delete(pc);
    }
    bench_serial_init();
    services_start(sys);  // spec §6.3 step 6: the bridges after serial, before the web
    if (webapi_start(sys))
        blog("app: web FAILED\r\n");
    sysmgr_start(sys);
    cJSON_Delete(sys);
    info();
    for (;;) {
        uint8_t c;
        // An overrun stops reception until cleared: the console would go deaf (Plan 06a, B1).
        if (huart7.Instance->ISR & USART_ISR_ORE)
            __HAL_UART_CLEAR_FLAG(&huart7, UART_CLEAR_OREF);
        // Sleep while nothing came in: HAL_UART_Receive waits by spinning, which would starve every
        // task below init's priority (syslog, idle) for as long as the console is quiet.
        if (!(huart7.Instance->ISR & USART_ISR_RXNE_RXFNE)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (HAL_UART_Receive(&huart7, &c, 1, 50) != HAL_OK)
            continue;
        switch (c) {
        case 'i':
            info();
            break;
        case 'r':
            blog("app: software reset\r\n");
            vTaskDelay(pdMS_TO_TICKS(20));
            NVIC_SystemReset();
            break;
        case 'w':
            wdog_stop();
            blog("app: watchdog kicks stopped at %lu ms\r\n", (unsigned long)(now_us() / 1000u));
            break;
        case 'c': {
            size_t len = (size_t)(__image_end__ - __image_start__);
            uint64_t t0 = now_us();
            uint32_t crc = crc32_update(0, __image_start__, len);
            blog("app: image crc32=%08lx len=%lu in %lu us\r\n", (unsigned long)crc, (unsigned long)len,
                 (unsigned long)(now_us() - t0));
            break;
        }
        case 'o':
            bench_ota_report();
            break;
        case 'S': {  // bench: the store's state
            store_edge_pin_t p;
            store_edge_pin(&p);
            store_edge_unpin(&p);
            char *sys = store_get_json("system");
            blog("app: store boot %lu edge gen %lu records %lu heap free %lu min %lu dma errors %u\r\n",
                 (unsigned long)store_boot_count(), (unsigned long)p.gen, (unsigned long)p.count, (unsigned long)heap_free_now(),
                 (unsigned long)heap_free_min(), flash_spi4_dma_errors());
            blog("app: system %.120s\r\n", sys ? sys : "(none)");
            blog("app: log %s, %u appends failed\r\n", store_log_ok() ? "mounted" : "NOT MOUNTED", store_log_failures());
            free(sys);
            break;
        }
        case 'P':  // bench: microseconds of idle since the boot, and the clock: the host takes the load
            blog("app: idle_us=%lu now_us=%lu\r\n", (unsigned long)ulTaskGetIdleRunTimeCounter(), (unsigned long)now_us());
            break;
        case 'G': {  // bench: 1000 saves in a row (about 300 KB: garbage collection in cfg), the slowest reported
            char text[64], err[160];
            uint32_t worst = 0, fails = 0;
            uint64_t idle0 = ulTaskGetIdleRunTimeCounter(), at0 = now_us();
            for (int i = 0; i < 1000; i++) {
                snprintf(text, sizeof text, "{\"hostname\":\"stress-%d\"}", i);
                uint64_t t0 = now_us();
                fails += store_set_text("system", text, strlen(text), err, sizeof err) != STORE_OK;
                uint32_t us = (uint32_t)(now_us() - t0);
                worst = us > worst ? us : worst;
            }
            uint64_t idle = ulTaskGetIdleRunTimeCounter() - idle0, took = now_us() - at0;
            blog("app: stress 1000 saves, %lu failed, slowest %lu us, %lu ms, idle %lu permille\r\n", (unsigned long)fails,
                 (unsigned long)worst, (unsigned long)(took / 1000u), (unsigned long)(idle * 1000u / (took ? took : 1)));
            break;
        }
        case 'W':  // bench: a warning, which syslog keeps in the persistent log
            SLOG_W("bench", "a warning to keep, boot %lu", (unsigned long)store_boot_count());
            break;
        case 'l':  // bench: the persistent log, the last lines
            store_log_iter(print_log_line, NULL);
            break;
        case 'p': {  // bench: save system.hostname = "bench-<uptime ms>" (applies after a restart)
            char text[64], err[160];
            unsigned long ms = (unsigned long)(now_us() / 1000u);
            snprintf(text, sizeof text, "{\"hostname\":\"bench-%lu\"}", ms);
            uint64_t t0 = now_us();
            store_err_t r = store_set_text("system", text, strlen(text), err, sizeof err);
            blog("app: saved bench-%lu: %d in %lu ms %s\r\n", ms, r, (unsigned long)((now_us() - t0) / 1000u), r ? err : "");
            break;
        }
        case 'n':
            net_report();
            break;
        case 'N':
            net_save_line();
            break;
        case 'L':  // bench: the PHY powered down for 3 s, then up (spec §7.1, link polled each second)
            blog("app: phy down %d\r\n", eth_drv_phy_power(true));
            vTaskDelay(pdMS_TO_TICKS(3000));
            blog("app: phy up %d\r\n", eth_drv_phy_power(false));
            break;
        case 'T':
            bench_transact();
            break;
        case 'X': {  // bench: "X<port>": the next write on it sees an XOFF 20 ms in, an XON 1 s later
            char l[4];
            read_line(l, sizeof l);
            int port = l[0] - '0';
            if (port >= 1 && port <= SERIAL_PORTS) {
                uart_drv_inject_xoff(port);
                blog("app: X%d armed\r\n", port);
            }
            break;
        }
        case 'E':
        case 'F': {  // bench: a port as the other's counterpart (bench_serial.c)
            char l[300];
            read_line(l, sizeof l);
            bench_serial_cmd((char)c, l);
            break;
        }
        case 'Z': {  // provisioning: "Z" then "WIPE": erase all of SPI4 (a board that ran another firmware), then reset
            char l[8];
            read_line(l, sizeof l);
            if (strcmp(l, "WIPE") != 0) {
                blog("app: Z needs WIPE\r\n");
                break;
            }
            // A restart already set (an OTA just done) would cut the wipe, and staging installs; once
            // claimed here, no other restart comes before ours.
            if (!sysmgr_restart_claim()) {
                blog("app: a restart is pending: Z refused\r\n");
                break;
            }
            store_port_lock();  // FlashDB is never called again before the reset
            uint64_t t0 = now_us();
            int bad = flash_spi4_wipe();  // also fails every OTA write from here on
            blog("app: SPI4 wiped in %lu ms, %d sectors failed; restarting\r\n", (unsigned long)((now_us() - t0) / 1000u), bad);
            vTaskDelay(pdMS_TO_TICKS(50));
            NVIC_SystemReset();
            break;
        }
        case 'a':
        case 'D':
        case 'J':
            bench_io_cmd((char)c, "");
            break;
        case 'A': {  // bench: "A <g1> <o1> <g2> <o2>": the factory record (bench_io.c)
            char l[80];
            read_line(l, sizeof l);
            bench_io_cmd('A', l);
            break;
        }
        case 'Y': {  // bench: "Y<n>": the free netconns a TCP server needs to accept (spec §5.6, default 4)
            char l[8];
            read_line(l, sizeof l);
            int n = 0;
            sscanf(l, "%d", &n);
            bridge_set_accept_reserve(n);
            blog("app: Y accept reserve %d netconns\r\n", n);
            break;
        }
        case 's': {  // bench: the serial counters of both ports
            for (int port = 1; port <= SERIAL_PORTS; port++) {
                serial_counters_t sc;
                serial_counters(port, &sc);
                blog("app: serial %d tx %lu rx %lu overrun %lu line %lu viol %lu dropped %lu lost %lu\r\n", port,
                     (unsigned long)sc.tx, (unsigned long)sc.rx, (unsigned long)sc.overruns, (unsigned long)sc.line_errors,
                     (unsigned long)sc.violations, (unsigned long)sc.dropped, (unsigned long)sc.lost);
            }
            break;
        }
        case 'u':
        case 'U':
            bench_ota_upload(c == 'u');
            break;
        default:
            break;
        }
    }
}

void app_main(void)
{
    heap_init();  // before any allocation (spec §5.5)
    board_latch_zero();  // MX_GPIO_Init (app.ioc) left the latch outputs disabled
    board_kick();
    blog_init();
    syslog_init();  // before the first line worth keeping
    xTaskCreateStatic(wdog_task, "wdog", WDOG_STACK_WORDS, NULL, 31, s_wdog_stack, &s_wdog_tcb);
    xTaskCreateStatic(init_task, "init", INIT_STACK_WORDS, NULL, 7, s_init_stack, &s_init_tcb);
    vTaskStartScheduler();
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    configASSERT(0);
}
