// The pure parts of the Edge runtime (Plan 08c); see edge_core.h. The ESP32's edge_rt.c is the
// baseline for every rule here (load, poll_modbus, point_set, the remote-access mapping, write_point,
// tcp_exchange, run_linkage, schedule_due, slot_value).
#include "edge_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clock.h"
#include "jsonscan.h"
#include "modbus.h"

unsigned json_alloc_failures(void);  // the JSON allocator's failed allocations (arena.c; the host tests' own)

static int num(const cJSON *o, const char *k) { return (int)cJSON_GetNumberValue(cJSON_GetObjectItem(o, k)); }
static double dbl(const cJSON *o, const char *k) { return cJSON_GetNumberValue(cJSON_GetObjectItem(o, k)); }
static const char *text(const cJSON *o, const char *k)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(o, k));
    return s ? s : "";
}
static bool flag(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valuedouble != 0);
}
static void copy(char *dst, size_t cap, const char *src) { snprintf(dst, cap, "%s", src); }

void edge_dev_from_json(const cJSON *d, edge_dev_t *out)
{
    memset(out, 0, sizeof *out);
    out->hb = -1;  // no supervisor slot until its poll task sets one (Plan 11a task 0b)
    copy(out->name, sizeof out->name, text(d, "name"));
    const char *src = text(d, "source");
    out->source = !strcmp(src, "eth") ? EDGE_SRC_ETH : !strcmp(src, "io") ? EDGE_SRC_IO : !strcmp(src, "virtual") ? EDGE_SRC_VIRTUAL : EDGE_SRC_UART;
    out->uart = num(d, "uart");
    out->dlt645 = out->source == EDGE_SRC_UART && !strcmp(text(d, "protocol"), "dlt645");
    copy(out->addr, sizeof out->addr, text(d, "addr"));
    out->unit = (uint8_t)atoi(out->addr);
    copy(out->ip, sizeof out->ip, text(d, "ip"));
    out->port = (uint16_t)num(d, "ethPort");
    out->poll_ms = num(d, "poll");
    out->map_en = flag(d, "mapEn");
    out->map_addr = (uint8_t)num(d, "mapAddr");
    out->merge = flag(d, "merge");
}

/** "DO2" -> EDGE_IO_DO, index 1; false (kind -1) for anything else. */
static bool io_channel(const char *s, int *kind, int *index)
{
    *kind = -1;
    if (strlen(s) < 3)
        return false;
    *kind = !strncmp(s, "DI", 2) ? EDGE_IO_DI : !strncmp(s, "DO", 2) ? EDGE_IO_DO : !strncmp(s, "AI", 2) ? EDGE_IO_AI : -1;
    *index = atoi(s + 2) - 1;
    if (*index < 0)
        *kind = -1;
    return *kind >= 0;
}

void edge_pt_from_json(const cJSON *n, edge_dev_t *dev, edge_pt_t *out)
{
    memset(out, 0, sizeof *out);
    out->dev = dev;
    copy(out->name, sizeof out->name, text(n, "name"));
    out->fc = num(n, "fc");
    out->reg = (uint16_t)num(n, "reg");
    out->di = (uint32_t)strtoul(text(n, "di"), NULL, 16);
    out->dtype = edge_dtype(text(n, "dtype"));
    if (out->dtype < 0)
        out->dtype = DT_UINT16;
    out->timeout_ms = num(n, "timeout");
    out->on_change = flag(n, "onChange");
    out->range = num(n, "range");
    out->map_en = flag(n, "mapEn");
    out->map_addr = (uint16_t)num(n, "mapAddr");
    copy(out->expr, sizeof out->expr, text(n, "formula"));
    io_channel(text(n, "ioPoint"), &out->io_kind, &out->io_index);
    out->io_report = num(n, "ioReport");
    out->lower = dbl(n, "lower");
    out->upper = dbl(n, "upper");
}

// ---- reading ------------------------------------------------------------------------------------------

