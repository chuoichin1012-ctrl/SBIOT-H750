// The Edge runtime (spec §9, Plan 08c): the ESP32's edge_rt.c on both RS485 ports.
//
//  - Polling: Modbus RTU and DL/T645 on UART1/UART2 (serial_transact), up to 5 Modbus TCP devices, the
//    gateway's own IO, and virtual (formula) points.
//  - Report: the JSON template through a port's socket (bridge_send); periodic, scheduled, on change.
//    Channel 0 (MQTT) comes with Plan 09: Edge then runs without sending reports.
//  - Remote access: Modbus RTU / Modbus TCP on the mapped addresses, or rw_prot JSON, on that port's socket.
//  - Linkage: conditions on points drive the digital outputs.
//
// Tasks, split by what they wait on (a slow device never delays control): edge_bus1/2 poll their port's
// devices in list order; edge_tcpN one Modbus TCP device each; edge_ctl IO, virtual points and linkage,
// never waiting on I/O; edge_rpt the reports. The model loads one node at a time (cfg_edge_load), and all
// of Edge's memory comes from the heap at boot, all or nothing (Plan 08c rulings 2 and 6): any failure
// leaves Edge off with no task and no hook. Point values live in one cache under s_cache, held only while
// values are copied, never during I/O; lock order: bus, cache, store (spec §6.4), every wait timed out.
#include "edge_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "arena.h"
#include "bridge.h"
#include "cfgmodel.h"
#include "edge_core.h"
#include "io_mb.h"
#include "iomgr.h"
#include "modbus.h"
#include "netmgr.h"
#include "now_us.h"
#include "rwprot.h"
#include "semphr.h"
#include "serial.h"
#include "store.h"
#include "svc_net.h"
#include "syslog.h"
#include "sysmgr.h"
#include "task.h"
#include "webapi.h"

#define REPORT_MAX (26 * 1024)  // spec §9.4
#define LOCK_MS 2000u           // every wait on a lock (spec §6.4)
#define BUS_STACK 6144
#define TCP_STACK 5120
#define CTL_STACK 4096
#define RPT_STACK 4096
#define PRIO_POLL 5
#define PRIO_CTL 6
#define PRIO_RPT 4
enum { FMT_RTU, FMT_TCP, FMT_JSON };

typedef struct {  // a Modbus TCP device's connection, shared by its poll task and remote writes
    conn_t c;
    uint16_t tid;
    SemaphoreHandle_t lock;
} tcp_link_t;

typedef struct {
    int channel;
    bool remote, remote_write;
    int format;
    bool reporting, periodic, scheduled, placeholder_on, quote;
    int interval_s, schedule, hour, minute;
    char placeholder[17];
} report_cfg_t;

typedef struct {  // a poll task's devices
    edge_dev_t **devs;
    int n;
} dev_list_t;

static edge_mem_t M;
static edge_dev_t *s_devs;
static int s_ndev, s_npts, s_nlinks;
static edge_index_t s_index;
static edge_tmpl_t *s_tmpl;
static char *s_report;  // the rendered report (heap, sized once from the template)
static size_t s_report_max;
static tcp_link_t *s_tcp;  // one per Modbus TCP device
static dev_list_t s_lists[EDGE_MAX_TASKS];
static report_cfg_t R;
static SemaphoreHandle_t s_cache;
static TaskHandle_t s_rpt_task;
static volatile bool s_running;
static const char *s_note = "NOT RUNNING";  // why Edge does not run, or a note on how it runs
static char s_mac[18];

static int64_t now_ms(void) { return (int64_t)(now_us() / 1000u); }

static bool cache_lock(void)
{
    if (xSemaphoreTake(s_cache, pdMS_TO_TICKS(LOCK_MS)) == pdTRUE)
        return true;
    SLOG_E("edge", "the value cache stayed locked for %u ms", (unsigned)LOCK_MS);
    return false;
}
static void cache_unlock(void) { xSemaphoreGive(s_cache); }

static void report_soon(void)
{
    if (s_rpt_task)
        xTaskNotifyGive(s_rpt_task);
}

// ---- loading ------------------------------------------------------------------------------------

typedef struct {
    int ndev, npts, neth, nbus[SERIAL_PORTS + 1], nlocal;
    bool fill;
} load_t;

static bool on_dev(void *ctx, const cJSON *d, int index)
{
    load_t *l = ctx;
    if (l->fill) {
        edge_dev_t *v = &s_devs[index];
        edge_dev_from_json(d, v);
        v->pts = &M.pts[l->npts];
    } else {
        const char *src = cJSON_GetStringValue(cJSON_GetObjectItem(d, "source"));
        int uart = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(d, "uart"));
        if (src && !strcmp(src, "eth"))
            l->neth++;
        else if (src && (!strcmp(src, "io") || !strcmp(src, "virtual")))
            l->nlocal++;
        else if (uart >= 1 && uart <= SERIAL_PORTS)
            l->nbus[uart]++;
    }
    l->ndev++;
    return true;
}

