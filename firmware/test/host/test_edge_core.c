// Host tests for edge_core (spec §9, Plan 08c): make -C stm32/firmware/test/host
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfgmodel.h"
#include "clock.h"
#include "edge_core.h"
#include "modbus.h"

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

static const cfg_env_t ENV = {.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};

// Allocation faults for cJSON (load_oom), counted as the board's allocator does.
static int s_fail_at = -1;
static unsigned s_failed;
static void *fail_malloc(size_t n)
{
    if (s_fail_at >= 0 && s_fail_at-- == 0) {
        s_failed++;
        return NULL;
    }
    return malloc(n);
}
unsigned json_alloc_failures(void) { return s_failed; }

// ---- a model through cfg_edge_load, as edge_rt loads it ---------------------------------------------

typedef struct {
    edge_dev_t devs[8];
    edge_pt_t pts[16];
    int ndev, npts;
} model_t;

static bool on_dev(void *ctx, const cJSON *d, int index)
{
    model_t *m = ctx;
    edge_dev_from_json(d, &m->devs[m->ndev++]);
    m->devs[index].pts = &m->pts[m->npts];
    return true;
}

static bool on_node(void *ctx, const cJSON *n, int dev)
{
    model_t *m = ctx;
    edge_pt_from_json(n, &m->devs[dev], &m->pts[m->npts++]);
    m->devs[dev].npts++;
    return true;
}

static const char MODEL[] =
    "[{\"name\":\"plc\",\"addr\":\"5\",\"uart\":2,\"poll\":500,\"merge\":true,\"mapEn\":true,\"mapAddr\":10,\"nodes\":["
    "{\"name\":\"t\",\"reg\":10,\"dtype\":\"int16\",\"formula\":\"=%s/10\",\"onChange\":true,\"range\":2,\"timeout\":300},"
    "{\"name\":\"f\",\"reg\":12,\"dtype\":\"float32_abcd\",\"timeout\":500},"
    "{\"name\":\"c\",\"fc\":1,\"reg\":3,\"dtype\":\"bool\",\"mapEn\":true,\"mapAddr\":100}]},"
    "{\"name\":\"meter\",\"protocol\":\"dlt645\",\"addr\":\"000000000001\",\"nodes\":[{\"name\":\"v\",\"di\":\"02010100\",\"mapEn\":true,\"mapAddr\":50}]},"
    "{\"name\":\"tcp\",\"source\":\"eth\",\"ip\":\"192.168.0.10\",\"ethPort\":1502,\"addr\":\"1\",\"nodes\":[{\"name\":\"h\",\"reg\":0}]},"
    "{\"name\":\"x\",\"source\":\"io\",\"nodes\":[{\"ioPoint\":\"DO2\"},{\"ioPoint\":\"AI1\",\"ioReport\":1,\"lower\":4,\"upper\":20}]},"
    "{\"name\":\"y\",\"source\":\"virtual\",\"addr\":\"200\",\"nodes\":[{\"name\":\"s\",\"formula\":\"=%s+%s,t,f\",\"dtype\":\"float32_abcd\"}]}]";

static bool load(model_t *m)
{
    memset(m, 0, sizeof *m);
    char err[160];
    return cfg_edge_load(&ENV, MODEL, sizeof MODEL - 1, on_dev, on_node, m, err, sizeof err) == CFG_OK;
}

