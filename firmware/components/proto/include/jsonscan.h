// Strict JSON (RFC 8259) scanner over text that stays in place: it validates,
// finds the boundaries of values and walks containers without building a tree.
// The STM32 firmware uses it to process a large Edge model one node at a time
// (spec §9.4). Plain C, no allocation, host-tested.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A slice of the text: one complete JSON value, or a key with its quotes. */
typedef struct {
    const char *p;
    size_t n;
} js_span_t;

typedef enum { JS_INVALID, JS_OBJECT, JS_ARRAY, JS_STRING, JS_NUMBER, JS_TRUE, JS_FALSE, JS_NULL } js_type_t;

/** Nesting deeper than this is rejected; it keeps the recursion within a small task stack. */
#define JS_MAX_DEPTH 32

/**
 * True when s[0..n) holds exactly one JSON value (RFC 8259: well-formed UTF-8,
 * surrogate escapes in pairs), optionally surrounded by whitespace, nested at
 * most JS_MAX_DEPTH deep. On success *root is the value. Every string this
 * accepts is one cJSON accepts too, so a later cJSON_Parse of a span can only
 * fail for lack of memory.
 */
bool js_parse_root(const char *s, size_t n, js_span_t *root);

/** Type of a value span returned by this module. */
js_type_t js_type(js_span_t v);

/**
 * Walks the children of an array (elements) or an object (member values), in
 * order, the way cJSON_ArrayForEach walks a parsed container. Any other value
 * has no children. Only valid on spans from this module.
 */
typedef struct {
    js_span_t in;
    size_t pos;
} js_iter_t;

void js_iter_init(js_iter_t *it, js_span_t container);
/** Next child; `key` (may be NULL) receives the member key for objects, an empty span for arrays. */
bool js_iter_next(js_iter_t *it, js_span_t *key, js_span_t *val);
/** Number of children, as cJSON_GetArraySize counts them. */
size_t js_count(js_span_t container);

/**
 * True when a key span (with its quotes) names `lit` the way cJSON compares
 * object keys: escapes decoded, the key ending at a decoded NUL, and ASCII
 * case ignored when `nocase` (cJSON_GetObjectItem) or not (…CaseSensitive).
 */
bool js_key_is(js_span_t key, const char *lit, bool nocase);

/**
 * The same validity as js_parse_root, over text that arrives in pieces (Plan 09c: an edgeDevices body is
 * checked as it streams into the store's records). Feed it in any chunks; js_stream_end then says whether the
 * whole was exactly one JSON value of the wanted root type (JS_INVALID: any), surrounded by whitespace only.
 */
typedef struct {
    unsigned char stack[JS_MAX_DEPTH];  // '{' or '[' per open container
    int depth;
    int state, sub;       // where the scanner is; a counter within that state
    unsigned long u;      // a \uXXXX escape's value so far
    unsigned char lead;   // a UTF-8 sequence's first byte
    bool key, high, bad;  // in a key; a high surrogate wants its low one; invalid for good
    js_type_t want, root;
    const char *lit;      // the literal being matched
} js_stream_t;

void js_stream_init(js_stream_t *s, js_type_t want);
/** False once the text so far can no longer be valid; further feeds change nothing. */
bool js_stream_feed(js_stream_t *s, const char *data, size_t n);
/** True when everything fed is one whole JSON value of the wanted type. */
bool js_stream_end(js_stream_t *s);

#ifdef __cplusplus
}
#endif