static bool on_node(void *ctx, const cJSON *n, int dev)
{
    load_t *l = ctx;
    if (l->fill) {
        edge_pt_from_json(n, &s_devs[dev], &M.pts[l->npts]);
        s_devs[dev].npts++;
    }
    l->npts++;
    return true;
}

static bool walk(load_t *l, const char *text, size_t len)
{
    char err[160];
    unsigned before = json_alloc_failures();
    cfg_result_t r = cfg_edge_load(store_env(), text, len, on_dev, on_node, l, err, sizeof err);
    if (r != CFG_OK || json_alloc_failures() != before) {
        SLOG_E("edge", "the saved model does not load: %s", r == CFG_NO_MEMORY ? "out of memory" : err);
        return false;
    }
    return true;
}

static int num(const cJSON *o, const char *k) { return (int)cJSON_GetNumberValue(cJSON_GetObjectItem(o, k)); }
static bool flag(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valuedouble != 0);
}
static const char *text(const cJSON *o, const char *k)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(o, k));
    return s ? s : "";
}

static void read_report(const cJSON *r)
{
    R.channel = num(r, "channel");
    R.remote = num(r, "remoteAccess") == 1;
    R.remote_write = flag(r, "remoteWrite");
    R.format = num(r, "remoteFormat");
    R.reporting = num(r, "reporting") == 1;
    R.periodic = flag(r, "periodic");
    R.interval_s = num(r, "interval");
    R.scheduled = flag(r, "scheduled");
    R.schedule = num(r, "schedule");
    R.hour = num(r, "scheduleHour");
    R.minute = num(r, "scheduleMinute");
    R.placeholder_on = flag(r, "failurePlaceholder");
    snprintf(R.placeholder, sizeof R.placeholder, "%s", text(r, "placeholder"));
    R.quote = flag(r, "quoteValues");
}

/** "DO2" -> index 1; -1 when it is not an output of this gateway. */
static int do_index(const char *s)
{
    int i = !strncmp(s, "DO", 2) ? atoi(s + 2) - 1 : -1;
    return i >= 0 && i < store_env()->do_count ? i : -1;
}

static void fill_links(const cJSON *links)
{
    const cJSON *e;
    cJSON_ArrayForEach(e, links)
    {
        edge_link_t *l = &M.links[s_nlinks];
        memset(l, 0, sizeof *l);
        l->pt = edge_index_get(&s_index, text(e, "point"));
        l->out = do_index(text(e, "output"));
        if (!l->pt || l->out < 0) {
            SLOG_W("edge", "linkage %s: unknown point or output, skipped", text(e, "name"));
            continue;
        }
        snprintf(l->name, sizeof l->name, "%s", text(e, "name"));
        snprintf(l->desc, sizeof l->desc, "%s", text(e, "desc"));
        l->enable = flag(e, "enable");
        l->cond = num(e, "cond");
        l->cycle_ms = num(e, "cycle");
        l->min_ms = num(e, "minInterval");
        l->lower = cJSON_GetNumberValue(cJSON_GetObjectItem(e, "lower"));
        l->upper = cJSON_GetNumberValue(cJSON_GetObjectItem(e, "upper"));
        l->action = num(e, "action");
        s_nlinks++;
    }
}

/** Formula references, once every point is in the index. */
static void resolve_formulas(void)
{
    for (int i = 0; i < s_npts; i++) {
        edge_pt_t *p = &M.pts[i];
        if (!p->expr[0])
            continue;
        char expr[65], names[EDGE_FORMULA_REFS][EDGE_NAME_MAX];
        int n = edge_formula_split(p->expr, expr, sizeof expr, names, EDGE_FORMULA_REFS);
        snprintf(p->expr, sizeof p->expr, "%s", expr);
        for (int k = 0; k < n; k++) {
            p->refs[k] = edge_index_get(&s_index, names[k]);
            if (!p->refs[k])
                SLOG_W("edge", "%s: formula names an unknown point %s", p->name, names[k]);
        }
        p->nrefs = n;
    }
}

// ---- reading devices ------------------------------------------------------------------------------

#define MB_NO_LINK (-2)  // Modbus TCP: no connection; the rest of the device's points fail this round

/** One Modbus TCP exchange on d's connection (its lock held): unit + PDU out with a fresh transaction id,
 * the matching reply back (stale ones skipped, edge_tcp_take). Its length, -1, or MB_NO_LINK. */