static void test_from_json(void)
{
    model_t m;
    CHECK(load(&m) && m.ndev == 5 && m.npts == 8, "the model loads (%d devices, %d points)", m.ndev, m.npts);
    edge_dev_t *plc = &m.devs[0], *meter = &m.devs[1], *tcp = &m.devs[2], *io = &m.devs[3], *virt = &m.devs[4];
    CHECK(plc->source == EDGE_SRC_UART && plc->uart == 2 && plc->unit == 5 && plc->poll_ms == 500 && plc->merge && plc->map_en &&
              plc->map_addr == 10 && !plc->dlt645,
          "a Modbus RTU device on UART2");
    CHECK(meter->dlt645 && !strcmp(meter->addr, "000000000001"), "a DL/T645 meter");
    CHECK(tcp->source == EDGE_SRC_ETH && !strcmp(tcp->ip, "192.168.0.10") && tcp->port == 1502 && tcp->unit == 1, "a Modbus TCP device");
    CHECK(io->source == EDGE_SRC_IO && !strcmp(io->name, "SBIOT_IO") && virt->source == EDGE_SRC_VIRTUAL && virt->unit == 200,
          "the IO and virtual devices");
    edge_pt_t *t = &plc->pts[0], *c = &plc->pts[2], *v = &meter->pts[0], *dout = &io->pts[0], *ai = &io->pts[1];
    CHECK(!strcmp(t->name, "t") && t->fc == 3 && t->reg == 10 && t->dtype == DT_INT16 && t->on_change && t->range == 2 &&
              t->timeout_ms == 300 && !strcmp(t->expr, "=%s/10") && t->dev == plc && t->io_kind == -1,
          "a register node");
    CHECK(c->fc == 1 && c->map_en && c->map_addr == 100, "a coil, mapped");
    CHECK(v->di == 0x02010100u && v->map_en && v->map_addr == 50, "a DL/T645 node: its di");
    CHECK(!strcmp(dout->name, "DO2") && dout->io_kind == EDGE_IO_DO && dout->io_index == 1 && dout->fc == 1,
          "an IO node: DO2 (%s %d %d)", dout->name, dout->io_kind, dout->io_index);
    CHECK(ai->io_kind == EDGE_IO_AI && ai->io_index == 0 && ai->io_report == 1 && ai->lower == 4 && ai->upper == 20,
          "AI1 with its range alarm");
}

static void test_load_oom(void)
{
    model_t full, m;
    load(&full);
    int n = 0, partial = 0;
    for (;; n++) {
        memset(&m, 0, sizeof m);
        s_fail_at = n;
        cJSON_Hooks h = {fail_malloc, free};
        cJSON_InitHooks(&h);
        char err[160];
        cfg_result_t r = cfg_edge_load(&ENV, MODEL, sizeof MODEL - 1, on_dev, on_node, &m, err, sizeof err);
        cJSON_InitHooks(NULL);
        bool reached = s_fail_at < 0;
        s_fail_at = -1;
        if (r == CFG_OK) {
            bool same = m.ndev == full.ndev && m.npts == full.npts;
            for (int i = 0; same && i < m.npts; i++)
                same = !strcmp(m.pts[i].name, full.pts[i].name) && m.pts[i].reg == full.pts[i].reg &&
                       m.pts[i].dtype == full.pts[i].dtype && m.pts[i].timeout_ms == full.pts[i].timeout_ms;
            partial += !same;
        } else {
            partial += r != CFG_NO_MEMORY;
        }
        if (!reached)
            break;
    }
    CHECK(partial == 0 && n > 20, "out of memory at any of %d allocations: the whole model or NO_MEMORY (%d bad)", n, partial);
}

// ---- reading --------------------------------------------------------------------------------------

static edge_pt_t pt(int fc, int reg, int dtype, int timeout)
{
    edge_pt_t p = {.fc = fc, .reg = (uint16_t)reg, .dtype = dtype, .timeout_ms = timeout, .io_kind = -1};
    return p;
}