static int by_fc_reg(const void *a, const void *b)
{
    const edge_pt_t *x = *(edge_pt_t *const *)a, *y = *(edge_pt_t *const *)b;
    return x->fc != y->fc ? x->fc - y->fc : (int)x->reg - (int)y->reg;
}

static int width_of(const edge_pt_t *p) { return p->fc <= 2 ? 1 : edge_dtype_regs(p->dtype); }

int edge_batches(const edge_dev_t *d, edge_pt_t **order, edge_batch_t *out, int max)
{
    if (d->merge)
        qsort(order, (size_t)d->npts, sizeof order[0], by_fc_reg);
    int nb = 0;
    for (int i = 0; i < d->npts && nb < max;) {
        edge_pt_t *first = order[i];
        bool bits = first->fc <= 2;
        uint32_t start = first->reg, end = start + (uint32_t)width_of(first);  // 32 bits: a read may end at 65536
        int j = i + 1, timeout = first->timeout_ms;
        // Neighbours of the same function code, gaps of up to 8 read through.
        while (d->merge && j < d->npts && order[j]->fc == first->fc && order[j]->reg >= start && order[j]->reg <= end + 8) {
            uint32_t e = order[j]->reg + (uint32_t)width_of(order[j]);
            if ((e - start) > (bits ? 2000 : 125))
                break;
            if (e > end)
                end = e;
            if (order[j]->timeout_ms > timeout)
                timeout = order[j]->timeout_ms;
            j++;
        }
        out[nb++] = (edge_batch_t){.first = i, .count = j - i, .fc = first->fc, .timeout_ms = timeout, .start = (uint16_t)start,
                                   .qty = (uint16_t)(end - start)};
        i = j;
    }
    return nb;
}

int edge_step(edge_dev_t *d, int nsteps)
{
    if (nsteps <= 0)
        return -1;
    if (d->cursor < 0 || d->cursor >= nsteps)
        d->cursor = 0;
    int i = d->cursor;
    d->cursor = (i + 1) % nsteps;
    return i;
}

bool edge_batch_take(const edge_pt_t *p, const edge_batch_t *b, const uint8_t *data, int n, uint16_t *regs)
{
    bool bits = b->fc <= 2;
    int off = p->reg - b->start, w = width_of(p);
    if (n < 0 || off < 0 || (bits ? off / 8 >= n : (off + w) * 2 > n))
        return false;  // no answer, or a reply too short for this point
    if (bits) {
        regs[0] = (uint16_t)((data[off / 8] >> (off % 8)) & 1);
    } else {
        for (int r = 0; r < w; r++)
            regs[r] = (uint16_t)(data[2 * (off + r)] << 8 | data[2 * (off + r) + 1]);
    }
    return true;
}

bool edge_apply_formula(const edge_pt_t *p, double raw, double *out)
{
    if (!p->expr[0]) {
        *out = raw;
        return true;
    }
    double args[EDGE_FORMULA_REFS];
    int n = p->nrefs;
    if (n == 0) {
        args[0] = raw;
        n = 1;
    }
    for (int k = 0; k < p->nrefs; k++) {
        if (!p->refs[k] || !p->refs[k]->valid)
            return false;
        args[k] = p->refs[k]->value;
    }
    return edge_formula_eval(p->expr, args, n, out);
}

bool edge_point_update(edge_pt_t *p, double v)
{
    p->value = v;
    p->valid = true;
    // Modbus points keep the registers as read and IO points the IO map's (edge_rt); virtual and
    // DL/T645 values are encoded in the point's type for mapped reads.
    const edge_dev_t *d = p->dev;
    bool keep_raw = d->source == EDGE_SRC_ETH || d->source == EDGE_SRC_IO || (d->source == EDGE_SRC_UART && !d->dlt645);
    if (!keep_raw)
        edge_encode(p->dtype, v, p->raw);
    bool due = false;
    if (p->on_change) {
        due = !p->reported_valid || (p->dtype == DT_BOOL || d->source == EDGE_SRC_IO ? v != p->reported
                                                                                       : fabs(v - p->reported) >= (p->range > 0 ? p->range : 1));
    }
    if (p->io_report) {  // AI alarm: report when the value enters (1) or leaves (2) the range
        bool inside = v >= p->lower && v <= p->upper;
        if (inside != p->in_range && inside == (p->io_report == 1))
            due = true;
        p->in_range = inside;
    }
    return due;
}

