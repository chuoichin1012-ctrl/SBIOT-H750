// Modbus framing shared by the UART bridge (Modbus TCP gateway), the IO slave
// and Edge. Pure C, no ESP-IDF: host-tested in firmware/test/host.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MB_RTU_MAX 256
#define MB_TCP_MAX 260

// Exception codes used by the gateway
#define MB_EX_ILLEGAL_FUNCTION 0x01
#define MB_EX_ILLEGAL_ADDRESS 0x02
#define MB_EX_ILLEGAL_VALUE 0x03
#define MB_EX_DEVICE_FAILURE 0x04
#define MB_EX_TARGET_NO_RESPONSE 0x0B

uint16_t mb_crc16(const uint8_t *data, size_t len);
/** True when `frame` is at least 4 bytes and ends with its correct CRC. */
bool mb_rtu_valid(const uint8_t *frame, size_t len);
/** Appends the CRC (low byte first); returns the new length. */
size_t mb_rtu_seal(uint8_t *frame, size_t len);

/** Read request (FC 1-4): unit, fc, start, count. Returns 8. */
size_t mb_rtu_read(uint8_t *out, uint8_t unit, uint8_t fc, uint16_t start, uint16_t count);
/** Write one coil (FC 5) or register (FC 6). Returns 8. */
size_t mb_rtu_write1(uint8_t *out, uint8_t unit, uint8_t fc, uint16_t addr, uint16_t value);
/** Write registers (FC 16). Returns the length, 0 when `count` is out of range. */
size_t mb_rtu_write_regs(uint8_t *out, uint8_t unit, uint16_t start, const uint16_t *regs, uint16_t count);

/**
 * Length of the reply a well-behaved slave sends to RTU request `req`
 * (with CRC), so a transaction can stop waiting early; 0 when unknown.
 * An exception reply (5 bytes) is shorter: callers also stop on the gap.
 */
size_t mb_rtu_reply_len(const uint8_t *req, size_t len);

/**
 * Modbus TCP ADU -> RTU frame (unit + PDU + CRC). Returns the RTU length and
 * the transaction id, or 0 when `adu` is not a complete Modbus TCP frame.
 */
size_t mb_tcp_to_rtu(const uint8_t *adu, size_t len, uint8_t *rtu, size_t max, uint16_t *tid);
/** RTU reply -> Modbus TCP ADU with transaction id `tid`; 0 on a bad CRC. */
size_t mb_rtu_to_tcp(const uint8_t *rtu, size_t len, uint16_t tid, uint8_t *adu, size_t max);
/** Length of the first complete Modbus TCP ADU in `buf`, 0 when incomplete, -1 when not Modbus TCP. */
int mb_tcp_frame_len(const uint8_t *buf, size_t len);
/** Wraps a PDU (unit + fc + data, no CRC) as Modbus TCP. */
size_t mb_tcp_wrap(const uint8_t *unit_pdu, size_t len, uint16_t tid, uint8_t *adu, size_t max);

/**
 * Length of the RTU *request* starting at `buf` (with CRC), from its function
 * code: 8 for FC 1-6, 9 + byte count for FC 15/16 (known once 7 bytes are in).
 * 0 when the function code is not one the gateway frames.
 */
size_t mb_rtu_request_len(const uint8_t *buf, size_t len);

/**
 * A register map served by the gateway itself (IO slave, Edge mapped points).
 * Callbacks return 0, or the exception code to answer with:
 * MB_EX_ILLEGAL_ADDRESS (no such address), MB_EX_TARGET_NO_RESPONSE (it
 * exists but has no valid data yet), MB_EX_DEVICE_FAILURE (reading it failed).
 * Any callback may be NULL (-> exception 1 for the functions that need it).
 */
typedef struct {
    uint8_t (*read_bit)(void *ctx, uint8_t fc, uint16_t addr, bool *out);        // FC 1, 2
    uint8_t (*read_reg)(void *ctx, uint8_t fc, uint16_t addr, uint16_t *out);    // FC 3, 4
    uint8_t (*write_bit)(void *ctx, uint16_t addr, bool on);                     // FC 5, 15
    uint8_t (*write_reg)(void *ctx, uint16_t addr, uint16_t value);              // FC 6, 16
    /** FC 16 as one block (32/64-bit values span registers); NULL = write_reg per register. */
    uint8_t (*write_regs)(void *ctx, uint16_t addr, const uint16_t *values, uint16_t count);
    /**
     * True when `addr` exists in function code `fc`'s space (1 coils, 3 holding).
     * FC15 and per-register FC16 check the whole range with it before writing
     * anything, so a bad address in the middle changes nothing. NULL = no check.
     */
    bool (*exists)(void *ctx, uint8_t fc, uint16_t addr);
    void *ctx;
} mb_map_t;

/**
 * Answers request `req` (unit + PDU, without CRC) from `map`. Writes unit +
 * reply PDU (normal or exception) to `out`; returns its length (no CRC).
 */
size_t mb_serve(const mb_map_t *map, const uint8_t *req, size_t len, uint8_t *out, size_t max);