static void test_batches(void)
{
    edge_dev_t d = {.merge = true};
    edge_pt_t p[] = {pt(3, 12, DT_FLOAT32_ABCD, 500), pt(3, 10, DT_INT16, 300), pt(1, 3, DT_BOOL, 200), pt(3, 11, DT_UINT16, 200),
                     pt(3, 21, DT_UINT16, 200), pt(3, 31, DT_UINT16, 200)};
    edge_pt_t *order[6];
    for (int i = 0; i < 6; i++)
        order[i] = &p[i], p[i].dev = &d;
    d.pts = p, d.npts = 6;
    edge_batch_t b[8];
    int n = edge_batches(&d, order, b, 8);
    // sorted: FC1 3 | FC3 10 11 12(2) 21 | 31 : 14..21 is a gap of 7, 22..31 a gap of 9
    CHECK(n == 3, "merge: three reads (%d)", n);
    CHECK(b[0].fc == 1 && b[0].start == 3 && b[0].qty == 1, "the coil alone");
    CHECK(b[1].fc == 3 && b[1].start == 10 && b[1].qty == 12 && b[1].count == 4 && b[1].timeout_ms == 500,
          "10..21 in one read, gaps of 8 or less, the slowest timeout (%u %u %d %d)", b[1].start, b[1].qty, b[1].count, b[1].timeout_ms);
    CHECK(b[2].start == 31 && b[2].qty == 1, "a gap of 9 starts another read");
    d.merge = false;
    for (int i = 0; i < 6; i++)
        order[i] = &p[i];
    CHECK(edge_batches(&d, order, b, 8) == 6 && b[0].start == 12 && b[0].qty == 2, "merge off: one read per point, list order");
    // limits: 125 registers, 2000 bits
    edge_pt_t q[] = {pt(3, 0, DT_UINT16, 200), pt(3, 124, DT_UINT16, 200), pt(3, 125, DT_UINT16, 200)};
    edge_pt_t *qo[3] = {&q[0], &q[1], &q[2]};
    d.merge = true, d.pts = q, d.npts = 3;
    for (int i = 0; i < 3; i++)
        q[i].dev = &d;
    n = edge_batches(&d, qo, b, 8);
    CHECK(n == 2 && b[0].qty == 1 && b[1].start == 124 && b[1].qty == 2, "a gap over 8 is not read through; 124 and 125 are one read (%d)", n);
    edge_pt_t r[20];
    edge_pt_t *ro[20];
    for (int i = 0; i < 20; i++)
        r[i] = pt(3, i * 8, DT_UINT16, 200), r[i].dev = &d, ro[i] = &r[i];
    d.pts = r, d.npts = 20;
    n = edge_batches(&d, ro, b, 8);
    CHECK(n == 2 && b[0].qty <= 125 && b[0].count == 16 && b[1].count == 4, "at most 125 registers in a read (%d: %u)", n, b[0].qty);
    for (int i = 0; i < 20; i++)
        r[i] = pt(1, i * 120, DT_BOOL, 200), r[i].dev = &d, ro[i] = &r[i];
    edge_batch_t many[32];
    n = edge_batches(&d, ro, many, 32);
    CHECK(n == 20, "bits: gaps over 8 split as well (%d)", n);
    CHECK(edge_batches(&d, ro, b, 8) == 8, "never more batches than room for");
}

static void test_batch_take(void)
{
    edge_batch_t b = {.fc = 3, .start = 10, .qty = 4};
    edge_pt_t i16 = pt(3, 10, DT_INT16, 0), f32 = pt(3, 12, DT_FLOAT32_ABCD, 0);
    const uint8_t data[] = {0xFF, 0xF6, 0x00, 0x07, 0x41, 0x20, 0x00, 0x00};
    uint16_t regs[4];
    CHECK(edge_batch_take(&i16, &b, data, 8, regs) && (int16_t)regs[0] == -10, "int16 out of the reply");
    CHECK(edge_batch_take(&f32, &b, data, 8, regs) && regs[0] == 0x4120 && regs[1] == 0, "float32: two registers");
    CHECK(!edge_batch_take(&f32, &b, data, 6, regs) && edge_batch_take(&i16, &b, data, 6, regs), "a short reply fails only the points it misses");
    CHECK(!edge_batch_take(&i16, &b, data, -1, regs), "no reply");
    edge_batch_t bits = {.fc = 1, .start = 0, .qty = 10};
    edge_pt_t c9 = pt(1, 9, DT_BOOL, 0);
    const uint8_t coils[] = {0x00, 0x02};
    CHECK(edge_batch_take(&c9, &bits, coils, 2, regs) && regs[0] == 1, "coil 9: bit 1 of the second byte");
}

