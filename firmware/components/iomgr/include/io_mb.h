// The gateway's own IO as a Modbus slave (the ESP32's iomgr_mb_map and iomgr_remote, Plan 08a), on the
// reference unit's map:
//   coils 0..             DO1..   FC 1 read, FC 5 / 15 write
//   discrete inputs 0..   DI1..   FC 2
//   input registers 0..   AI1..   FC 4, float32 ABCD in mA, two registers a channel; exception 4 when the AI failed
// It reads the IO through `io_mb_ops_t`, so the host test runs it against fakes; iomgr registers its getters.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "modbus.h"

typedef struct {
    int ndo, ndi, nai;
    bool (*do_get)(int i);
    int (*do_set)(int i, bool on);  // 0 or -1
    bool (*di_get)(int i);
    int32_t (*ai_ua)(int i);
    bool (*ai_ok)(int i);
} io_mb_ops_t;

void io_mb_init(const io_mb_ops_t *ops, uint8_t address);
/** The map, for the Modbus TCP gateway (mb_serve with the unit's PDU). */
const mb_map_t *io_mb_map(void);
/** io.modbusAddress. */
uint8_t io_mb_address(void);
/** An RTU request addressed to the IO slave -> its RTU reply (with CRC); 0 when `req` is not one. */
size_t io_mb_remote(const uint8_t *req, size_t len, uint8_t *reply, size_t max);
