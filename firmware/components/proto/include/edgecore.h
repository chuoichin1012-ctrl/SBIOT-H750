// Edge computing core: value decoding, formulas, DL/T645-2007 framing, report
// templates and linkage conditions. Pure C (+cJSON), host-tested.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

// ---- Data types (EdgeNode.dtype) ----
typedef enum {
    DT_BOOL, DT_UINT8, DT_INT8, DT_UINT16, DT_INT16, DT_UINT32_ABCD, DT_UINT32_CDAB, DT_INT32_ABCD, DT_INT32_CDAB,
    DT_FLOAT32_ABCD, DT_FLOAT32_CDAB, DT_FLOAT64, DT_COUNT
} edge_dtype_t;

/** Index of a dtype name ("uint16", "float32_cdab"...), or -1. */
int edge_dtype(const char *name);
/** Registers the type occupies (bool and 8-bit types: 1). */
int edge_dtype_regs(int dt);
/** Registers (big-endian words as read) -> value. */
double edge_decode(int dt, const uint16_t *regs);
/** Value -> registers for a write; returns the register count. */
int edge_encode(int dt, double v, uint16_t *regs);

// ---- Formulas: "=expr" with %s slots, then optional ",name1,name2" ----
#define EDGE_FORMULA_REFS 8
#define EDGE_NAME_MAX 17

/** Splits "=%s*2+%s,a,b" into the expression ("=%s*2+%s") and the names; returns the name count. */
int edge_formula_split(const char *formula, char *expr, size_t expr_max, char names[][EDGE_NAME_MAX], int max);
/**
 * Evaluates `expr` ("=..." with + - * / ( ) numbers and %s), filling the
 * %s slots from `args` in order. False on a syntax error or division by zero.
 */
bool edge_formula_eval(const char *expr, const double *args, int nargs, double *out);

// ---- DL/T645-2007 ----
/** Read request for data identifier `di` (e.g. 0x02010100) to meter `addr` (12 digits). Returns the length, 0 on a bad address. */
size_t dlt645_read_req(const char *addr, uint32_t di, uint8_t *out, size_t max);
/**
 * Parses a reply: 0 ok (`*value` = the BCD data as an unsigned integer, apply
 * the scaling formula on top), -1 the meter answered with an error, -2 no
 * valid frame for this meter/DI.
 */
int dlt645_parse(const uint8_t *buf, size_t len, const char *addr, uint32_t di, double *value);

// ---- Report template ----
// Compiled once at start-up into literal fragments and slots:
//   frag0 slot0 frag1 slot1 ... fragN
// The fragments are finished JSON text (keys, brackets, constants such as
// "bedroom"); a report only appends fragments and slot values into a buffer:
// no parsing, no name lookups, no allocation.

typedef enum {
    EDGE_NO_VALUE,  // a point that was never read, or whose last read failed
    EDGE_NUMBER,    // *num holds the value
    EDGE_TEXT,      // *text holds the value (system fields such as sys_mac)
} edge_kind_t;

/** Compile time: the slot handle for a template string, or NULL when it is a constant. */
typedef void *(*edge_resolve_fn)(void *ctx, const char *name);
/** Report time: the current value of a slot. */
typedef edge_kind_t (*edge_value_fn)(void *ctx, void *slot, double *num, const char **text);

typedef struct edge_tmpl edge_tmpl_t;

/**
 * Compiles a template (a JSON object). Each string value that `resolve`
 * knows becomes a slot; any other string is a constant. Numbers become JSON
 * numbers, or strings when `quote`; text values are strings. A slot without
 * value becomes `placeholder` (a JSON literal if it is one, else a string), or
 * null when `placeholder` is NULL. NULL on a bad template or no memory.
 */
edge_tmpl_t *edge_tmpl_compile(const char *tmpl, edge_resolve_fn resolve, void *ctx, bool quote, const char *placeholder);
size_t edge_tmpl_slots(const edge_tmpl_t *t);
/** Size of the buffer edge_tmpl_render() needs at most (text values up to EDGE_TEXT_MAX). */
size_t edge_tmpl_size(const edge_tmpl_t *t);
/** Renders into `out` (NUL-terminated); returns the length, 0 when `max` is too small. */
size_t edge_tmpl_render(const edge_tmpl_t *t, edge_value_fn value, void *ctx, char *out, size_t max);
void edge_tmpl_free(edge_tmpl_t *t);

#define EDGE_TEXT_MAX 40  // longest text value a slot may carry (sys_ver, sys_time, ...)

// ---- Name index (point names are unique across all devices) ----
typedef struct {
    const char **keys;
    void **vals;
    size_t cap;  // a power of two, at least twice the entries
} edge_index_t;

/** Room for `n` names; false when out of memory. */
bool edge_index_init(edge_index_t *ix, size_t n);
/** Adds `key` (the string must outlive the index); false when it is already there. */
bool edge_index_put(edge_index_t *ix, const char *key, void *val);
void *edge_index_get(const edge_index_t *ix, const char *key);
void edge_index_free(edge_index_t *ix);

/** Formats a value the way reports and JSON replies show it (integers without a decimal point). */
void edge_format(double v, char *out, size_t max);

// ---- Linkage ----
/**
 * Condition state for linkage condition `cond` (0 follow, 1 opposite,
 * 2 >= lower, 3 <= upper, 4 between, 5 outside, 6 > lower, 7 < upper).
 */
bool edge_condition(int cond, double v, double lower, double upper);