static void test_point_update(void)
{
    edge_dev_t uart = {.source = EDGE_SRC_UART}, io = {.source = EDGE_SRC_IO}, virt = {.source = EDGE_SRC_VIRTUAL};
    edge_pt_t p = pt(3, 0, DT_INT16, 0);
    p.dev = &uart, p.on_change = true, p.range = 2;
    CHECK(edge_point_update(&p, 10) && p.valid && p.value == 10, "the first value is a change");
    p.reported = 10, p.reported_valid = true;
    CHECK(!edge_point_update(&p, 11.9), "less than range: no report");
    CHECK(edge_point_update(&p, 12), "range reached: report");
    p.range = 0;
    CHECK(!edge_point_update(&p, 10.5) && edge_point_update(&p, 11), "range 0 counts as 1");
    edge_pt_t b = pt(1, 0, DT_BOOL, 0);
    b.dev = &io, b.on_change = true, b.reported_valid = true, b.reported = 0;
    CHECK(!edge_point_update(&b, 0) && edge_point_update(&b, 1), "an IO point: any change");
    edge_pt_t ai = pt(4, 0, DT_FLOAT32_ABCD, 0);
    ai.dev = &io, ai.io_report = 1, ai.lower = 4, ai.upper = 20;
    CHECK(!edge_point_update(&ai, 2) && edge_point_update(&ai, 5) && !edge_point_update(&ai, 6), "AI alarm 1: on entering the range");
    ai.io_report = 2;
    CHECK(edge_point_update(&ai, 25) && !edge_point_update(&ai, 30), "AI alarm 2: on leaving it");
    edge_pt_t v = pt(4, 0, DT_FLOAT32_ABCD, 0);
    v.dev = &virt;
    edge_point_update(&v, 20);
    CHECK(v.raw[0] == 0x41A0 && v.raw[1] == 0, "a virtual value is encoded for mapped reads (%04x)", v.raw[0]);
    edge_pt_t m = pt(3, 0, DT_INT16, 0);
    m.dev = &uart, m.raw[0] = 0xFFF6;
    edge_point_update(&m, -1);
    CHECK(m.raw[0] == 0xFFF6, "a Modbus point keeps its registers as read");
}

static void test_formula(void)
{
    edge_pt_t a = pt(3, 0, 0, 0), b = pt(3, 0, 0, 0), s = pt(4, 0, 0, 0), sc = pt(3, 0, 0, 0);
    a.valid = true, a.value = 3, b.valid = true, b.value = 4;
    strcpy(s.expr, "=%s+%s"), s.nrefs = 2, s.refs[0] = &a, s.refs[1] = &b;
    double out;
    CHECK(edge_apply_formula(&s, 0, &out) && out == 7, "a sum of two points");
    b.valid = false;
    CHECK(!edge_apply_formula(&s, 0, &out), "an input missing: no value");
    strcpy(sc.expr, "=%s/10");
    CHECK(edge_apply_formula(&sc, 250, &out) && out == 25, "scaling the raw value");
    sc.expr[0] = 0;
    CHECK(edge_apply_formula(&sc, 250, &out) && out == 250, "no formula: raw");
}

// ---- mapping, writes --------------------------------------------------------------------------------

static void test_mapping(void)
{
    model_t m;
    load(&m);
    edge_dev_t *plc = &m.devs[0], *meter = &m.devs[1], *io = &m.devs[3], *virt = &m.devs[4];
    CHECK(edge_exposed_unit(plc, 100) == 10 && edge_exposed_unit(io, 100) == 100 && edge_exposed_unit(virt, 100) == 200,
          "mapped, the IO page's address, own");
    CHECK(edge_exposed_fc(&meter->pts[0]) == 3 && edge_exposed_fc(&plc->pts[0]) == 3, "DL/T645 mapped: a holding register");
    meter->pts[0].map_en = false;
    CHECK(edge_exposed_fc(&meter->pts[0]) == 0, "DL/T645 unmapped: not exposed");
    int off = -1;
    CHECK(edge_point_at(plc, 3, 13, &off) == &plc->pts[1] && off == 1, "the second register of a float32");
    CHECK(edge_point_at(plc, 1, 100, &off) == &plc->pts[2] && !edge_point_at(plc, 1, 3, &off), "a mapped coil: only at its mapping");
    CHECK(!edge_point_at(plc, 4, 10, &off), "function codes kept apart");
    CHECK(edge_device_for_unit(m.devs, m.ndev, 10, 100) == plc && !edge_device_for_unit(m.devs, m.ndev, 5, 100), "mapped away from 5");
    CHECK(edge_device_for_unit(m.devs, m.ndev, 1, 100) == &m.devs[2] && edge_device_for_unit(m.devs, m.ndev, 100, 100) == io,
          "the TCP device on its own unit, the IO on the IO address");
    CHECK(!edge_device_for_unit(m.devs, m.ndev, 0, 100) || meter->map_en, "an unmapped meter answers nothing");
    CHECK(edge_writable(&plc->pts[0]) && edge_writable(&plc->pts[2]) && !edge_writable(&meter->pts[0]) &&
              edge_writable(&io->pts[0]) && !edge_writable(&io->pts[1]) && !edge_writable(&virt->pts[0]),
          "writable: registers, coils, DO; not DL/T645, AI or virtual");
}