static int tcp_exchange(edge_dev_t *d, const uint8_t *unit_pdu, size_t len, uint8_t *out, size_t max, int timeout_ms)
{
    tcp_link_t *t = d->link;
    if (t->c.fd < 0) {
        t->c.fd = net_tcp_connect(d->ip, d->port, 0, (uint32_t)timeout_ms + 1000);
        if (t->c.fd < 0)
            return MB_NO_LINK;
    }
    uint8_t buf[2 * MBTCP_MAX];
    for (;;) {  // whatever is already there is late: drop it
        int r = conn_recv(&t->c, buf, sizeof buf);
        if (r == NET_AGAIN)
            break;
        if (r <= 0) {
            conn_close(&t->c);
            return MB_NO_LINK;
        }
    }
    uint8_t adu[MBTCP_MAX];
    uint16_t tid = ++t->tid;
    size_t n = mb_tcp_wrap(unit_pdu, len, tid, adu, sizeof adu);
    if (!n || !conn_send_all(&t->c, adu, n, (uint32_t)timeout_ms)) {
        conn_close(&t->c);
        return MB_NO_LINK;
    }
    size_t got = 0;
    int64_t end = now_ms() + timeout_ms;
    while (now_ms() < end) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(t->c.fd, &rd);
        struct timeval tv = {.tv_sec = 0, .tv_usec = 20000};
        if (lwip_select(t->c.fd + 1, &rd, NULL, NULL, &tv) <= 0)
            continue;
        int r = conn_recv(&t->c, buf + got, sizeof buf - got);
        if (r == NET_AGAIN)
            continue;
        if (r <= 0) {
            conn_close(&t->c);
            return -1;
        }
        got += (size_t)r;
        int body = edge_tcp_take(buf, &got, tid, unit_pdu[0], unit_pdu[1], out, max);
        if (body != 0) {
            if (body < 0)
                conn_close(&t->c);  // not Modbus TCP: the stream is unusable
            return body;
        }
        if (got == sizeof buf)
            got = 0;
    }
    return -1;
}

/** One Modbus read (RTU on the device's port, or TCP): the reply's data length, -1, or MB_NO_LINK. */
static int modbus_read(edge_dev_t *d, int fc, uint16_t start, uint16_t count, int timeout_ms, uint8_t *data)
{
    uint8_t req[12], resp[MBTCP_MAX];
    int got;
    size_t n = mb_rtu_read(req, d->unit, (uint8_t)fc, start, count);
    if (d->source == EDGE_SRC_UART) {
        got = serial_transact(d->uart, req, n, resp, sizeof resp, d->unit, (uint32_t)timeout_ms);
        if (got < 5 || !mb_rtu_valid(resp, (size_t)got))
            return -1;
        got -= 2;
    } else {
        tcp_link_t *t = d->link;
        if (xSemaphoreTake(t->lock, pdMS_TO_TICKS(LOCK_MS)) != pdTRUE) {
            SLOG_E("edge", "%s: the connection stayed locked", d->name);
            return -1;
        }
        got = tcp_exchange(d, req, 6, resp, sizeof resp, timeout_ms);
        xSemaphoreGive(t->lock);
        if (got < 0)
            return got;
    }
    if (resp[0] != d->unit || resp[1] != fc || got < 3 || resp[2] + 3 > got)
        return -1;  // an exception, or garbage
    memcpy(data, resp + 3, resp[2]);
    return resp[2];
}

static void store_value(edge_pt_t *p, double raw)
{
    double v;
    bool due = false;
    if (edge_apply_formula(p, raw, &v))
        due = edge_point_update(p, v);
    else
        p->valid = false;
    if (due)
        report_soon();
}

static void poll_modbus(edge_dev_t *d)
{
    edge_pt_t *order[d->npts ? d->npts : 1];
    edge_batch_t batches[d->npts ? d->npts : 1];
    for (int i = 0; i < d->npts; i++)
        order[i] = &d->pts[i];
    int nb = edge_batches(d, order, batches, d->npts);
    for (int k = 0; k < nb; k++) {
        edge_batch_t *b = &batches[k];
        uint8_t data[256];
        int n = modbus_read(d, b->fc, b->start, b->qty, b->timeout_ms, data);
        if (!cache_lock())
            return;
        int last = n == MB_NO_LINK ? d->npts : b->first + b->count;  // no connection: the rest fail now
        for (int i = b->first; i < last; i++) {
            edge_pt_t *p = order[i];
            uint16_t regs[4];
            if (n >= 0 && i < b->first + b->count && edge_batch_take(p, b, data, n, regs)) {
                memcpy(p->raw, regs, (size_t)(b->fc <= 2 ? 1 : edge_dtype_regs(p->dtype)) * sizeof regs[0]);
                store_value(p, edge_decode(p->dtype, p->raw));
            } else {
                p->valid = false;
            }
        }
        cache_unlock();
        if (n == MB_NO_LINK)
            return;
    }
}

static void poll_dlt645(edge_dev_t *d)
{
    for (int i = 0; i < d->npts; i++) {
        edge_pt_t *p = &d->pts[i];
        uint8_t req[24], resp[128];
        size_t n = dlt645_read_req(d->addr, p->di, req, sizeof req);
        int got = n ? serial_transact(d->uart, req, n, resp, sizeof resp, -1, (uint32_t)p->timeout_ms) : -1;
        double raw;
        bool ok = got > 0 && dlt645_parse(resp, (size_t)got, d->addr, p->di, &raw) == 0;
        if (!cache_lock())
            return;
        if (ok)
            store_value(p, raw);
        else
            p->valid = false;
        cache_unlock();
    }
}