// ---- mapping -------------------------------------------------------------------------------------------

uint8_t edge_exposed_unit(const edge_dev_t *d, uint8_t io_addr)
{
    if (d->map_en)
        return d->map_addr;
    return d->source == EDGE_SRC_IO ? io_addr : d->unit;
}

static uint16_t exposed_reg(const edge_pt_t *p) { return p->map_en ? p->map_addr : p->reg; }

int edge_exposed_fc(const edge_pt_t *p) { return p->dev->dlt645 ? (p->map_en ? 3 : 0) : p->fc; }

edge_pt_t *edge_point_at(edge_dev_t *d, int fc, uint16_t addr, int *off)
{
    for (int i = 0; i < d->npts; i++) {
        edge_pt_t *p = &d->pts[i];
        if (edge_exposed_fc(p) != fc)
            continue;
        int w = fc <= 2 ? 1 : edge_dtype_regs(p->dtype);
        uint16_t a = exposed_reg(p);
        if (addr >= a && addr < a + w) {
            *off = addr - a;
            return p;
        }
    }
    return NULL;
}

bool edge_writable(const edge_pt_t *p)
{
    if (p->dev->source == EDGE_SRC_IO)
        return p->io_kind == EDGE_IO_DO;
    bool modbus = p->dev->source == EDGE_SRC_ETH || (p->dev->source == EDGE_SRC_UART && !p->dev->dlt645);
    return modbus && (p->fc == 1 || p->fc == 3);
}

edge_dev_t *edge_device_for_unit(edge_dev_t *devs, int n, uint8_t unit, uint8_t io_addr)
{
    for (int i = 0; i < n; i++) {
        edge_dev_t *d = &devs[i];
        bool addressable = d->map_en || (d->source == EDGE_SRC_UART && !d->dlt645) || d->source == EDGE_SRC_ETH ||
                           d->source == EDGE_SRC_VIRTUAL || d->source == EDGE_SRC_IO;
        if (addressable && edge_exposed_unit(d, io_addr) == unit)
            return d;
    }
    return NULL;
}

size_t edge_write_req(const edge_pt_t *p, double v, uint8_t *rtu, size_t max)
{
    const edge_dev_t *d = p->dev;
    bool modbus = d->source == EDGE_SRC_ETH || (d->source == EDGE_SRC_UART && !d->dlt645);
    if (!modbus || (p->fc != 1 && p->fc != 3) || max < 7 + 8 + 2)
        return 0;
    if (p->fc == 1)
        return mb_rtu_write1(rtu, d->unit, 5, p->reg, v != 0);
    uint16_t regs[4];
    int c = edge_encode(p->dtype, v, regs);
    return c == 1 ? mb_rtu_write1(rtu, d->unit, 6, p->reg, regs[0]) : mb_rtu_write_regs(rtu, d->unit, p->reg, regs, (uint16_t)c);
}

bool edge_write_echo_ok(const uint8_t *req, const uint8_t *resp, int got, bool rtu)
{
    // FC5/6 repeat all 6 bytes (unit, fc, address, value), FC16 unit, fc, start and count.
    if (rtu)
        return got == 8 && mb_rtu_valid(resp, 8) && memcmp(resp, req, 6) == 0;
    return got == 6 && memcmp(resp, req, 6) == 0;
}

int edge_tcp_take(uint8_t *buf, size_t *got, uint16_t tid, uint8_t unit, uint8_t fc, uint8_t *out, size_t max)
{
    for (;;) {  // every complete frame in what arrived
        int fl = mb_tcp_frame_len(buf, *got);
        if (fl == 0)
            return 0;
        if (fl < 0)
            return -1;  // not Modbus TCP: the stream is unusable
        bool mine = (uint16_t)(buf[0] << 8 | buf[1]) == tid && buf[6] == unit && (buf[7] & 0x7F) == fc;
        if (mine) {
            size_t body = (size_t)fl - 6;
            if (body > max)
                return -1;
            memcpy(out, buf + 6, body);
            return (int)body;
        }
        memmove(buf, buf + fl, *got - (size_t)fl);  // a late reply to an earlier request: skip it
        *got -= (size_t)fl;
    }
}