static void test_writes(void)
{
    model_t m;
    load(&m);
    uint8_t rtu[64], resp[16];
    size_t n = edge_write_req(&m.devs[0].pts[2], 1, rtu, sizeof rtu);  // coil 3
    CHECK(n == 8 && rtu[0] == 5 && rtu[1] == 5 && rtu[3] == 3 && rtu[4] == 0xFF && rtu[5] == 0 && mb_rtu_valid(rtu, 8), "FC5 coil on");
    memcpy(resp, rtu, 8);
    CHECK(edge_write_echo_ok(rtu, resp, 8, true), "its echo");
    resp[5] = 1;
    CHECK(!edge_write_echo_ok(rtu, resp, 8, true) && !edge_write_echo_ok(rtu, rtu, 7, true), "a wrong or short echo");
    CHECK(edge_write_echo_ok(rtu, rtu, 6, false), "Modbus TCP: unit + PDU");
    n = edge_write_req(&m.devs[0].pts[0], -10, rtu, sizeof rtu);  // int16 register 10
    CHECK(n == 8 && rtu[1] == 6 && rtu[3] == 10 && rtu[4] == 0xFF && rtu[5] == 0xF6, "FC6 for one register");
    n = edge_write_req(&m.devs[0].pts[1], 10, rtu, sizeof rtu);  // float32 at 12
    CHECK(n == 13 && rtu[1] == 16 && rtu[3] == 12 && rtu[5] == 2 && rtu[7] == 0x41 && rtu[8] == 0x20, "FC16 for a float32");
    memcpy(resp, rtu, 6);
    resp[6] = 0, resp[7] = 0;
    CHECK(edge_write_echo_ok(rtu, resp, 6, false), "FC16's echo: unit, fc, start, count");
    CHECK(edge_write_req(&m.devs[1].pts[0], 1, rtu, sizeof rtu) == 0 && edge_write_req(&m.devs[4].pts[0], 1, rtu, sizeof rtu) == 0,
          "DL/T645 and virtual points: no write");
}

static size_t mbap(uint8_t *out, uint16_t tid, const uint8_t *pdu, size_t n) { return mb_tcp_wrap(pdu, n, tid, out, 300); }

static void test_tcp_take(void)
{
    uint8_t buf[600], out[260];
    size_t got = 0;
    const uint8_t stale[] = {1, 3, 2, 0x11, 0x11}, mine[] = {1, 3, 2, 0x22, 0x22};
    got += mbap(buf + got, 7, stale, sizeof stale);
    got += mbap(buf + got, 8, mine, sizeof mine);
    int r = edge_tcp_take(buf, &got, 8, 1, 3, out, sizeof out);
    CHECK(r == 5 && out[3] == 0x22, "a stale reply skipped, the right one taken (%d)", r);
    got = mbap(buf, 9, mine, sizeof mine) - 2;
    CHECK(edge_tcp_take(buf, &got, 9, 1, 3, out, sizeof out) == 0, "incomplete: wait");
    const uint8_t ex[] = {1, 0x83, 2};
    got = mbap(buf, 10, ex, sizeof ex);
    CHECK(edge_tcp_take(buf, &got, 10, 1, 3, out, sizeof out) == 3 && out[1] == 0x83, "an exception with the right id");
    memcpy(buf, "GET / HTTP", 10), got = 10;
    CHECK(edge_tcp_take(buf, &got, 1, 1, 3, out, sizeof out) == -1, "not Modbus TCP");
}

// ---- linkage, schedule, system fields ----------------------------------------------------------------