static void poll_io(edge_dev_t *d)
{
    if (!cache_lock())
        return;
    for (int i = 0; i < d->npts; i++) {
        edge_pt_t *p = &d->pts[i];
        if (p->io_kind == EDGE_IO_AI && !iomgr_ai_ok(p->io_index)) {  // a failed ADC read is not a 0 µA reading
            p->valid = false;
            continue;
        }
        double raw = p->io_kind == EDGE_IO_DI   ? iomgr_di_get(p->io_index)
                     : p->io_kind == EDGE_IO_DO ? iomgr_do_get(p->io_index)
                                                : iomgr_ai_ua(p->io_index);
        // Its Modbus registers are the IO map's: a bit, or the AI as float32 ABCD in mA.
        if (p->io_kind == EDGE_IO_AI)
            edge_encode(DT_FLOAT32_ABCD, raw / 1000.0, p->raw);
        else
            p->raw[0] = raw != 0;
        store_value(p, raw);
    }
    cache_unlock();
}

static void poll_virtual(edge_dev_t *d)
{
    if (!cache_lock())
        return;
    for (int i = 0; i < d->npts; i++)
        store_value(&d->pts[i], 0);
    cache_unlock();
}

static void poll_device(edge_dev_t *d)
{
    switch (d->source) {
    case EDGE_SRC_UART:
        if (d->dlt645)
            poll_dlt645(d);
        else
            poll_modbus(d);
        break;
    case EDGE_SRC_ETH: poll_modbus(d); break;
    case EDGE_SRC_IO: poll_io(d); break;
    default: poll_virtual(d); break;
    }
}

// ---- writing points (remote access) ---------------------------------------------------------------

/** Writes a value to the device behind a point (raw: no inverse formula); a read-back follows. */
static bool write_point(edge_pt_t *p, double v)
{
    edge_dev_t *d = p->dev;
    if (d->source == EDGE_SRC_IO)
        return p->io_kind == EDGE_IO_DO && iomgr_do_set(p->io_index, v != 0) == 0;
    uint8_t rtu[MBTCP_MAX], resp[MBTCP_MAX];
    size_t n = edge_write_req(p, v, rtu, sizeof rtu);
    if (!n)
        return false;  // read-only
    int timeout = p->timeout_ms > 0 ? p->timeout_ms : 500;
    bool ok;
    if (d->source == EDGE_SRC_UART) {  // serial_transact holds the bus
        int got = serial_transact(d->uart, rtu, n, resp, sizeof resp, d->unit, (uint32_t)timeout);
        ok = edge_write_echo_ok(rtu, resp, got, true);
    } else {
        tcp_link_t *t = d->link;
        if (xSemaphoreTake(t->lock, pdMS_TO_TICKS(LOCK_MS)) != pdTRUE)
            return false;
        int got = tcp_exchange(d, rtu, n - 2, resp, sizeof resp, timeout);
        xSemaphoreGive(t->lock);
        ok = edge_write_echo_ok(rtu, resp, got, false);
    }
    if (ok)
        d->poll_now = true;  // read back soon
    return ok;
}

// ---- remote access: Modbus ---------------------------------------------------------------------------

static uint8_t map_read_bit(void *ctx, uint8_t fc, uint16_t addr, bool *out)
{
    int off;
    edge_pt_t *p = edge_point_at(ctx, fc, addr, &off);
    if (!p)
        return MB_EX_ILLEGAL_ADDRESS;
    if (!cache_lock())
        return MB_EX_DEVICE_FAILURE;
    bool ok = p->valid;
    *out = p->value != 0;
    cache_unlock();
    return ok ? 0 : MB_EX_TARGET_NO_RESPONSE;  // it exists, but its last read failed (or none yet)
}

static uint8_t map_read_reg(void *ctx, uint8_t fc, uint16_t addr, uint16_t *out)
{
    int off;
    edge_pt_t *p = edge_point_at(ctx, fc, addr, &off);
    if (!p)
        return MB_EX_ILLEGAL_ADDRESS;
    if (!cache_lock())
        return MB_EX_DEVICE_FAILURE;
    bool ok = p->valid;
    *out = p->raw[off];
    cache_unlock();
    return ok ? 0 : MB_EX_TARGET_NO_RESPONSE;
}

static uint8_t map_write_bit(void *ctx, uint16_t addr, bool on)
{
    int off;
    edge_pt_t *p = edge_point_at(ctx, 1, addr, &off);
    if (!p || !edge_writable(p))
        return MB_EX_ILLEGAL_ADDRESS;
    return write_point(p, on) ? 0 : MB_EX_TARGET_NO_RESPONSE;
}

/** FC6: a one-register point only (a 32/64-bit value needs all its registers: FC16). */
static uint8_t map_write_reg(void *ctx, uint16_t addr, uint16_t value)
{
    int off;
    edge_pt_t *p = edge_point_at(ctx, 3, addr, &off);
    if (!p || !edge_writable(p) || edge_dtype_regs(p->dtype) != 1)
        return MB_EX_ILLEGAL_ADDRESS;
    return write_point(p, edge_decode(p->dtype, &value)) ? 0 : MB_EX_TARGET_NO_RESPONSE;
}