// ---- linkage, schedule, system fields ------------------------------------------------------------------

int edge_link_step(edge_link_t *l, int64_t now_ms, bool valid, double value, bool do_now, bool *fired)
{
    *fired = false;
    if (!l->enable || now_ms < l->next_check)
        return EDGE_DO_KEEP;
    l->next_check = now_ms + (l->cycle_ms > 0 ? l->cycle_ms : 100);
    if (!valid)
        return EDGE_DO_KEEP;  // a failed point never drives an output
    bool state = edge_condition(l->cond, value, l->lower, l->upper);
    bool rose = state && !l->last_state;
    l->last_state = state;  // every valid check is recorded; minInterval only holds back the action (outside review)
    bool waiting = l->has_fired && now_ms - l->last_fire < l->min_ms;
    int act = EDGE_DO_KEEP;
    if (l->cond <= 1) {  // follow / opposite: the output mirrors the condition
        if (!waiting && do_now != state) {
            act = state ? EDGE_DO_ON : EDGE_DO_OFF;
            l->last_fire = now_ms;
            l->has_fired = true;
        }
    } else if (rose && !waiting) {  // a threshold: act when it becomes true; an edge during the wait is dropped
        act = l->action == 1 ? EDGE_DO_OFF : l->action == 2 ? EDGE_DO_ON : l->action == 3 ? (do_now ? EDGE_DO_OFF : EDGE_DO_ON) : EDGE_DO_KEEP;
        *fired = true;
        l->last_fire = now_ms;
        l->has_fired = true;
    }
    return act;
}

bool edge_schedule_due(int64_t utc_s, int tz_hours, int schedule, int hour, int minute, int64_t *last)
{
    if (utc_s <= 0)
        return false;
    int64_t m = utc_s / 60;  // each due minute fires once
    if (m == *last)
        return false;
    civil_t c;
    clock_from_unix(utc_s + (int64_t)tz_hours * 3600, &c);
    bool due = schedule == 0 ? c.min == 0 : schedule == 1 ? c.min % 15 == 0 : schedule == 2 ? true : c.hour == hour && c.min == minute;
    if (due)
        *last = m;
    return due;
}

static const char *const SYS_NAMES[EDGE_SYS_FIELDS] = {"sys_sn", "sys_mac", "sys_ver", "sys_time", "sys_unix_time"};

int edge_sys_field(const char *name)
{
    if (!strcmp(name, "sys_net_time"))  // in the reference unit's example, not its field table
        return EDGE_SYS_TIME;
    for (int i = 0; i < EDGE_SYS_FIELDS; i++)
        if (!strcmp(name, SYS_NAMES[i]))
            return i;
    return -1;
}

edge_kind_t edge_sys_value(int field, int64_t utc_ms, const char *sn, const char *mac, const char *ver, double *num_out,
                           const char **text_out, char buf[20])
{
    switch (field) {
    case EDGE_SYS_SN: return *text_out = sn, EDGE_TEXT;
    case EDGE_SYS_MAC: return *text_out = mac, EDGE_TEXT;
    case EDGE_SYS_VER: return *text_out = ver, EDGE_TEXT;
    default: break;
    }
    if (utc_ms <= 0)
        return EDGE_NO_VALUE;  // no clock: the failure placeholder, never a made-up time
    if (field == EDGE_SYS_UNIX_TIME)
        return *num_out = (double)(utc_ms / 1000), EDGE_NUMBER;
    clock_format(utc_ms / 1000, 0, buf);  // sys_time is UTC on the reference unit
    return *text_out = buf, EDGE_TEXT;
}

// ---- the report template --------------------------------------------------------------------------------