static void test_link(void)
{
    bool fired;
    edge_link_t f = {.enable = true, .cond = 0, .cycle_ms = 100, .min_ms = 0};  // follow: DO = value != 0
    CHECK(edge_link_step(&f, 0, true, 1, false, &fired) == EDGE_DO_ON, "follow: on");
    CHECK(edge_link_step(&f, 50, true, 0, true, &fired) == EDGE_DO_KEEP, "cycle: not checked again before 100 ms");
    CHECK(edge_link_step(&f, 100, true, 0, true, &fired) == EDGE_DO_OFF, "follow: off");
    CHECK(edge_link_step(&f, 200, true, 0, false, &fired) == EDGE_DO_KEEP, "already as it should be");
    CHECK(edge_link_step(&f, 300, false, 1, false, &fired) == EDGE_DO_KEEP, "a failed point never acts");
    edge_link_t o = {.enable = true, .cond = 1};
    CHECK(edge_link_step(&o, 0, true, 1, true, &fired) == EDGE_DO_OFF, "opposite");
    edge_link_t t = {.enable = true, .cond = 2, .lower = 50, .action = 2, .min_ms = 500};  // >= 50: on
    CHECK(edge_link_step(&t, 0, true, 10, false, &fired) == EDGE_DO_KEEP && !fired, "below: nothing");
    CHECK(edge_link_step(&t, 1000, true, 60, false, &fired) == EDGE_DO_ON && fired, "becomes true: acts once");
    CHECK(edge_link_step(&t, 2000, true, 70, true, &fired) == EDGE_DO_KEEP && !fired, "stays true: nothing more");
    edge_link_t mi = {.enable = true, .cond = 2, .lower = 50, .action = 2, .min_ms = 500};
    CHECK(edge_link_step(&mi, 1000, true, 60, false, &fired) == EDGE_DO_ON, "fires");
    edge_link_step(&mi, 1100, true, 10, true, &fired);
    CHECK(edge_link_step(&mi, 1200, true, 60, true, &fired) == EDGE_DO_KEEP && !fired, "minInterval holds it back");
    edge_link_t g = {.enable = true, .cond = 2, .lower = 50, .action = 3};
    CHECK(edge_link_step(&g, 0, true, 60, true, &fired) == EDGE_DO_OFF, "toggle");
    edge_link_t off = {.enable = false, .cond = 0};
    CHECK(edge_link_step(&off, 0, true, 1, false, &fired) == EDGE_DO_KEEP, "disabled");
}

static int64_t at(const char *s, int tz)
{
    civil_t c;
    clock_parse(s, &c);
    return clock_local_to_utc(&c, tz);
}

static void test_schedule(void)
{
    int64_t last = -1;
    CHECK(!edge_schedule_due(0, 0, 2, 0, 0, &last), "no clock: never");
    CHECK(edge_schedule_due(at("2026-10-16 10:00:05", 0), 0, 0, 0, 0, &last) && !edge_schedule_due(at("2026-10-16 10:00:40", 0), 0, 0, 0, 0, &last),
          "hourly at :00, once in that minute");
    CHECK(!edge_schedule_due(at("2026-10-16 10:01:00", 0), 0, 0, 0, 0, &last), "not at :01");
    CHECK(edge_schedule_due(at("2026-10-16 10:15:00", 0), 0, 1, 0, 0, &last) && !edge_schedule_due(at("2026-10-16 10:16:00", 0), 0, 1, 0, 0, &last),
          "every 15 minutes");
    CHECK(edge_schedule_due(at("2026-10-16 10:17:00", 0), 0, 2, 0, 0, &last) && edge_schedule_due(at("2026-10-16 10:18:00", 0), 0, 2, 0, 0, &last),
          "every minute");
    last = -1;
    CHECK(edge_schedule_due(at("2026-10-16 07:30:10", 7), 7, 3, 7, 30, &last), "daily at 07:30 local, UTC+7");
    CHECK(!edge_schedule_due(at("2026-10-16 07:30:50", 7), 7, 3, 7, 30, &last), "once");
    CHECK(edge_schedule_due(at("2026-10-17 07:30:00", 7), 7, 3, 7, 30, &last), "and again the next day");
}