/** FC16: the block must cover whole, writable points; all of it is checked before anything is written. */
static uint8_t map_write_regs(void *ctx, uint16_t addr, const uint16_t *values, uint16_t count)
{
    for (int pass = 0; pass < 2; pass++) {  // 0: check everything, 1: write
        for (uint16_t i = 0; i < count;) {
            int off;
            edge_pt_t *p = edge_point_at(ctx, 3, (uint16_t)(addr + i), &off);
            int w = p ? edge_dtype_regs(p->dtype) : 0;
            if (!p || !edge_writable(p) || off != 0 || i + w > count)
                return MB_EX_ILLEGAL_ADDRESS;
            if (pass == 1 && !write_point(p, edge_decode(p->dtype, values + i)))
                return MB_EX_TARGET_NO_RESPONSE;
            i = (uint16_t)(i + w);
        }
    }
    return 0;
}

static bool map_exists(void *ctx, uint8_t fc, uint16_t addr)
{
    int off;
    edge_pt_t *p = edge_point_at(ctx, fc, addr, &off);
    return p && edge_writable(p);
}

static edge_dev_t *device_for(uint8_t unit) { return edge_device_for_unit(s_devs, s_ndev, unit, io_mb_address()); }

/** unit + PDU -> unit + reply PDU, from the mapped points. */
static size_t serve_mapped(edge_dev_t *d, const uint8_t *req, size_t len, uint8_t *out, size_t max)
{
    // Without remoteWrite, FC5/6/15/16 get "illegal function".
    const mb_map_t map = {.read_bit = map_read_bit, .read_reg = map_read_reg,
                          .write_bit = R.remote_write ? map_write_bit : NULL,
                          .write_reg = R.remote_write ? map_write_reg : NULL,
                          .write_regs = R.remote_write ? map_write_regs : NULL, .exists = map_exists, .ctx = d};
    return mb_serve(&map, req, len, out, max);
}

// ---- remote access: rw_prot JSON -------------------------------------------------------------------

static bool rw_read(void *ctx, const char *name, char *value, size_t max)
{
    (void)ctx;
    const edge_pt_t *p = edge_index_get(&s_index, name);
    if (!p || !cache_lock())
        return false;
    bool ok = p->valid;
    double v = p->value;
    cache_unlock();
    if (ok)
        edge_format(v, value, max);
    return ok;
}

static bool rw_write(void *ctx, const char *name, const char *value)
{
    (void)ctx;
    edge_pt_t *p = edge_index_get(&s_index, name);
    char *end;
    double v = strtod(value, &end);
    return p && end != value && *end == '\0' && write_point(p, v);
}

/** A request in the configured format -> the reply; 0 when it is not Edge's (it goes on down the bridge). */
static size_t remote(const uint8_t *req, size_t len, uint8_t *reply, size_t max)
{
    if (R.format == FMT_JSON) {
        char *s = rwprot_handle((const char *)req, len, rw_read, R.remote_write ? rw_write : NULL, NULL);
        if (!s)
            return 0;
        size_t n = strlen(s);
        if (n > max)
            n = 0;
        memcpy(reply, s, n);
        free(s);
        return n;
    }
    if (R.format == FMT_RTU) {
        edge_dev_t *d = len >= 4 && mb_rtu_valid(req, len) ? device_for(req[0]) : NULL;
        if (!d || max < 2)
            return 0;
        size_t n = serve_mapped(d, req, len - 2, reply, max - 2);
        return n ? mb_rtu_seal(reply, n) : 0;
    }
    int fl = mb_tcp_frame_len(req, len);
    edge_dev_t *d = fl > 6 ? device_for(req[6]) : NULL;
    if (!d)
        return 0;
    uint8_t pdu[MBTCP_MAX];
    size_t n = serve_mapped(d, req + 6, (size_t)fl - 6, pdu, sizeof pdu);
    return n ? mb_tcp_wrap(pdu, n, (uint16_t)(req[0] << 8 | req[1]), reply, max) : 0;
}

// ---- report -------------------------------------------------------------------------------------------

static char s_sys_handle[EDGE_SYS_FIELDS];  // their addresses are the slot handles of system fields

/** Compile time: a template string -> a point, a system field, or NULL for a constant. */
static void *resolve(void *ctx, const char *name)
{
    edge_index_t *ix = ctx;
    edge_pt_t *p = edge_index_get(ix, name);
    if (p)
        return p;
    int f = edge_sys_field(name);
    return f >= 0 ? &s_sys_handle[f] : NULL;
}

/** Report time: the value behind a slot. Cache lock held: one consistent snapshot. */
static edge_kind_t slot_value(void *ctx, void *slot, double *v, const char **out_text)
{
    (void)ctx;
    static char buf[20];
    char *s = slot;
    if (!(s >= s_sys_handle && s < s_sys_handle + EDGE_SYS_FIELDS)) {
        const edge_pt_t *p = slot;
        *v = p->value;
        return p->valid ? EDGE_NUMBER : EDGE_NO_VALUE;
    }
    return edge_sys_value((int)(s - s_sys_handle), sysmgr_utc_ms(), sysmgr_serial(), s_mac, APP_VERSION, v, out_text, buf);
}

