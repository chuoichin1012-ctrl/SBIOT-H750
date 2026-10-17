// The pure parts of the Edge runtime (spec §9, Plan 08c), host-tested (test_edge_core): the model's C
// types, read batching, change detection, Modbus mapping, writes, Modbus TCP reply matching, linkage,
// the report schedule, system fields, and the all-or-nothing reservation of Edge's memory. The ESP32's
// edge_rt.c is the baseline; edge_rt.c drives this on the board.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "edgecore.h"

enum { EDGE_SRC_UART, EDGE_SRC_ETH, EDGE_SRC_IO, EDGE_SRC_VIRTUAL };
enum { EDGE_IO_DI, EDGE_IO_DO, EDGE_IO_AI };

typedef struct edge_dev edge_dev_t;

typedef struct edge_pt {
    char name[EDGE_NAME_MAX];
    edge_dev_t *dev;
    int fc, dtype, timeout_ms;
    uint16_t reg;
    uint32_t di;
    bool on_change, map_en;
    double range, lower, upper;
    uint16_t map_addr;
    char expr[65];  // the formula as stored; edge_rt splits it into the expression and refs
    int nrefs;
    struct edge_pt *refs[EDGE_FORMULA_REFS];
    int io_kind, io_index, io_report;  // io_kind -1: not an IO point
    // state, under the cache lock
    bool valid, reported_valid, in_range;
    double value, reported;
    uint16_t raw[4];
} edge_pt_t;

struct edge_dev {
    char name[EDGE_NAME_MAX], addr[13], ip[16];
    int hb;  // the supervisor slot of the task that polls it (Plan 11a task 0b), set by that task
    int source, uart, poll_ms;
    bool dlt645, map_en, merge;
    uint8_t unit, map_addr;
    uint16_t port;
    edge_pt_t *pts;
    int npts;
    // its reads in poll order, planned once (edge_plan_batches), in the model's arrays
    edge_pt_t **order;
    struct edge_batch *batches;
    int nbatch;
    // runtime (edge_rt.c)
    int64_t next_poll;
    int cursor;              // the next block or point to read (one per polling interval)
    volatile bool poll_now;  // a remote write asks for a read-back
    void *link;              // edge_rt's per-device I/O state (the Modbus TCP connection and its lock)
};

/** A normalised device header (cfg_edge_load) into C; strings are copied. */
void edge_dev_from_json(const cJSON *d, edge_dev_t *out);
/** A normalised node into C, for device `dev`; strings are copied. */
void edge_pt_from_json(const cJSON *n, edge_dev_t *dev, edge_pt_t *out);

// ---- Reading ----------------------------------------------------------------------------------------

typedef struct edge_batch {
    int first, count;  // order[first .. first+count)
    int fc, timeout_ms;
    uint16_t start, qty;
} edge_batch_t;

/** A device's reads in poll order. `order` holds its points (sorted here by fc and reg when merge);
 * with merge, neighbours of one fc with gaps of up to 8 go into one read, at most 125 registers or 2000
 * bits; a batch waits as long as its slowest point. Returns the number of batches (≤ max). */
int edge_batches(const edge_dev_t *d, edge_pt_t **order, edge_batch_t *out, int max);
/** The user's polling interval (Plan 08c): a device reads one block (Modbus) or point (DL/T645) at a time and
 * waits its interval before the next. The index of the step to read now among nsteps, advancing; -1 for none. */
int edge_step(edge_dev_t *d, int nsteps);
/** A point's registers out of a batch's reply data (n bytes, n < 0: no reply); false when not covered. */
bool edge_batch_take(const edge_pt_t *p, const edge_batch_t *b, const uint8_t *data, int n, uint16_t *regs);
/** The value after the point's formula (refs already resolved); false when an input is missing or it fails. */
bool edge_apply_formula(const edge_pt_t *p, double raw, double *out);
/** A fresh value: stores it (DL/T645 and virtual values are encoded into raw for mapped reads); true when
 * an on-change report or an AI range alarm is due. Cache lock held. */
bool edge_point_update(edge_pt_t *p, double v);

// ---- Modbus mapping (remote access) -----------------------------------------------------------------

/** Slave ID a device answers on: its mapping, else the IO page's address (IO device) or its own. */
uint8_t edge_exposed_unit(const edge_dev_t *d, uint8_t io_addr);
/** The function code a point answers on (DL/T645: 3 when mapped, else 0, not exposed). */
int edge_exposed_fc(const edge_pt_t *p);
/** The point covering addr in fc's space on d; *off the word offset inside it. */
edge_pt_t *edge_point_at(edge_dev_t *d, int fc, uint16_t addr, int *off);
/** Coils and holding registers of Modbus devices, IO outputs. */
bool edge_writable(const edge_pt_t *p);
/** The first device, in list order, that answers on `unit`. */
edge_dev_t *edge_device_for_unit(edge_dev_t *devs, int n, uint8_t unit, uint8_t io_addr);
/** The RTU write of v to a point's device (FC5, FC6 or FC16, CRC included); 0 when it is not a Modbus write. */
size_t edge_write_req(const edge_pt_t *p, double v, uint8_t *rtu, size_t max);
/** The write's echo: RTU (resp with CRC, 8 bytes) or Modbus TCP (unit + PDU, 6 bytes): the first 6 bytes. */
bool edge_write_echo_ok(const uint8_t *req, const uint8_t *resp, int got, bool rtu);

