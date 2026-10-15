// Simulated NOR flash for the OTA host tests (spec §12.1): erase sets bytes to 0xFF, programming
// can only clear bits, and every erase and program is one operation in a counter shared by all
// devices of a world. Faults:
// - power cut at operation N: that operation is done only partly (a prefix of the page programmed,
//   a prefix of the sector erased, one byte in between half-done), then longjmp to w->cut_jmp;
// - timeout at operation N: the operation does nothing and returns NOR_ETIMEOUT (a hung device);
// - up to two failing regions [lo, hi) of device dev, until cleared. code NOR_EIO / NOR_ETIMEOUT:
//   every erase and program there fails so; SIM_SILENT: programming reports success but changes
//   nothing; SIM_READ_EIO / SIM_READ_TIMEOUT: reads there fail, erase and program work;
// - power cuts repeated: after the cut at cut_at, cuts_left more, each cut_period operations later;
// - a hook called once, before operation N or before the first operation on device hook_dev at an
//   offset >= hook_addr (to change memory or faults at a precise point).
#pragma once
#include <setjmp.h>
#include <stdint.h>
#include "nor.h"

#define SIM_SILENT 1        // fail code: a program that reports success but leaves the cells as they were
#define SIM_READ_EIO 2      // fail code: reads fail with NOR_EIO
#define SIM_READ_TIMEOUT 3  // fail code: reads fail with NOR_ETIMEOUT

typedef struct sim_world sim_world_t;
struct sim_world {
    long ops;
    long cut_at, timeout_at, hook_at;  // -1: never
    int cuts_left;                     // further cuts after cut_at
    long cut_period;
    jmp_buf *cut_jmp;
    struct {
        int dev;  // 0 none, else the nor_sim id
        uint32_t lo, hi;
        int code;
        int count;  // > 0: the fault hits this many times, then clears itself; 0: until cleared
    } fail[2];
    int hook_dev;  // 0: hook by operation number only
    uint32_t hook_addr;
    void (*hook)(sim_world_t *w, void *arg);
    void *hook_arg;
    uint32_t rng;
};

typedef struct {
    nor_t nor;  // first: a nor_sim_t * is a nor_t *
    uint8_t *mem;
    uint32_t size;
    int id;
    sim_world_t *w;
} nor_sim_t;

void sim_world_init(sim_world_t *w, uint32_t seed);
void nor_sim_init(nor_sim_t *s, sim_world_t *w, int id, uint32_t size);  // all 0xFF
void nor_sim_free(nor_sim_t *s);
void sim_fail(sim_world_t *w, int i, int dev, uint32_t lo, uint32_t hi, int code);  // dev 0 clears