static void report(const char *why)
{
    static int64_t s_warned;
    if (!s_tmpl || !s_report || !cache_lock())
        return;
    size_t n = edge_tmpl_render(s_tmpl, slot_value, NULL, s_report, s_report_max);
    for (int i = 0; n && i < s_npts; i++) {
        M.snap_value[i] = M.pts[i].value;
        M.snap_valid[i] = M.pts[i].valid;
    }
    cache_unlock();
    if (!n) {
        SLOG_W("edge", "report (%s) did not fit in %u bytes", why, (unsigned)s_report_max);
        return;
    }
    bool ok = R.channel > 0 && bridge_send(R.channel, (const uint8_t *)s_report, n);
    if (!ok) {
        // The baselines stay: a change not delivered is still a change, reported again on the next poll.
        if (now_ms() - s_warned > 60000 || !s_warned) {
            s_warned = now_ms();
            if (R.channel == 0)
                SLOG_W("edge", "report (%s) not sent: reports over MQTT come with Plan 09", why);
            else
                SLOG_W("edge", "report (%s) not sent: UART%d's socket has no peer", why, R.channel);
        }
        return;
    }
    if (!cache_lock())
        return;
    for (int i = 0; i < s_npts; i++) {
        M.pts[i].reported = M.snap_value[i];
        M.pts[i].reported_valid = M.snap_valid[i];
    }
    cache_unlock();
}

// ---- tasks ----------------------------------------------------------------------------------------------

/** Polls `l`'s devices on their own intervals, in list order; sleeps until the next one is due. */
static void poll_task(void *arg)
{
    dev_list_t *l = arg;
    for (;;) {
        int64_t now = now_ms(), next = now + 50;  // 50 ms: read-backs after remote writes come promptly
        for (int i = 0; i < l->n; i++) {
            edge_dev_t *d = l->devs[i];
            if (now >= d->next_poll || d->poll_now) {
                d->poll_now = false;
                d->next_poll = now + d->poll_ms;
                poll_device(d);
                now = now_ms();
            }
            if (d->next_poll < next)
                next = d->next_poll;
        }
        int64_t wait = next - now_ms();
        vTaskDelay(pdMS_TO_TICKS(wait < 1 ? 1 : wait > 50 ? 50 : wait));
    }
}

static void run_linkage(void)
{
    int64_t now = now_ms();
    for (int i = 0; i < s_nlinks; i++) {
        edge_link_t *l = &M.links[i];
        if (!cache_lock())
            return;
        bool valid = l->pt->valid;
        double value = l->pt->value;
        cache_unlock();
        bool fired;
        int act = edge_link_step(l, now, valid, value, iomgr_do_get(l->out), &fired);
        if (act != EDGE_DO_KEEP)
            iomgr_do_set(l->out, act == EDGE_DO_ON);
        if (fired)
            SLOG_I("edge", "linkage %s fired (%s = %g)%s%s", l->name, l->pt->name, value, l->desc[0] ? ": " : "", l->desc);
    }
}

/** edge_ctl: IO and virtual points and linkage, every 10 ms; never waits on a device. */
static void ctl_task(void *arg)
{
    dev_list_t *l = arg;
    for (;;) {
        int64_t now = now_ms();
        for (int i = 0; i < l->n; i++) {
            edge_dev_t *d = l->devs[i];
            if (now >= d->next_poll || d->poll_now) {
                d->poll_now = false;
                d->next_poll = now + d->poll_ms;
                poll_device(d);
            }
        }
        run_linkage();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/** edge_rpt: sleeps until the next periodic or scheduled report, or an on-change notification. */
static void rpt_task(void *arg)
{
    (void)arg;
    int64_t next_periodic = now_ms() + (int64_t)R.interval_s * 1000, last_minute = -1;
    for (;;) {
        int64_t wait = R.periodic ? next_periodic - now_ms() : 60000;
        if (R.scheduled && wait > 1000)
            wait = 1000;  // the clock is checked every second
        if (wait < 1)
            wait = 1;
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait)))
            report("change");
        if (R.periodic && now_ms() >= next_periodic) {
            next_periodic += (int64_t)R.interval_s * 1000;  // fixed cadence, no drift
            if (next_periodic < now_ms())
                next_periodic = now_ms() + (int64_t)R.interval_s * 1000;
            report("periodic");
        }
        if (R.scheduled && edge_schedule_due(sysmgr_utc_ms() / 1000, sysmgr_timezone(), R.schedule, R.hour, R.minute, &last_minute)) {
            report("scheduled");
            SLOG_I("edge", "scheduled report sent");
        }
    }
}

// ---- start --------------------------------------------------------------------------------------------