int edge_tmpl_check(const char *tmpl, edge_resolve_fn resolve, void *ctx, bool quote, const char *ph, size_t max, edge_tmpl_t **keep)
{
    if (keep)
        *keep = NULL;
    unsigned before = json_alloc_failures();
    edge_tmpl_t *t = edge_tmpl_compile(tmpl, resolve, ctx, quote, ph);
    if (json_alloc_failures() != before) {  // a part of it may be missing or wrong (the placeholder's parse)
        edge_tmpl_free(t);
        return EDGE_TMPL_NO_MEMORY;
    }
    if (!t) {
        js_span_t root;
        bool object = js_parse_root(tmpl, strlen(tmpl), &root) && js_type(root) == JS_OBJECT;
        return object ? EDGE_TMPL_NO_MEMORY : EDGE_TMPL_NOT_OBJECT;  // a valid object compiles unless memory ran out
    }
    if (edge_tmpl_size(t) > max) {
        edge_tmpl_free(t);
        return EDGE_TMPL_TOO_LARGE;
    }
    if (keep)
        *keep = t;
    else
        edge_tmpl_free(t);
    return EDGE_TMPL_OK;
}

// ---- memory ---------------------------------------------------------------------------------------------

void edge_unreserve(edge_mem_t *m, edge_free_fn release)
{
    release(m->devs);
    release(m->pts);
    release(m->order);
    release(m->batches);
    release(m->links);
    release(m->snap_value);
    release(m->snap_valid);
    release(m->report);
    for (int i = 0; i < EDGE_MAX_TASKS; i++) {
        release(m->stacks[i]);
        release(m->tcbs[i]);
    }
    for (int i = 0; i < EDGE_MAX_LOCKS; i++)
        release(m->locks[i]);
    release(m->accs);
    memset(m, 0, sizeof *m);
}

static bool take(void **p, size_t n, edge_alloc_fn alloc)
{
    *p = alloc(n ? n : 1);
    return *p != NULL;
}

bool edge_reserve(const edge_counts_t *c, edge_mem_t *m, edge_alloc_fn alloc, edge_free_fn release)
{
    memset(m, 0, sizeof *m);
    if (c->ntasks > EDGE_MAX_TASKS || c->nlocks > EDGE_MAX_LOCKS)
        return false;
    size_t np = (size_t)(c->npts ? c->npts : 1);
    bool ok = take((void **)&m->devs, (size_t)c->ndev * sizeof *m->devs, alloc) && take((void **)&m->pts, np * sizeof *m->pts, alloc) &&
              take((void **)&m->order, np * sizeof *m->order, alloc) && take((void **)&m->batches, np * sizeof *m->batches, alloc) &&
              take((void **)&m->links, (size_t)c->nlinks * sizeof *m->links, alloc) &&
              take((void **)&m->snap_value, np * sizeof *m->snap_value, alloc) && take((void **)&m->snap_valid, np * sizeof *m->snap_valid, alloc);
    if (ok && c->report_bytes)
        ok = take((void **)&m->report, c->report_bytes, alloc);
    for (int i = 0; ok && i < c->ntasks; i++)
        ok = take(&m->stacks[i], c->stack_bytes[i], alloc) && take(&m->tcbs[i], c->tcb_bytes, alloc);
    for (int i = 0; ok && i < c->nlocks; i++)
        ok = take(&m->locks[i], c->lock_bytes, alloc);
    if (ok && c->acc_bytes)
        ok = take(&m->accs, c->acc_bytes, alloc);
    if (!ok)
        edge_unreserve(m, release);
    return ok;
}

void edge_plan_batches(edge_mem_t *m, int ndev)
{
    for (int k = 0; k < ndev; k++) {
        edge_dev_t *d = &m->devs[k];
        size_t at = (size_t)(d->pts - m->pts);
        d->order = m->order + at;
        d->batches = m->batches + at;
        for (int i = 0; i < d->npts; i++)
            d->order[i] = &d->pts[i];
        d->nbatch = edge_batches(d, d->order, d->batches, d->npts);
    }
}
