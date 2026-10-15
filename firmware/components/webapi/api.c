// The non-streamed /api handlers (spec §10.2), kept from the ESP32's api.c. The contract is at the
// top of stm32/web/src/lib/api.ts. Every config applies after a restart (spec §9.1): GET returns what
// is saved, /api/device what runs.
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "heap_newlib.h"
#include "io_core.h"
#include "iomgr.h"
#include "netmgr.h"
#include "serial.h"
#include "store.h"
#include "syslog.h"
#include "sysmgr.h"
#include "task.h"
#include "wdog.h"
#include "web_core.h"
#include "web_priv.h"

#define MODEL "SBIOT-H750"
#define HARDWARE "STM32H750VBT6"

unsigned web_arena_peak(void);
void console_buffer_use(unsigned *used, unsigned *size);

void api_json(struct mg_connection *c, int status, cJSON *doc)
{
    char *s = doc ? cJSON_PrintUnformatted(doc) : NULL;
    cJSON_Delete(doc);
    if (!s) {
        api_error(c, 500, "Out of memory.");
        return;
    }
    mg_http_reply(c, status, "Content-Type: application/json\r\nCache-Control: no-store\r\n", "%s", s);
    cJSON_free(s);
}

void api_error(struct mg_connection *c, int status, const char *message)
{
    // No WWW-Authenticate on 401: the UI shows its own sign-in page (spec §10.1).
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", message);
    char *s = o ? cJSON_PrintUnformatted(o) : NULL;
    cJSON_Delete(o);
    mg_http_reply(c, status, "Content-Type: application/json\r\nCache-Control: no-store\r\n", "%s", s ? s : "{}");
    cJSON_free(s);
}

void api_ok(struct mg_connection *c)
{
    mg_http_reply(c, 200, "Content-Type: application/json\r\nCache-Control: no-store\r\n", "{\"ok\":true}");
}

static bool is(struct mg_http_message *hm, const char *method, const char *uri)
{
    return mg_strcmp(hm->method, mg_str(method)) == 0 && mg_strcmp(hm->uri, mg_str(uri)) == 0;
}

// ---- reads --------------------------------------------------------------------------------------

static void device_get(struct mg_connection *c)
{
    const cfg_env_t *env = store_env();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "model", MODEL);
    cJSON_AddStringToObject(o, "firmware", APP_VERSION);
    cJSON_AddStringToObject(o, "hardware", HARDWARE);
    cJSON_AddNumberToObject(o, "portmax", env->uart_count);
    cJSON_AddNumberToObject(o, "baudMin", 600);
    cJSON_AddNumberToObject(o, "baudMax", 230400);
    cJSON *io = cJSON_AddObjectToObject(o, "io");
    cJSON_AddNumberToObject(io, "do", env->do_count);
    cJSON_AddNumberToObject(io, "di", env->di_count);
    cJSON_AddNumberToObject(io, "ai", env->ai_count);
    cJSON_AddNumberToObject(io, "ao", 0);
    cJSON_AddStringToObject(o, "hostname", g_web.hostname);
    cJSON_AddNumberToObject(o, "wsDirection", g_web.ws_target);
    cJSON_AddNumberToObject(o, "wsPort", g_web.ws_port);
    api_json(c, 200, o);
}

static void ip_str(uint32_t a, char out[16])
{
    if (!a) {
        out[0] = '\0';
        return;
    }
    snprintf(out, 16, "%lu.%lu.%lu.%lu", (unsigned long)(a >> 24), (unsigned long)(a >> 16 & 255),
             (unsigned long)(a >> 8 & 255), (unsigned long)(a & 255));
}

static void add_ip(cJSON *o, const char *k, uint32_t a)
{
    char b[16];
    ip_str(a, b);
    cJSON_AddStringToObject(o, k, b);
}

static void add_pool(cJSON *arr, const char *name, unsigned used, unsigned size)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "name", name);
    cJSON_AddNumberToObject(p, "used", used);
    cJSON_AddNumberToObject(p, "size", size);
    cJSON_AddItemToArray(arr, p);
}

// Whether a saved config enables its service: `enabled` is 1 (mqtt) or true (edge).
static bool enabled(const char *name)
{
    cJSON *o = store_get(name);
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, "enabled");
    bool r = cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valueint == 1);
    cJSON_Delete(o);
    return r;
}