static void test_sys(void)
{
    double num;
    const char *text = NULL;
    char buf[20];
    CHECK(edge_sys_field("sys_unix_time") == EDGE_SYS_UNIX_TIME && edge_sys_field("sys_net_time") == EDGE_SYS_TIME &&
              edge_sys_field("t") == -1,
          "field names");
    int64_t ms = at("2026-10-16 10:00:05", 0) * 1000 + 250;
    CHECK(edge_sys_value(EDGE_SYS_UNIX_TIME, ms, "SN", "MAC", "1.0", &num, &text, buf) == EDGE_NUMBER && num == (double)(ms / 1000),
          "sys_unix_time: a number of seconds");
    CHECK(edge_sys_value(EDGE_SYS_TIME, ms, "SN", "MAC", "1.0", &num, &text, buf) == EDGE_TEXT && !strcmp(text, "2026-10-16 10:00:05"),
          "sys_time: UTC text");
    CHECK(edge_sys_value(EDGE_SYS_UNIX_TIME, 0, "SN", "MAC", "1.0", &num, &text, buf) == EDGE_NO_VALUE &&
              edge_sys_value(EDGE_SYS_TIME, 0, "SN", "MAC", "1.0", &num, &text, buf) == EDGE_NO_VALUE,
          "no clock: no value for either");
    CHECK(edge_sys_value(EDGE_SYS_MAC, 0, "SN", "MAC", "1.0", &num, &text, buf) == EDGE_TEXT && !strcmp(text, "MAC") &&
              edge_sys_value(EDGE_SYS_SN, 0, "SN", "MAC", "1.0", &num, &text, buf) == EDGE_TEXT &&
              edge_sys_value(EDGE_SYS_VER, 0, "SN", "MAC", "1.0", &num, &text, buf) == EDGE_TEXT && !strcmp(text, "1.0"),
          "sys_sn, sys_mac, sys_ver: text, clock or not");
}

// ---- memory: all or nothing -------------------------------------------------------------------------

static int s_live, s_alloc_left = -1;
static void *counting_alloc(size_t n)
{
    if (s_alloc_left >= 0 && s_alloc_left-- == 0)
        return NULL;
    void *p = calloc(1, n);
    s_live += p != NULL;
    return p;
}
static void counting_free(void *p)
{
    s_live -= p != NULL;
    free(p);
}

static void test_reserve(void)
{
    edge_counts_t c = {.ndev = 5, .npts = 8, .nlinks = 2, .report_bytes = 4096, .ntasks = 4,
                       .stack_bytes = {6144, 5120, 4096, 4096}, .tcb_bytes = 100, .nlocks = 2, .lock_bytes = 80};
    edge_mem_t m;
    CHECK(edge_reserve(&c, &m, counting_alloc, counting_free) && m.devs && m.pts && m.links && m.report && m.stacks[3] && m.tcbs[3] &&
              m.locks[1] && !m.stacks[4],
          "everything reserved");
    int blocks = s_live;
    edge_unreserve(&m, counting_free);
    CHECK(s_live == 0, "and released");
    int bad = 0;
    for (int k = 0; k < blocks; k++) {
        s_alloc_left = k;
        bool ok = edge_reserve(&c, &m, counting_alloc, counting_free);
        s_alloc_left = -1;
        bad += ok || s_live != 0 || m.devs || m.report || m.stacks[0];
    }
    CHECK(bad == 0, "a failure at any of %d allocations leaves nothing reserved (%d bad)", blocks, bad);
    c.report_bytes = 0;
    CHECK(edge_reserve(&c, &m, counting_alloc, counting_free) && !m.report, "reports off: no report buffer");
    edge_unreserve(&m, counting_free);
}

int main(void)
{
    test_from_json();
    test_load_oom();
    test_batches();
    test_batch_take();
    test_point_update();
    test_formula();
    test_mapping();
    test_writes();
    test_tcp_take();
    test_link();
    test_schedule();
    test_sys();
    test_reserve();
    printf("sizes: edge_pt_t %zu B, edge_dev_t %zu B (host, 64-bit)\n", sizeof(edge_pt_t), sizeof(edge_dev_t));
    printf("test_edge_core: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