static void *heap_alloc(size_t n) { return calloc(1, n); }

/** Everything Edge holds back to the heap, and Edge off with `why`. */
static void give_up(const char *why)
{
    for (int i = 0; i < s_ndev; i++)
        if (s_devs[i].link)
            conn_close(&((tcp_link_t *)s_devs[i].link)->c);
    free(s_tcp);
    s_tcp = NULL;
    edge_index_free(&s_index);
    memset(&s_index, 0, sizeof s_index);
    edge_tmpl_free(s_tmpl);
    s_tmpl = NULL;
    free(s_report);
    s_report = NULL;
    edge_unreserve(&M, free);
    s_devs = NULL;
    s_ndev = s_npts = s_nlinks = 0;
    s_note = why;
    SLOG_E("edge", "Edge stays off: %s", why);
}

void edge_start(void)
{
    json_alloc_install();  // so every failed cJSON allocation of the load is counted (the web does it later too)
    size_t cap;
    char *buf = web_edge_buffer_for_boot(&cap);
    size_t len = store_edge_text(buf, cap);
    if (!len) {
        strcpy(buf, "[]");
        len = 2;
    }
    load_t count = {0};
    if (!walk(&count, buf, len)) {
        s_note = "MODEL UNREADABLE";
        return;
    }
    cJSON *rep = store_get("edgeReport"), *links = store_get("edgeLinkage");
    if (!rep || !links) {
        cJSON_Delete(rep);
        cJSON_Delete(links);
        give_up("NO MEMORY");
        return;
    }
    read_report(rep);
    cJSON_Delete(rep);

    // Tasks: a poll task per port with devices, one per Modbus TCP device, edge_ctl, and edge_rpt.
    edge_counts_t c = {.ndev = count.ndev, .npts = count.npts, .nlinks = cJSON_GetArraySize(links),
                       .tcb_bytes = sizeof(StaticTask_t), .nlocks = 1 + count.neth, .lock_bytes = sizeof(StaticSemaphore_t)};
    for (int p = 1; p <= SERIAL_PORTS; p++)
        if (count.nbus[p])
            c.stack_bytes[c.ntasks++] = BUS_STACK;
    for (int k = 0; k < count.neth; k++)
        c.stack_bytes[c.ntasks++] = TCP_STACK;
    c.stack_bytes[c.ntasks++] = CTL_STACK;
    if (R.reporting)
        c.stack_bytes[c.ntasks++] = RPT_STACK;
    s_tcp = count.neth ? calloc((size_t)count.neth, sizeof *s_tcp) : NULL;
    if ((count.neth && !s_tcp) || !edge_reserve(&c, &M, heap_alloc, free)) {
        cJSON_Delete(links);
        give_up("NO MEMORY");
        return;
    }
    s_devs = M.devs;
    load_t fill = {.fill = true};
    bool ok = walk(&fill, buf, len) && edge_index_init(&s_index, (size_t)(fill.npts ? fill.npts : 1));
    s_ndev = fill.ndev;
    s_npts = fill.npts;
    for (int i = 0; ok && i < s_npts; i++)
        if (!edge_index_put(&s_index, M.pts[i].name, &M.pts[i]))
            SLOG_W("edge", "point name %s used twice", M.pts[i].name);
    if (ok) {
        resolve_formulas();
        fill_links(links);
    }
    cJSON_Delete(links);
    if (!ok) {
        give_up("NO MEMORY");
        return;
    }

    // The template, on the heap (spec §9.3): compiled once, reports then only fill in values.
    if (R.reporting) {
        cJSON *r = store_get("edgeReport");
        const char *tmpl = r ? text(r, "template") : NULL;
        s_tmpl = tmpl ? edge_tmpl_compile(tmpl, resolve, &s_index, R.quote, R.placeholder_on ? R.placeholder : NULL) : NULL;
        cJSON_Delete(r);
        s_report_max = s_tmpl ? edge_tmpl_size(s_tmpl) : 0;
        if (!s_tmpl) {
            SLOG_W("edge", "the report template is not a JSON object: reports are off");
        } else if (s_report_max > REPORT_MAX) {
            SLOG_E("edge", "the report template needs %u bytes, over %u: reports are off", (unsigned)s_report_max, REPORT_MAX);
            edge_tmpl_free(s_tmpl);
            s_tmpl = NULL;
            s_note = "TEMPLATE TOO LARGE";
        } else if (!(s_report = malloc(s_report_max))) {
            give_up("NO MEMORY");
            return;
        }
    }

    // Nothing can fail from here on: locks and tasks on reserved memory.
    net_status_t ns;
    netmgr_status(&ns);
    snprintf(s_mac, sizeof s_mac, "%02X:%02X:%02X:%02X:%02X:%02X", ns.mac[0], ns.mac[1], ns.mac[2], ns.mac[3], ns.mac[4], ns.mac[5]);
    s_cache = xSemaphoreCreateMutexStatic(M.locks[0]);
    int lock = 1, eth = 0;
    for (int i = 0; i < s_ndev; i++) {
        edge_dev_t *d = &s_devs[i];
        if (d->source != EDGE_SRC_ETH)
            continue;
        tcp_link_t *t = &s_tcp[eth++];
        t->c.fd = -1;
        t->lock = xSemaphoreCreateMutexStatic(M.locks[lock++]);
        d->link = t;
    }
    if (R.remote && R.channel > 0)
        bridge_set_edge(R.channel, remote, R.format == FMT_TCP ? BRIDGE_EDGE_MBAP : R.format == FMT_RTU ? BRIDGE_EDGE_RTU : BRIDGE_EDGE_JSON);
    int task = 0, nbus = 0, ntcp = 0, nlocal = 0;
    char name[24];
    for (int p = 1; p <= SERIAL_PORTS; p++) {
        if (!count.nbus[p])
            continue;
        dev_list_t *l = &s_lists[task];
        static edge_dev_t *s_bus_devs[SERIAL_PORTS][64];
        l->devs = s_bus_devs[p - 1];
        for (int i = 0; i < s_ndev; i++)
            if (s_devs[i].source == EDGE_SRC_UART && s_devs[i].uart == p)
                l->devs[l->n++] = &s_devs[i];
        nbus += l->n;
        snprintf(name, sizeof name, "edge_bus%d", p);
        xTaskCreateStatic(poll_task, name, c.stack_bytes[task] / sizeof(StackType_t), l, PRIO_POLL, M.stacks[task], M.tcbs[task]);
        task++;
    }
    for (int i = 0; i < s_ndev; i++) {
        if (s_devs[i].source != EDGE_SRC_ETH)
            continue;
        dev_list_t *l = &s_lists[task];
        static edge_dev_t *s_tcp_dev[EDGE_MAX_TASKS];
        s_tcp_dev[task] = &s_devs[i];
        l->devs = &s_tcp_dev[task];
        l->n = 1;
        snprintf(name, sizeof name, "edge_tcp%d", ntcp++);
        xTaskCreateStatic(poll_task, name, c.stack_bytes[task] / sizeof(StackType_t), l, PRIO_POLL, M.stacks[task], M.tcbs[task]);
        task++;
    }
    static edge_dev_t *s_local[64];
    dev_list_t *ctl = &s_lists[task];
    ctl->devs = s_local;
    for (int i = 0; i < s_ndev; i++)
        if (s_devs[i].source == EDGE_SRC_IO || s_devs[i].source == EDGE_SRC_VIRTUAL)
            ctl->devs[ctl->n++] = &s_devs[i];
    nlocal = ctl->n;
    xTaskCreateStatic(ctl_task, "edge_ctl", c.stack_bytes[task] / sizeof(StackType_t), ctl, PRIO_CTL, M.stacks[task], M.tcbs[task]);
    task++;
    if (R.reporting && s_tmpl)
        s_rpt_task = xTaskCreateStatic(rpt_task, "edge_rpt", c.stack_bytes[task] / sizeof(StackType_t), NULL, PRIO_RPT, M.stacks[task],
                                       M.tcbs[task]);
    if (R.channel == 0 && (R.reporting || R.remote)) {
        s_note = "MQTT: Plan 09";
        SLOG_W("edge", "reports and remote access over MQTT come with Plan 09: Edge runs without them");
    }
    s_running = true;
    SLOG_I("edge", "%d devices (%d on the buses, %d Modbus TCP, %d local), %d points, %d linkage events; reports %s via %s%s",
           s_ndev, nbus, ntcp, nlocal, s_npts, s_nlinks, R.reporting ? "on" : "off", R.channel == 0 ? "MQTT" : R.channel == 1 ? "UART1's socket" : "UART2's socket",
           s_tmpl ? "" : R.reporting ? " (no template)" : "");
    SLOG_I("edge", "memory: point %u B, device %u B, report %u B, heap free %u", (unsigned)sizeof(edge_pt_t), (unsigned)sizeof(edge_dev_t),
           (unsigned)s_report_max, (unsigned)xPortGetFreeHeapSize());
}

void edge_status(char *state, size_t len)
{
    if (!s_running) {
        snprintf(state, len, "%s", s_note);
        return;
    }
    int ok = 0;
    if (cache_lock()) {
        for (int i = 0; i < s_npts; i++)
            ok += M.pts[i].valid;
        cache_unlock();
    }
    bool note = strcmp(s_note, "NOT RUNNING") != 0;
    snprintf(state, len, "RUNNING %d/%d%s%s%s", ok, s_npts, note ? " (" : "", note ? s_note : "", note ? ")" : "");
}

bool edge_template_fits(const char *tmpl, bool quote, const char *placeholder)
{
    if (!s_running)
        return true;  // the boot decides
    edge_tmpl_t *t = edge_tmpl_compile(tmpl, resolve, &s_index, quote, placeholder);
    if (!t)
        return true;  // not a template the boot compiles: reports off then, as on the ESP32
    bool fits = edge_tmpl_size(t) <= REPORT_MAX;
    edge_tmpl_free(t);
    return fits;
}