static void status_get(struct mg_connection *c)
{
    static net_status_t ns;  // the web task only
    netmgr_status(&ns);
    char t[20], mac[18];
    sysmgr_time_str(t);
    snprintf(mac, sizeof mac, "%02X:%02X:%02X:%02X:%02X:%02X", ns.mac[0], ns.mac[1], ns.mac[2], ns.mac[3], ns.mac[4], ns.mac[5]);
    cJSON *o = cJSON_CreateObject();
    cJSON *sys = cJSON_AddObjectToObject(o, "system");
    cJSON_AddNumberToObject(sys, "uptime", sysmgr_uptime_s());
    cJSON_AddStringToObject(sys, "os", "FreeRTOS " tskKERNEL_VERSION_NUMBER " + lwIP");
    cJSON_AddStringToObject(sys, "mac", mac);
    cJSON_AddStringToObject(sys, "imei", "");
    cJSON_AddStringToObject(sys, "sn", sysmgr_serial());
    cJSON_AddStringToObject(sys, "activeNet", ns.bound && ns.eth.up ? "ETH" : "NONE");
    cJSON_AddStringToObject(sys, "time", t);  // "" while the clock is not set (spec §10.4)
    cJSON_AddNumberToObject(sys, "heapFree", heap_free_now());
    cJSON_AddNumberToObject(sys, "heapMin", heap_free_min());
    cJSON_AddNumberToObject(sys, "psramFree", 0);
    cJSON_AddNumberToObject(sys, "wdiMaxGapMs", wdog_max_gap_us() / 1000u);

    cJSON *eth = cJSON_AddObjectToObject(o, "eth");
    add_ip(eth, "ip", ns.ip);
    add_ip(eth, "mask", ns.mask);
    add_ip(eth, "gateway", ns.gw);
    add_ip(eth, "dns1", ns.dns1);
    add_ip(eth, "dns2", ns.dns2);
    cJSON *cell = cJSON_AddObjectToObject(o, "cell");  // LTE: Plan 10
    cJSON_AddStringToObject(cell, "iccid", "");
    cJSON_AddStringToObject(cell, "ip", "");
    cJSON_AddStringToObject(cell, "dns1", "");
    cJSON_AddStringToObject(cell, "dns2", "");
    cJSON_AddNumberToObject(cell, "signal", 99);
    cJSON_AddStringToObject(cell, "netType", "");
    cJSON_AddStringToObject(cell, "state", "disconnected");

    cJSON *ports = cJSON_AddArrayToObject(o, "ports");  // the bridge (the state): Plan 08
    for (int p = 1; p <= store_env()->uart_count; p++) {
        serial_counters_t sc;
        serial_counters(p, &sc);
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "state", "NOT STARTED");
        cJSON_AddNumberToObject(x, "tx", sc.tx);
        cJSON_AddNumberToObject(x, "rx", sc.rx);
        cJSON_AddItemToArray(ports, x);
    }
    cJSON *mq = cJSON_AddObjectToObject(o, "mqtt");  // Plan 09
    cJSON_AddBoolToObject(mq, "enabled", enabled("mqtt"));
    cJSON_AddStringToObject(mq, "state", "NOT STARTED");
    cJSON *ed = cJSON_AddObjectToObject(o, "edge");  // Plan 08
    cJSON_AddBoolToObject(ed, "enabled", enabled("edge"));
    cJSON_AddStringToObject(ed, "state", "NOT STARTED");

    // spec §10.2: the memory picture
    cJSON *mem = cJSON_AddObjectToObject(o, "mem");
    cJSON *heap = cJSON_AddObjectToObject(mem, "heap");
    cJSON_AddNumberToObject(heap, "free", heap_free_now());
    cJSON_AddNumberToObject(heap, "min", heap_free_min());
    cJSON_AddNumberToObject(mem, "arenaPeak", web_arena_peak());
    cJSON *pools = cJSON_AddArrayToObject(mem, "pools");
    add_pool(pools, "lwip_heap", ns.heap.max, ns.heap.size);
    add_pool(pools, "netconn", ns.netconn.max, ns.netconn.size);
    add_pool(pools, "tcp_pcb", ns.tcp_pcb.max, ns.tcp_pcb.size);
    add_pool(pools, "tcp_seg", ns.tcp_seg.max, ns.tcp_seg.size);
    add_pool(pools, "pbuf_pool", ns.pbuf_pool.max, ns.pbuf_pool.size);
    add_pool(pools, "eth_rx", ns.eth.rx_in_use, 16);
    unsigned used = 0, size = 0;
    console_buffer_use(&used, &size);
    add_pool(pools, "console_log", used, size);
    // The lowest stack left of any task, and whose.
    TaskStatus_t ts[24];
    UBaseType_t n = uxTaskGetSystemState(ts, sizeof ts / sizeof ts[0], NULL);
    const char *worst = "";
    unsigned words = 0xFFFFFFFFu;
    for (UBaseType_t i = 0; i < n; i++)
        if (ts[i].usStackHighWaterMark < words) {
            words = ts[i].usStackHighWaterMark;
            worst = ts[i].pcTaskName;
        }
    cJSON *st = cJSON_AddObjectToObject(mem, "stackMin");
    cJSON_AddStringToObject(st, "task", worst);
    cJSON_AddNumberToObject(st, "words", n ? words : 0);
    io_status_t ios;
    iomgr_status(&ios);
    cJSON_AddBoolToObject(o, "aiCalibrated", ios.ai_calibrated);
    cJSON *adc = cJSON_AddObjectToObject(mem, "adc");
    cJSON_AddNumberToObject(adc, "irqs", ios.adc_irqs);
    cJSON_AddNumberToObject(adc, "late", ios.adc_late);
    cJSON_AddNumberToObject(adc, "errors", ios.adc_errors);
    cJSON_AddNumberToObject(adc, "recoveries", ios.adc_recoveries);
    cJSON *uart = cJSON_AddArrayToObject(mem, "uart");
    for (int p = 1; p <= store_env()->uart_count; p++) {
        serial_counters_t sc;
        serial_counters(p, &sc);
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "overrun", sc.overruns);
        cJSON_AddNumberToObject(x, "dropped", sc.dropped);
        cJSON_AddNumberToObject(x, "lineErrors", sc.line_errors);
        cJSON_AddNumberToObject(x, "t15Violations", sc.violations);
        cJSON_AddItemToArray(uart, x);
    }
    api_json(c, 200, o);
}