/** Modbus TCP replies in buf (*got bytes): the one for tid/unit/fc (exception or not) is copied to out as
 * unit + PDU; frames before it with another id are dropped. >0 its length, 0 incomplete, -1 not Modbus TCP
 * or too large for out (the connection is unusable). */
int edge_tcp_take(uint8_t *buf, size_t *got, uint16_t tid, uint8_t unit, uint8_t fc, uint8_t *out, size_t max);

// ---- Linkage, schedule, system fields ----------------------------------------------------------------

typedef struct {
    bool enable;
    int cond, cycle_ms, min_ms, out, action;
    double lower, upper;
    int64_t next_check, last_fire;
    bool last_state, has_fired;  // has_fired: minInterval counts from a trigger, so none before the first
    struct edge_pt *pt;  // what it watches (edge_rt)
    char name[EDGE_NAME_MAX], desc[65];
} edge_link_t;
enum { EDGE_DO_KEEP = -1, EDGE_DO_OFF = 0, EDGE_DO_ON = 1 };
/** One linkage check at now_ms: the output to set, or EDGE_DO_KEEP. *fired: a threshold event fired (log
 * it). A failed point never acts. */
int edge_link_step(edge_link_t *l, int64_t now_ms, bool valid, double value, bool do_now, bool *fired);

/** Scheduled reports: 0 hourly, 1 every 15 min, 2 every minute, 3 daily at hour:minute, in zone tz; each
 * due minute fires once (*last, the minute since the epoch). False while the clock is unset (utc_s 0). */
bool edge_schedule_due(int64_t utc_s, int tz_hours, int schedule, int hour, int minute, int64_t *last);

enum { EDGE_SYS_SN, EDGE_SYS_MAC, EDGE_SYS_VER, EDGE_SYS_TIME, EDGE_SYS_UNIX_TIME, EDGE_SYS_FIELDS };
/** "sys_sn" … "sys_unix_time" ("sys_net_time" is sys_time); -1 otherwise. */
int edge_sys_field(const char *name);
/** A system field's value, in the gateway's time zone `tz_hours` (the user, 2026-10-17): sys_time is local text
 * (into buf), sys_unix_time the Unix seconds shifted by the zone; both give EDGE_NO_VALUE while the clock is unset
 * (utc_ms 0). */
edge_kind_t edge_sys_value(int field, int64_t utc_ms, int tz_hours, const char *sn, const char *mac, const char *ver,
                           double *num, const char **text, char buf[20]);

// ---- The report template (spec §9.4) ---------------------------------------------------------------------

enum { EDGE_TMPL_OK, EDGE_TMPL_TOO_LARGE, EDGE_TMPL_NOT_OBJECT, EDGE_TMPL_NO_MEMORY };
/** Compiles tmpl and checks its report buffer against max. Any failed allocation is NO_MEMORY (cJSON's counted by
 * json_alloc_failures; a NULL from the compile of a valid object), never NOT_OBJECT or a silently wrong placeholder.
 * With keep, an OK template is handed over (the caller frees it); otherwise it is freed here. */
int edge_tmpl_check(const char *tmpl, edge_resolve_fn resolve, void *ctx, bool quote, const char *ph, size_t max, edge_tmpl_t **keep);

// ---- Memory: all or nothing (Plan 08c ruling 2) ------------------------------------------------------

#define EDGE_MAX_TASKS 9   // edge_bus 0..2, edge_tcp 0..5, edge_ctl, edge_rpt (bench: the largest model needs 9)
#define EDGE_MAX_LOCKS 6   // the cache, one per Modbus TCP device
typedef struct {
    int ndev, npts, nlinks;
    size_t report_bytes;              // 0: reports off
    int ntasks;
    size_t stack_bytes[EDGE_MAX_TASKS];
    size_t tcb_bytes;
    int nlocks;
    size_t lock_bytes;
    size_t acc_bytes;                 // the bridge's per-client Edge accumulators (remote access on a socket), or 0
} edge_counts_t;
typedef struct {
    edge_dev_t *devs;
    edge_pt_t *pts;
    edge_pt_t **order;      // the devices' reads (edge_plan_batches): a slice of each per device
    edge_batch_t *batches;
    edge_link_t *links;
    double *snap_value;
    bool *snap_valid;
    char *report;
    void *stacks[EDGE_MAX_TASKS], *tcbs[EDGE_MAX_TASKS], *locks[EDGE_MAX_LOCKS];
    void *accs;
} edge_mem_t;
typedef void *(*edge_alloc_fn)(size_t n);  // zeroed memory, or NULL
typedef void (*edge_free_fn)(void *p);
/** Every block Edge needs, or none: on false everything reserved so far is freed and *m is zeroed. */
bool edge_reserve(const edge_counts_t *c, edge_mem_t *m, edge_alloc_fn alloc, edge_free_fn release);
/** Once the model is filled (devs[i].pts in m->pts): each device's reads in poll order, into m->order and
 * m->batches (Plan 09d: off the poll tasks' stacks, where 300 points on one device would take 7 KB). */
void edge_plan_batches(edge_mem_t *m, int ndev);
void edge_unreserve(edge_mem_t *m, edge_free_fn release);