// ---- config -------------------------------------------------------------------------------------

static void config_get(struct mg_connection *c, const char *name)
{
    char *s = store_get_json(name);
    if (!s) {
        api_error(c, 500, "Out of memory.");
        return;
    }
    mg_http_reply(c, 200, "Content-Type: application/json\r\nCache-Control: no-store\r\n", "%s", s);
    free(s);
}

static void config_post(struct mg_connection *c, const char *name, struct mg_http_message *hm)
{
    char err[160];
    store_err_t e = store_set_text(name, hm->body.buf, hm->body.len, err, sizeof err);
    if (e == STORE_OK)
        api_ok(c);
    else
        api_error(c, e == STORE_EINVALID ? 400 : e == STORE_ETOOLARGE ? 413 : e == STORE_ENOTFOUND ? 404 : 500,
                  err[0] ? err : "The settings could not be saved.");
}

// ---- actions ------------------------------------------------------------------------------------

static void time_post(struct mg_connection *c, struct mg_http_message *hm)
{
    cJSON *b = cJSON_ParseWithLength(hm->body.buf, hm->body.len);
    const char *t = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(b, "time"));
    int r = sysmgr_set_local(t);
    cJSON_Delete(b);
    if (r)
        api_error(c, 400, "Send {\"time\": \"YYYY-MM-DD hh:mm:ss\"}, a real local date from 2024 to 2099.");
    else
        api_ok(c);
}

// {"do": index, "value": true|false|0|1} (as on the ESP32).
static void io_post(struct mg_connection *c, struct mg_http_message *hm)
{
    cJSON *b = cJSON_ParseWithLength(hm->body.buf, hm->body.len);
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(b, "do"), *v = cJSON_GetObjectItemCaseSensitive(b, "value");
    bool ao = cJSON_GetObjectItemCaseSensitive(b, "ao") != NULL;
    int r = -1;
    int i = cJSON_IsNumber(d) ? io_index(d->valuedouble, IO_DO) : -1;  // never truncated: 1.9 is no output
    if (!ao && i >= 0 && (cJSON_IsBool(v) || cJSON_IsNumber(v)))
        r = iomgr_do_set(i, cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valuedouble != 0));
    cJSON_Delete(b);
    if (ao)
        api_error(c, 400, "This gateway has no analog outputs.");
    else if (r)
        api_error(c, 400, "Send {\"do\": index, \"value\": true|false} with an existing output.");
    else
        api_ok(c);
}

void api_handle(struct mg_connection *c, struct mg_http_message *hm)
{
    char name[16];
    if (is(hm, "GET", "/api/device"))
        device_get(c);
    else if (is(hm, "GET", "/api/status"))
        status_get(c);
    else if (web_config_name(hm->uri.buf, hm->uri.len, name, sizeof name)) {
        bool get = mg_strcmp(hm->method, mg_str("GET")) == 0, post = mg_strcmp(hm->method, mg_str("POST")) == 0;
        if (!store_known(name) || (!get && !post))
            api_error(c, 404, "Unknown settings.");
        else if (get)
            config_get(c, name);
        else
            config_post(c, name, hm);
    } else if (is(hm, "GET", "/api/io"))
        api_json(c, 200, iomgr_state_json());
    else if (is(hm, "POST", "/api/io"))
        io_post(c, hm);
    else if (is(hm, "POST", "/api/time"))
        time_post(c, hm);
    else if (is(hm, "POST", "/api/restart")) {
        SLOG_W("web", "restart requested from the web");
        api_ok(c);
        sysmgr_restart_later(800);
    } else if (is(hm, "POST", "/api/factory-reset")) {
        SLOG_W("web", "factory reset requested from the web");
        store_err_t e = store_factory_reset();
        if (e == STORE_OK)
            api_ok(c);
        else
            api_error(c, 500, "Some settings could not be erased.");
        sysmgr_restart_later(1500);
    } else
        api_error(c, 404, "Unknown API endpoint.");
}
