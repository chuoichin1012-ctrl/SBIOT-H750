# Plan 03: Edge node-by-node pipeline and JSON arena, implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Validate and normalise every config from JSON text within a fixed 64 KB arena. For `edgeDevices` this works one device header and one node at a time, and gives byte-identical results to the tree-based `cfg_validate` that the ESP32 firmware uses.

**Architecture:**
- **TLSF** (vendored) provides the allocator for a static region, recreated for every scope (`components/arena`). While a scope is open, cJSON allocates from the arena; otherwise it uses the heap.
- **`proto/jsonscan`** is a strict RFC 8259 scanner. It validates the text and walks its containers in place, without building a tree.
- **`cfgmodel` gains two text entry points:**
  - `cfg_validate_text()` for the small configs. It frees the input tree before printing, and prints into a caller buffer of `CFG_SMALL_MAX + 1` bytes.
  - `cfg_edge_text()` for `edgeDevices`, in three passes that mirror `list()`, `slaves()` and `slaves_strict()`. It parses only the schema's keys of one device header or one node at a time, keeps a compact summary for the cross-device rules, and streams the normalised text to an emit callback.
- **The tree-based code stays unchanged as the oracle.** A differential test (fixed cases plus 4 000 random models) compares the two paths byte for byte.

**Tech Stack:** C11, gcc with ASan/UBSan, GNU make, cJSON 1.7.19, TLSF (mattconte/tlsf at `deff9ab509341f264addbd3c8ada533678591905`, BSD).

**Spec:** `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` (§5.5, §9.2–9.4, §10.1, §12.1). **Roadmap:** `stm32/docs/plans/2026-10-14-00-roadmap.md`, row 03.

## Global Constraints

- Arena: **64 KB** static region, TLSF, recreated for each scope. **Peak ≤ 60 KB** for the largest accepted input (§9.3, §12.1).
- Limits in bytes: `CFG_SMALL_MAX 10240`, `CFG_EDGE_MAX 49152` (§9.3, §9.4). Messages are exactly:
  - `These settings are too large.` (413)
  - `The Edge model is too large.` (413)
  - `The request body is not valid JSON.` (400)
  - `Out of memory.` (500)
  - `The Edge model could not be stored.` (emit refused)
- Edge model limits: 64 devices, 5 Modbus TCP devices, 100 points (D6).
- For RFC 8259-valid JSON, the normalised text and the first error message of the text path equal those of `cfg_validate` / `cfg_load` followed by `cJSON_PrintUnformatted`, byte for byte (§9.4 "Ràng buộc kiểm thử").
- Nesting deeper than `JS_MAX_DEPTH` (32) is rejected as invalid JSON. This keeps the scanner's recursion within the `web` task stack.
- Unchanged: the tree-based `cfg_validate`, `cfg_load`, `list()`, `slaves()` and `slaves_strict()` in `cfgmodel.c`. Host build flags stay `-std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined`.
- Run every command from the repository root `/home/linh/projects/SBIOT-Gateway`.

## Review Focus

1. **Body ≤ 10 240 bytes but normalised text larger.** Control characters print as `\u00XX`. The result must be 413 with `These settings are too large.`, never a truncated document. Pinned in Task 3, `test_small` (15 escaped topics).
2. **A device with a huge unknown member** (40 KB of numbers under `junk`) must be accepted without parsing that member. Arena peak < 20 KB. Pinned in Task 4, `test_memory_edges`.
3. **The arena runs out mid-model.** The result is `CFG_NO_MEMORY` with `Out of memory.`, never `CFG_OK` with half a model. Pinned in Task 4, `test_memory_edges` (12 KB arena).
4. **cJSON's own key rules.** `"nodes"` is matched case-insensitively, the first duplicate key wins, keys are compared after decoding escapes (`\u006D`), a key ends at a decoded NUL, and `nodes` given as an object is walked by member. Each must give the tree path's result. Pinned in Task 4, `test_cases`.
5. **An emit callback that refuses** (flash write failed) stops the run with `CFG_ABORTED`. Pinned in Task 4, `test_abort`.

---

### Task 1: TLSF and the JSON arena

**Files:**
- Create: `stm32/firmware/third_party/tlsf/tlsf.c`, `tlsf.h`, `README.md` (vendored, pinned commit)
- Create: `stm32/firmware/components/arena/include/arena.h`, `stm32/firmware/components/arena/arena.c`
- Test: `stm32/firmware/test/host/test_arena.c`
- Modify: `stm32/firmware/test/host/Makefile`

**Interfaces:**
- Consumes: cJSON (`stm32/firmware/third_party/cjson`).
- Produces: `arena_t {mem, size, tlsf, used, peak, failed}`, `arena_init(arena_t*, void*, size_t)`, `arena_begin(arena_t*)`, `arena_end(arena_t*)`, `arena_malloc`, `arena_free`, `arena_owns`, `arena_current()`, `json_alloc_install()`.

- [ ] **Step 1: Vendor TLSF at the pinned commit**

```bash
T=$(mktemp -d) && git clone -q https://github.com/mattconte/tlsf.git $T/tlsf && git -C $T/tlsf checkout -q deff9ab509341f264addbd3c8ada533678591905
mkdir -p stm32/firmware/third_party/tlsf && cp $T/tlsf/tlsf.c $T/tlsf/tlsf.h $T/tlsf/README.md stm32/firmware/third_party/tlsf/
grep -c "Copyright (c) 2006-2016, Matthew Conte" stm32/firmware/third_party/tlsf/tlsf.c
```

Expected: `1`. The BSD licence text is in the header of `tlsf.c`.

- [ ] **Step 2: Write the arena header and the failing test**

Create `stm32/firmware/components/arena/include/arena.h`:

```c
// JSON arena (spec §5.5, §9.3): a fixed region with a TLSF allocator that is
// re-created for every scope, so a request can never fragment it for the next
// one. While a scope is open, cJSON allocates from the arena; at any other time
// it allocates from the heap. Scopes never span a return to an event loop.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *mem;
    size_t size;
    void *tlsf;    // tlsf_t while a scope is open, else NULL
    size_t used;   // bytes held by live allocations, TLSF block overhead included
    size_t peak;   // highest `used` since the scope opened
    bool failed;   // an allocation in this scope returned NULL
} arena_t;

/**
 * Prepares an arena over `mem` (8-byte aligned, larger than TLSF's control block plus pool
 * overhead, about 3.3 KB on the 32-bit target). The region is poisoned until a scope opens.
 */
void arena_init(arena_t *a, void *mem, size_t size);

/** Opens a scope: a fresh allocator over the whole region, and cJSON routed to it. */
void arena_begin(arena_t *a);

/** Closes the scope. Every block from the arena becomes invalid (poisoned under ASan). */
void arena_end(arena_t *a);

void *arena_malloc(arena_t *a, size_t size);
void arena_free(arena_t *a, void *p);
bool arena_owns(const arena_t *a, const void *p);

/** The arena whose scope is open on this task, or NULL. Single-threaded on the host. */
arena_t *arena_current(void);

/** Installs cJSON hooks: blocks come from arena_current() when a scope is open, else from malloc. */
void json_alloc_install(void);

#ifdef __cplusplus
}
#endif
```

Create `stm32/firmware/test/host/test_arena.c`:

```c
// Host tests for components/arena: make -C stm32/firmware/test/host
#include <stdio.h>
#include <string.h>
#include "arena.h"
#include "cJSON.h"
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#endif

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

static _Alignas(8) char region[64 * 1024];

int main(void)
{
    arena_t a;
    arena_init(&a, region, sizeof region);
    json_alloc_install();

    // Outside a scope cJSON uses the heap.
    cJSON *heap = cJSON_CreateString("heap");
    CHECK(heap && !arena_owns(&a, heap), "no scope: heap");

    arena_begin(&a);
    CHECK(arena_current() == &a, "scope is current");
    cJSON *in = cJSON_Parse("{\"k\":[1,2,3],\"s\":\"text\"}");
    CHECK(in && arena_owns(&a, in), "scope: arena");
    size_t live = a.used;
    CHECK(live > 0 && a.peak >= live, "used and peak tracked (%zu, %zu)", live, a.peak);
    cJSON_Delete(in);
    CHECK(a.used == 0, "freed blocks are returned (%zu left)", a.used);
    cJSON_Delete(heap);  // a heap block freed inside a scope still goes back to the heap
    CHECK(arena_malloc(&a, sizeof region) == NULL && a.failed, "an oversized request fails and is flagged");
    arena_end(&a);
    CHECK(arena_current() == NULL, "scope closed");
#if defined(__SANITIZE_ADDRESS__)
    CHECK(__asan_address_is_poisoned(region + 1024), "the region is poisoned after the scope");
#endif

    // A new scope starts from an empty allocator and a fresh peak.
    arena_begin(&a);
    CHECK(a.used == 0 && a.peak == 0 && !a.failed, "fresh scope");
    void *big = arena_malloc(&a, 48 * 1024);
    CHECK(big != NULL, "the whole region is available again");
    arena_end(&a);

    printf(failures ? "%d FAILED\n" : "arena: all tests passed\n", failures);
    return failures != 0;
}
```

Replace `stm32/firmware/test/host/Makefile` with:

```make
# Host tests for the hardware-free modules of the STM32 firmware: make -C stm32/firmware/test/host
CJSON := ../../third_party/cjson
TLSF := ../../third_party/tlsf
INC := -I../../components/cfgmodel/include -I../../components/proto/include -I../../components/arena/include -I$(CJSON) -I$(TLSF)
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined $(INC)
BUILD := build
CFG := ../../components/cfgmodel/cfgmodel.c ../../components/proto/ipacl.c
ARENA := ../../components/arena/arena.c $(TLSF)/tlsf.c

all: $(BUILD)/test_cfgmodel $(BUILD)/test_proto $(BUILD)/test_arena
	$(BUILD)/test_cfgmodel
	$(BUILD)/test_proto
	$(BUILD)/test_arena

$(BUILD)/test_cfgmodel: test_cfgmodel.c $(CFG) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_proto: test_proto.c $(wildcard ../../components/proto/*.c) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_arena: test_arena.c $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
```

(Recipe lines start with a tab.)

- [ ] **Step 3: Run the tests to verify they fail**

Run: `make -C stm32/firmware/test/host`
Expected: the `test_arena` link fails with `undefined reference to 'arena_init'` (and the other `arena_*` functions). `test_cfgmodel` and `test_proto` still build.

- [ ] **Step 4: Implement the arena**

Create `stm32/firmware/components/arena/arena.c`:

```c
#include "arena.h"

#include <assert.h>
#include <stdlib.h>
#include "cJSON.h"
#include "tlsf.h"

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define POISON(p, n) ASAN_POISON_MEMORY_REGION(p, n)
#define UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION(p, n)
#else
#define POISON(p, n) ((void)(p), (void)(n))
#define UNPOISON(p, n) ((void)(p), (void)(n))
#endif

// The open scope. Plan 05 makes this per task (a FreeRTOS thread-local pointer).
static arena_t *s_current;
// The one arena whose blocks json_free must recognise, also after its scope closed.
static arena_t *s_known;

void arena_init(arena_t *a, void *mem, size_t size)
{
    assert(size > tlsf_size() + tlsf_pool_overhead());  // TLSF keeps its control block inside the region
    *a = (arena_t){.mem = mem, .size = size};
    s_known = a;
    POISON(mem, size);
}

void arena_begin(arena_t *a)
{
    assert(!s_current && !a->tlsf);  // no nesting, one scope at a time
    UNPOISON(a->mem, a->size);
    a->tlsf = tlsf_create_with_pool(a->mem, a->size);
    a->used = a->peak = 0;
    a->failed = false;
    s_current = a;
}

void arena_end(arena_t *a)
{
    assert(s_current == a);
    a->tlsf = NULL;
    s_current = NULL;
    POISON(a->mem, a->size);
}

void *arena_malloc(arena_t *a, size_t size)
{
    void *p = a->tlsf ? tlsf_malloc(a->tlsf, size) : NULL;
    if (!p) {
        a->failed = true;
        return NULL;
    }
    a->used += tlsf_block_size(p) + tlsf_alloc_overhead();
    if (a->used > a->peak) a->peak = a->used;
    return p;
}

void arena_free(arena_t *a, void *p)
{
    if (!p || !a->tlsf) return;  // after the scope closed the whole region is gone anyway
    a->used -= tlsf_block_size(p) + tlsf_alloc_overhead();
    tlsf_free(a->tlsf, p);
}

bool arena_owns(const arena_t *a, const void *p)
{
    const char *c = p, *m = a->mem;
    return c >= m && c < m + a->size;
}

arena_t *arena_current(void) { return s_current; }

static void *json_malloc(size_t size)
{
    arena_t *a = arena_current();
    return a ? arena_malloc(a, size) : malloc(size);
}

static void json_free(void *p)
{
    if (s_known && arena_owns(s_known, p)) arena_free(s_known, p);
    else free(p);
}

void json_alloc_install(void)
{
    cJSON_Hooks h = {.malloc_fn = json_malloc, .free_fn = json_free};
    cJSON_InitHooks(&h);
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `make -C stm32/firmware/test/host`
Expected: `cfgmodel: all checks passed`, `proto: all tests passed`, `arena: all tests passed`, exit 0.

- [ ] **Step 6: Commit**

```bash
git add stm32/firmware/third_party/tlsf stm32/firmware/components/arena stm32/firmware/test/host/test_arena.c stm32/firmware/test/host/Makefile
git commit -m "stm32: JSON arena over TLSF with per-scope reset, ASan poisoning and cJSON routing"
```

---

### Task 2: `proto/jsonscan`, a strict JSON scanner

**Files:**
- Create: `stm32/firmware/components/proto/include/jsonscan.h`, `stm32/firmware/components/proto/jsonscan.c`
- Test: `stm32/firmware/test/host/test_jsonscan.c`
- Modify: `stm32/firmware/test/host/Makefile`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `js_span_t {p, n}`;
  - `js_type_t` (`JS_INVALID`, `JS_OBJECT`, `JS_ARRAY`, `JS_STRING`, `JS_NUMBER`, `JS_TRUE`, `JS_FALSE`, `JS_NULL`);
  - `JS_MAX_DEPTH` = 32;
  - `bool js_parse_root(const char*, size_t, js_span_t*)`, `js_type(js_span_t)`;
  - `js_iter_t`, `js_iter_init(js_iter_t*, js_span_t)`, `bool js_iter_next(js_iter_t*, js_span_t *key, js_span_t *val)`, `size_t js_count(js_span_t)`;
  - `bool js_key_is(js_span_t key, const char *lit, bool nocase)`.

- [ ] **Step 1: Write the header and the failing test**

Create `stm32/firmware/components/proto/include/jsonscan.h`:

```c
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
 * True when s[0..n) holds exactly one JSON value, optionally surrounded by
 * whitespace, nested at most JS_MAX_DEPTH deep. On success *root is the value.
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

#ifdef __cplusplus
}
#endif
```

Create `stm32/firmware/test/host/test_jsonscan.c`:

```c
// Host tests for proto/jsonscan.c: make -C stm32/firmware/test/host
#include <stdio.h>
#include <string.h>
#include "jsonscan.h"

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

static bool valid(const char *s)
{
    js_span_t r;
    return js_parse_root(s, strlen(s), &r);
}

static void test_validity(void)
{
    const char *good[] = {"[]", " {} ", "0", "-0.5e+10", "\"a\\u00e9\\n\"", "true", "null", "[1,[2,{\"a\":[]}],\"x\"]",
                          "{\"a\":1,\"a\":2}", "\t[ 1 , 2 ]\r\n"};
    const char *bad[] = {"", " ", "[", "[1,]", "{\"a\"}", "{\"a\":}", "{a:1}", "01", "1.", "+1", ".5", "1e", "\"\\x\"",
                         "\"\\u12g4\"", "\"tab\there\"", "tru", "nul", "[1]x", "[1] [2]", "{\"a\":1,}", "\"open"};
    for (size_t i = 0; i < sizeof good / sizeof *good; i++) CHECK(valid(good[i]), "rejected valid %s", good[i]);
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(!valid(bad[i]), "accepted invalid %s", bad[i]);
    char deep[2 * JS_MAX_DEPTH + 3];
    memset(deep, '[', JS_MAX_DEPTH);
    memset(deep + JS_MAX_DEPTH, ']', JS_MAX_DEPTH);
    deep[2 * JS_MAX_DEPTH] = '\0';
    CHECK(valid(deep), "JS_MAX_DEPTH levels must pass");
    memset(deep, '[', JS_MAX_DEPTH + 1);
    memset(deep + JS_MAX_DEPTH + 1, ']', JS_MAX_DEPTH + 1);
    deep[2 * JS_MAX_DEPTH + 2] = '\0';
    CHECK(!valid(deep), "JS_MAX_DEPTH + 1 levels must fail");
}

static void test_walk(void)
{
    const char *s = "[ {\"a\":1} , [2,3], \"s,]\" ,4 ]";
    js_span_t r, k, v;
    CHECK(js_parse_root(s, strlen(s), &r) && js_type(r) == JS_ARRAY, "root");
    CHECK(js_count(r) == 4, "count %zu", js_count(r));
    js_iter_t it;
    js_iter_init(&it, r);
    const char *want[] = {"{\"a\":1}", "[2,3]", "\"s,]\"", "4"};
    for (int i = 0; i < 4; i++) {
        CHECK(js_iter_next(&it, &k, &v) && v.n == strlen(want[i]) && memcmp(v.p, want[i], v.n) == 0, "element %d", i);
        CHECK(k.n == 0, "array elements have no key");
    }
    CHECK(!js_iter_next(&it, &k, &v), "end of array");

    const char *o = "{ \"x\" : [1] , \"y\":{\"z\":null} }";
    CHECK(js_parse_root(o, strlen(o), &r) && js_type(r) == JS_OBJECT, "object root");
    js_iter_init(&it, r);
    CHECK(js_iter_next(&it, &k, &v) && js_key_is(k, "x", false) && v.n == 3 && js_type(v) == JS_ARRAY, "member x");
    CHECK(js_iter_next(&it, &k, &v) && js_key_is(k, "y", false) && js_type(v) == JS_OBJECT && js_count(v) == 1, "member y");
    CHECK(!js_iter_next(&it, &k, &v), "end of object");

    js_span_t num = {"5", 1};
    CHECK(js_count(num) == 0, "a scalar has no children");
    js_span_t none = {"", 0};
    js_iter_init(&it, none);
    CHECK(!js_iter_next(&it, &k, &v), "an empty span has no children");
}

static void test_keys(void)
{
    js_span_t k1 = {"\"name\"", 6}, k2 = {"\"na\\u006De\"", 11}, k3 = {"\"NoDeS\"", 7}, k4 = {"\"name\\u0000junk\"", 16},
              k5 = {"\"nam\"", 5}, k6 = {"\"n\\u00e9\"", 9};
    CHECK(js_key_is(k1, "name", false), "plain");
    CHECK(js_key_is(k2, "name", false), "escaped letter");
    CHECK(js_key_is(k3, "nodes", true) && !js_key_is(k3, "nodes", false), "case only when asked");
    CHECK(js_key_is(k4, "name", false), "cJSON keys end at a decoded NUL");
    CHECK(!js_key_is(k5, "name", false) && !js_key_is(k1, "nam", false), "prefixes differ");
    CHECK(!js_key_is(k6, "n", false), "non-ASCII never matches");
}

int main(void)
{
    test_validity();
    test_walk();
    test_keys();
    printf(failures ? "%d FAILED\n" : "jsonscan: all tests passed\n", failures);
    return failures != 0;
}
```

Replace `stm32/firmware/test/host/Makefile` with:

```make
# Host tests for the hardware-free modules of the STM32 firmware: make -C stm32/firmware/test/host
CJSON := ../../third_party/cjson
TLSF := ../../third_party/tlsf
INC := -I../../components/cfgmodel/include -I../../components/proto/include -I../../components/arena/include -I$(CJSON) -I$(TLSF)
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined $(INC)
BUILD := build
CFG := ../../components/cfgmodel/cfgmodel.c ../../components/proto/ipacl.c
ARENA := ../../components/arena/arena.c $(TLSF)/tlsf.c

all: $(BUILD)/test_cfgmodel $(BUILD)/test_proto $(BUILD)/test_jsonscan $(BUILD)/test_arena
	$(BUILD)/test_cfgmodel
	$(BUILD)/test_proto
	$(BUILD)/test_jsonscan
	$(BUILD)/test_arena

$(BUILD)/test_cfgmodel: test_cfgmodel.c $(CFG) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_proto: test_proto.c $(wildcard ../../components/proto/*.c) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_jsonscan: test_jsonscan.c ../../components/proto/jsonscan.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_arena: test_arena.c $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make -C stm32/firmware/test/host`
Expected: the `test_jsonscan` link fails with `undefined reference to 'js_parse_root'` (and the other `js_*` functions).

- [ ] **Step 3: Implement the scanner**

Create `stm32/firmware/components/proto/jsonscan.c`:

```c
#include "jsonscan.h"

#include <ctype.h>
#include <string.h>

static size_t skip_ws(const char *s, size_t n, size_t i)
{
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    return i;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Each scanner returns the index just past the value that starts at i, or 0 on a syntax
// error: a value always has at least one character, so a real end is never 0.
static size_t scan_value(const char *s, size_t n, size_t i, int depth);

static size_t scan_string(const char *s, size_t n, size_t i)
{
    for (i++; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') return i + 1;
        if (c < 0x20) return 0;
        if (c != '\\') continue;
        if (++i >= n) return 0;
        c = (unsigned char)s[i];
        if (c == 'u') {
            if (i + 4 >= n) return 0;
            for (int k = 1; k <= 4; k++)
                if (hexval(s[i + k]) < 0) return 0;
            i += 4;
        } else if (!strchr("\"\\/bfnrt", c) || c == '\0') {
            return 0;
        }
    }
    return 0;
}

static size_t scan_number(const char *s, size_t n, size_t i)
{
    if (i < n && s[i] == '-') i++;
    if (i >= n) return 0;
    if (s[i] == '0') {
        i++;
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < n && isdigit((unsigned char)s[i])) i++;
    } else {
        return 0;
    }
    if (i < n && s[i] == '.') {
        if (++i >= n || !isdigit((unsigned char)s[i])) return 0;
        while (i < n && isdigit((unsigned char)s[i])) i++;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        if (++i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (i >= n || !isdigit((unsigned char)s[i])) return 0;
        while (i < n && isdigit((unsigned char)s[i])) i++;
    }
    return i;
}

static size_t scan_literal(const char *s, size_t n, size_t i, const char *lit)
{
    size_t k = strlen(lit);
    return i + k <= n && memcmp(s + i, lit, k) == 0 ? i + k : 0;
}

static size_t scan_container(const char *s, size_t n, size_t i, int depth)
{
    char close = s[i] == '{' ? '}' : ']';
    bool object = close == '}';
    if (depth >= JS_MAX_DEPTH) return 0;
    i = skip_ws(s, n, i + 1);
    if (i < n && s[i] == close) return i + 1;
    for (;;) {
        if (object) {
            if (i >= n || s[i] != '"' || !(i = scan_string(s, n, i))) return 0;
            i = skip_ws(s, n, i);
            if (i >= n || s[i] != ':') return 0;
            i = skip_ws(s, n, i + 1);
        }
        if (!(i = scan_value(s, n, i, depth + 1))) return 0;
        i = skip_ws(s, n, i);
        if (i < n && s[i] == close) return i + 1;
        if (i >= n || s[i] != ',') return 0;
        i = skip_ws(s, n, i + 1);
    }
}

static size_t scan_value(const char *s, size_t n, size_t i, int depth)
{
    if (i >= n) return 0;
    switch (s[i]) {
    case '{':
    case '[': return scan_container(s, n, i, depth);
    case '"': return scan_string(s, n, i);
    case 't': return scan_literal(s, n, i, "true");
    case 'f': return scan_literal(s, n, i, "false");
    case 'n': return scan_literal(s, n, i, "null");
    default: return scan_number(s, n, i);
    }
}

bool js_parse_root(const char *s, size_t n, js_span_t *root)
{
    size_t a = skip_ws(s, n, 0);
    size_t b = scan_value(s, n, a, 0);
    if (!b || skip_ws(s, n, b) != n) return false;
    root->p = s + a;
    root->n = b - a;
    return true;
}

js_type_t js_type(js_span_t v)
{
    if (!v.n) return JS_INVALID;
    switch (v.p[0]) {
    case '{': return JS_OBJECT;
    case '[': return JS_ARRAY;
    case '"': return JS_STRING;
    case 't': return JS_TRUE;
    case 'f': return JS_FALSE;
    case 'n': return JS_NULL;
    default: return JS_NUMBER;
    }
}

void js_iter_init(js_iter_t *it, js_span_t container)
{
    it->in = container;
    js_type_t t = js_type(container);
    it->pos = (t == JS_OBJECT || t == JS_ARRAY) ? 1 : container.n;  // scalars: nothing to walk
}

bool js_iter_next(js_iter_t *it, js_span_t *key, js_span_t *val)
{
    const char *s = it->in.p;
    size_t n = it->in.n, i = skip_ws(s, n, it->pos);
    if (i >= n || s[i] == '}' || s[i] == ']') return false;
    if (s[i] == ',') i = skip_ws(s, n, i + 1);
    js_span_t k = {s + i, 0};
    if (s[0] == '{') {
        size_t e = scan_string(s, n, i);
        k.n = e - i;
        i = skip_ws(s, n, skip_ws(s, n, e) + 1);  // past the ':'
    }
    size_t e = scan_value(s, n, i, 0);
    if (key) *key = k;
    val->p = s + i;
    val->n = e - i;
    it->pos = e;
    return true;
}

size_t js_count(js_span_t container)
{
    js_iter_t it;
    js_span_t v;
    size_t c = 0;
    js_iter_init(&it, container);
    while (js_iter_next(&it, NULL, &v)) c++;
    return c;
}

bool js_key_is(js_span_t key, const char *lit, bool nocase)
{
    if (key.n < 2 || key.p[0] != '"') return false;
    const char *s = key.p + 1, *end = key.p + key.n - 1;
    for (;;) {
        unsigned char c;
        if (s >= end) {
            c = '\0';
        } else if (*s != '\\') {
            c = (unsigned char)*s++;
        } else {
            char e = s[1];
            s += 2;
            if (e == 'u') {
                unsigned cp = 0;
                for (int k = 0; k < 4; k++) cp = cp << 4 | (unsigned)hexval(s[k]);
                s += 4;
                if (cp >= 0x80) return false;  // field names are ASCII
                c = (unsigned char)cp;
            } else {
                c = (unsigned char)(e == 'b' ? '\b' : e == 'f' ? '\f' : e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : e);
            }
        }
        unsigned char l = (unsigned char)*lit;
        if (c == '\0' || l == '\0') return c == l;  // cJSON keys end at a decoded NUL
        if (nocase ? tolower(c) != tolower(l) : c != l) return false;
        lit++;
    }
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make -C stm32/firmware/test/host`
Expected: `jsonscan: all tests passed`, and the earlier suites still pass. `test_proto` now also compiles `jsonscan.c` through its wildcard; that is expected.

- [ ] **Step 5: Commit**

```bash
git add stm32/firmware/components/proto/include/jsonscan.h stm32/firmware/components/proto/jsonscan.c stm32/firmware/test/host/test_jsonscan.c stm32/firmware/test/host/Makefile
git commit -m "stm32 proto: strict JSON scanner that validates and walks text in place"
```

---

### Task 3: `cfg_validate_text`, small configs within the arena

**Files:**
- Modify: `stm32/firmware/components/cfgmodel/include/cfgmodel.h` (insert the block below just before the closing `#ifdef __cplusplus` / `}` / `#endif`)
- Modify: `stm32/firmware/components/cfgmodel/cfgmodel.c` (append the block below at the end of the file)
- Test: `stm32/firmware/test/host/test_text_small.c`
- Modify: `stm32/firmware/test/host/Makefile`

**Interfaces:**
- Consumes: Task 1 (`arena_*`, `json_alloc_install`), Task 2 (`js_parse_root`).
- Produces:
  - `cfg_result_t` (`CFG_OK`, `CFG_INVALID`, `CFG_TOO_LARGE`, `CFG_NO_MEMORY`, `CFG_ABORTED`);
  - `CFG_SMALL_MAX` = 10240, `CFG_EDGE_MAX` = 49152;
  - `cfg_result_t cfg_validate_text(const cfg_env_t*, const char *name, const char *text, size_t len, const cJSON *current, char *out, size_t out_size, char *err, size_t errlen)`.

  Plan 06 (`webapi`) calls it with `out_size = CFG_SMALL_MAX + 1`.

- [ ] **Step 1: Declare the API and write the failing test**

Insert into `cfgmodel.h`, directly after the `cfg_load` declaration:

```c
// ---- Text in, text out (STM32: bounded memory, spec §9.3-9.4) -------------------------

typedef enum {
    CFG_OK = 0,
    CFG_INVALID,    /**< the document breaks the schema or is not JSON: `err` says why (HTTP 400) */
    CFG_TOO_LARGE,  /**< the normalised text would exceed `max_out` (HTTP 413) */
    CFG_NO_MEMORY,  /**< an allocation failed (HTTP 500) */
    CFG_ABORTED,    /**< the emit callback returned false, e.g. a flash write failed */
} cfg_result_t;

/** Exact limits of the accepted normalised text, in bytes (spec §9.3, §9.4). */
#define CFG_SMALL_MAX 10240
#define CFG_EDGE_MAX 49152

/**
 * One config other than edgeDevices, from JSON text to normalised JSON text in
 * `out` (NUL-terminated, at most out_size - 1 bytes; pass CFG_SMALL_MAX + 1).
 * Same rules and messages as cfg_validate. The input tree is freed before the
 * output is printed, and the output is printed into `out` without reallocation,
 * which keeps the arena peak low.
 */
cfg_result_t cfg_validate_text(const cfg_env_t *env, const char *name, const char *text, size_t len,
                               const cJSON *current, char *out, size_t out_size, char *err, size_t errlen);
```

Create `stm32/firmware/test/host/test_text_small.c`:

```c
// cfg_validate_text: the round trip, the 10 240-byte limit and the arena peak for the
// configs other than edgeDevices (spec §9.3): make -C stm32/firmware/test/host
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arena.h"
#include "cfgmodel.h"

static const cfg_env_t ENV = {.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};
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

static _Alignas(8) char region[64 * 1024];
static arena_t s_arena;

/** Small configs: the round trip holds, an oversized normalised text is 413, and the arena peak fits. */
static void test_small(void)
{
    static char in[CFG_SMALL_MAX + 1], out[CFG_SMALL_MAX + 1], again[CFG_SMALL_MAX + 1];
    char err[200];
    size_t o = (size_t)snprintf(in, sizeof in, "[");
    for (int i = 0; i < 16; i++) {
        char t[129], id[65], d[17];
        memset(t, 't', 128), t[128] = 0, memset(id, 'i', 64), id[64] = 0, memset(d, 'd', 16), d[16] = 0;
        o += (size_t)snprintf(in + o, sizeof in - o,
                              "%s{\"enabled\":true,\"mode\":1,\"topic\":\"%s\",\"ports\":3,\"qos\":2,\"retain\":true,\"ioControl\":true,"
                              "\"identifier\":\"%s\",\"delimiter\":\"%s\"}",
                              i ? "," : "", t, id, d);
    }
    snprintf(in + o, sizeof in - o, "]");
    arena_begin(&s_arena);
    cfg_result_t r = cfg_validate_text(&ENV, "mqttPublish", in, strlen(in), NULL, out, sizeof out, err, sizeof err);
    size_t peak = s_arena.peak;
    arena_end(&s_arena);
    CHECK(r == CFG_OK, "16 full topics rejected: %d %s", r, err);
    CHECK(peak <= 60 * 1024, "arena peak %zu bytes for 16 full topics (limit 60 KB)", peak);
    printf("  16 full topics: %zu bytes, arena peak %zu bytes\n", strlen(out), peak);
    r = cfg_validate_text(&ENV, "mqttPublish", out, strlen(out), NULL, again, sizeof again, err, sizeof err);
    CHECK(r == CFG_OK && strcmp(out, again) == 0, "POST -> GET -> POST changes the text");

    // Control characters print as \u00XX: 15 topics fit the input limit but not the output one.
    o = (size_t)snprintf(in, sizeof in, "[");
    for (int i = 0; i < 15; i++) {
        o += (size_t)snprintf(in + o, sizeof in - o, "%s{\"topic\":\"", i ? "," : "");
        for (int k = 0; k < 128; k++) in[o++] = 't';
        o += (size_t)snprintf(in + o, sizeof in - o, "\",\"identifier\":\"");
        for (int k = 0; k < 64; k++) o += (size_t)snprintf(in + o, sizeof in - o, "\\u0001");
        o += (size_t)snprintf(in + o, sizeof in - o, "\",\"delimiter\":\"");
        for (int k = 0; k < 16; k++) o += (size_t)snprintf(in + o, sizeof in - o, "\\u0001");
        o += (size_t)snprintf(in + o, sizeof in - o, "\"}");
    }
    snprintf(in + o, sizeof in - o, "]");
    CHECK(strlen(in) <= CFG_SMALL_MAX, "the escaped payload must fit the input limit (%zu)", strlen(in));
    r = cfg_validate_text(&ENV, "mqttPublish", in, strlen(in), NULL, out, sizeof out, err, sizeof err);
    CHECK(r == CFG_TOO_LARGE && strcmp(err, "These settings are too large.") == 0, "escaped topics: %d %s", r, err);

    // Every config's defaults round-trip; a schema error keeps its message; bad JSON is named.
    const char *names[16];
    size_t n = cfg_names(&ENV, names, 16);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(names[i], "edgeDevices") == 0) continue;
        cJSON *d = cfg_default(&ENV, names[i]);
        char *t = cJSON_PrintUnformatted(d);
        r = cfg_validate_text(&ENV, names[i], t, strlen(t), NULL, out, sizeof out, err, sizeof err);
        CHECK(r == CFG_OK && strcmp(out, t) == 0, "%s defaults do not round-trip (%d %s)", names[i], r, err);
        free(t);
        cJSON_Delete(d);
    }
    r = cfg_validate_text(&ENV, "port1", "{\"baudRate\":300}", 16, NULL, out, sizeof out, err, sizeof err);
    CHECK(r == CFG_INVALID && strcmp(err, "baudRate must be 600 to 230400.") == 0, "schema error: %d %s", r, err);
    r = cfg_validate_text(&ENV, "port1", "{\"baudRate\":", 12, NULL, out, sizeof out, err, sizeof err);
    CHECK(r == CFG_INVALID && strcmp(err, "The request body is not valid JSON.") == 0, "bad JSON: %d %s", r, err);
}

int main(void)
{
    arena_init(&s_arena, region, sizeof region);
    json_alloc_install();
    test_small();
    printf(failures ? "%d FAILED\n" : "text_small: all tests passed\n", failures);
    return failures != 0;
}
```

Replace `stm32/firmware/test/host/Makefile` with:

```make
# Host tests for the hardware-free modules of the STM32 firmware: make -C stm32/firmware/test/host
CJSON := ../../third_party/cjson
TLSF := ../../third_party/tlsf
INC := -I../../components/cfgmodel/include -I../../components/proto/include -I../../components/arena/include -I$(CJSON) -I$(TLSF)
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined $(INC)
BUILD := build
CFG := ../../components/cfgmodel/cfgmodel.c ../../components/proto/ipacl.c ../../components/proto/jsonscan.c
ARENA := ../../components/arena/arena.c $(TLSF)/tlsf.c

all: $(BUILD)/test_cfgmodel $(BUILD)/test_proto $(BUILD)/test_jsonscan $(BUILD)/test_arena $(BUILD)/test_text_small
	$(BUILD)/test_cfgmodel
	$(BUILD)/test_proto
	$(BUILD)/test_jsonscan
	$(BUILD)/test_arena
	$(BUILD)/test_text_small

$(BUILD)/test_cfgmodel: test_cfgmodel.c $(CFG) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_proto: test_proto.c $(wildcard ../../components/proto/*.c) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_jsonscan: test_jsonscan.c ../../components/proto/jsonscan.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_arena: test_arena.c $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_text_small: test_text_small.c $(CFG) $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make -C stm32/firmware/test/host`
Expected: the `test_text_small` link fails with `undefined reference to 'cfg_validate_text'`.

- [ ] **Step 3: Implement**

Append to the end of `stm32/firmware/components/cfgmodel/cfgmodel.c`:

```c

// ---- Text in, text out ----------------------------------------------------------------
// The STM32 has no PSRAM: a whole Edge model as a cJSON tree does not fit (spec §9.2). These
// functions keep the same rules and messages as the tree-based code above, which stays the
// reference: test/host/test_edge_text.c checks the two against each other byte for byte.

#include "jsonscan.h"

static const char NOT_JSON[] = "The request body is not valid JSON.";

cfg_result_t cfg_validate_text(const cfg_env_t *env, const char *name, const char *text, size_t len,
                               const cJSON *current, char *out, size_t out_size, char *err, size_t errlen)
{
    js_span_t root;
    if (!js_parse_root(text, len, &root)) return fail(err, errlen, NOT_JSON, NULL, NULL), CFG_INVALID;
    cJSON *in = cJSON_ParseWithLength(root.p, root.n);
    if (!in) return fail(err, errlen, "Out of memory.", NULL, NULL), CFG_NO_MEMORY;
    char why[160] = "";
    cJSON *doc = cfg_validate(env, name, in, current, why, sizeof why);
    cJSON_Delete(in);  // before printing: the peak is the input or the output, not both
    if (!doc) {
        bool oom = !*why;  // every schema error writes a message; only a failed allocation leaves none
        fail(err, errlen, oom ? "Out of memory." : "%s", why, NULL);
        return oom ? CFG_NO_MEMORY : CFG_INVALID;
    }
    bool fits = cJSON_PrintPreallocated(doc, out, (int)out_size, false);
    cJSON_Delete(doc);
    if (!fits) return fail(err, errlen, "These settings are too large.", NULL, NULL), CFG_TOO_LARGE;
    return CFG_OK;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make -C stm32/firmware/test/host`
Expected:
- the output contains `  16 full topics: 5201 bytes, arena peak` followed by a number ≤ 61440 (about 41 800 on the 64-bit host);
- `text_small: all tests passed`;
- every earlier suite still passes.

- [ ] **Step 5: Commit**

```bash
git add stm32/firmware/components/cfgmodel stm32/firmware/test/host/test_text_small.c stm32/firmware/test/host/Makefile
git commit -m "stm32 cfgmodel: cfg_validate_text with the 10 240-byte limit and a bounded arena peak"
```

---

### Task 4: `cfg_edge_text`, edgeDevices node by node

**Files:**
- Modify: `stm32/firmware/components/cfgmodel/include/cfgmodel.h` (insert after the Task 3 block)
- Modify: `stm32/firmware/components/cfgmodel/cfgmodel.c` (append at the end)
- Test: `stm32/firmware/test/host/test_edge_text.c`
- Modify: `stm32/firmware/test/host/Makefile`
- Modify: `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` (the hash sentence in §9.4)
- Modify: `stm32/docs/plans/2026-10-14-00-roadmap.md` (the API name in row 03)

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces:
  - `typedef bool (*cfg_emit_fn)(void *ctx, const char *text, size_t len)`;
  - `cfg_result_t cfg_edge_text(const cfg_env_t*, const char *text, size_t len, bool strict, size_t max_out, cfg_emit_fn emit, void *ctx, char *err, size_t errlen)`.

  Plan 05 (`store`) emits into 8 KB FlashDB records and commits the generation only on `CFG_OK`. Plan 06 calls it with `strict = true`; boot calls it with `strict = false`.

- [ ] **Step 1: Declare the API and write the failing test**

Insert into `cfgmodel.h`, after the Task 3 block:

```c
/** Receives the normalised edgeDevices text in order; returns false to stop. */
typedef bool (*cfg_emit_fn)(void *ctx, const char *text, size_t len);

/**
 * edgeDevices from JSON text, one device header and one node at a time, never
 * building the whole tree. With `strict` it applies cfg_validate's save-time
 * rules, without them cfg_load's. For any valid JSON the emitted text and the
 * first error message are byte-identical to cfg_validate / cfg_load followed
 * by cJSON_PrintUnformatted. Emitted text is only meaningful when the result
 * is CFG_OK: a caller writes it to a new generation and commits it only then.
 */
cfg_result_t cfg_edge_text(const cfg_env_t *env, const char *text, size_t len, bool strict, size_t max_out,
                           cfg_emit_fn emit, void *ctx, char *err, size_t errlen);
```

Create `stm32/firmware/test/host/test_edge_text.c`:

```c
// cfg_edge_text against the tree-based cfg_validate / cfg_load, byte for byte, plus the
// arena limit of spec §9.4: make -C stm32/firmware/test/host
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arena.h"
#include "cfgmodel.h"

static const cfg_env_t ENV = {.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};
static int failures, compared;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

typedef struct {
    char *p;
    size_t n, cap;
} sbuf_t;

static bool sink(void *ctx, const char *s, size_t n)
{
    sbuf_t *b = ctx;
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
    return true;
}

/** The reference: the tree-based model, printed the way the ESP32 firmware stores it. */
static bool oracle(const char *json, bool strict, char **text, char *err, size_t errlen)
{
    *text = NULL;
    err[0] = '\0';
    cJSON *in = cJSON_Parse(json);
    if (!in) return snprintf(err, errlen, "The request body is not valid JSON."), false;
    cJSON *out = strict ? cfg_validate(&ENV, "edgeDevices", in, NULL, err, errlen) : cfg_load(&ENV, "edgeDevices", in, err, errlen);
    cJSON_Delete(in);
    if (!out) return false;
    *text = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return true;
}

/** One payload through both paths; they must agree on result, message and text. */
static void same(const char *json, bool strict, const char *label)
{
    char e1[200], e2[200] = "";
    char *t1;
    bool ok1 = oracle(json, strict, &t1, e1, sizeof e1);
    sbuf_t b = {0};
    cfg_result_t r = cfg_edge_text(&ENV, json, strlen(json), strict, CFG_EDGE_MAX, sink, &b, e2, sizeof e2);
    compared++;
    if (ok1) {
        CHECK(r == CFG_OK, "%s: text path failed (%d: %s) where the tree path passed", label, r, e2);
        CHECK(r != CFG_OK || (b.p && strcmp(t1, b.p) == 0), "%s: normalised text differs\n  tree: %s\n  text: %s", label, t1,
              b.p ? b.p : "(none)");
    } else {
        CHECK(r == CFG_INVALID, "%s: text path returned %d where the tree path said \"%s\"", label, r, e1);
        CHECK(strcmp(e1, e2) == 0, "%s: messages differ\n  tree: %s\n  text: %s", label, e1, e2);
    }
    free(t1);
    free(b.p);
}

static void both(const char *json, const char *label)
{
    same(json, true, label);
    same(json, false, label);
}

static void test_cases(void)
{
    both("[]", "empty");
    both("{}", "not a list");
    both("[1]", "device not an object");
    both("[{\"name\":\"a\"}]", "one device, no nodes key");
    both("[{\"name\":\"a\",\"nodes\":[]}]", "empty nodes");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"p\"}]}]", "one point");
    both("[{\"name\":\"a\",\"nodes\":{\"x\":{\"name\":\"p\"},\"y\":{\"name\":\"q\"}}}]", "nodes as an object");
    both("[{\"name\":\"a\",\"nodes\":7}]", "nodes as a number");
    both("[{\"name\":\"a\",\"Nodes\":[{\"name\":\"p\"}]}]", "Nodes in another case");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"p\"}],\"NODES\":[{\"name\":\"q\"}]}]", "two nodes keys");
    both("[{\"na\\u006de\":\"esc\",\"nodes\":[{\"n\\u0061me\":\"p\"}]}]", "escaped keys");
    both("[{\"name\\u0000x\":\"nul\"}]", "key with a NUL");
    both("[{\"name\":\"a\",\"name\":\"b\"}]", "duplicate key: first wins");
    both("[{\"name\":\"a\",\"junk\":[1,2,{\"deep\":true}]}]", "unknown keys dropped");
    both("[{\"name\":\"a\",\"nodes\":[5]}]", "point not an object");
    both("[{\"name\":\"a\",\"uart\":3}]", "header range error");
    both("[{\"name\":\"a\"},{\"name\":\"b\",\"uart\":\"x\"}]", "second header bad");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"p\",\"fc\":9}]},{\"name\":\"b\",\"uart\":7}]", "header errors come before points");
    both("[{\"name\":\"bad name\"}]", "invalid device name");
    both("[{\"name\":\"SBIOT_IO\"}]", "reserved name");
    both("[{\"name\":\"d\"},{\"name\":\"d\"}]", "duplicate device");
    both("[{\"name\":\"a\",\"source\":\"io\"},{\"name\":\"b\",\"source\":\"io\"}]", "two IO devices");
    both("[{\"name\":\"a\",\"source\":\"virtual\"},{\"name\":\"b\",\"source\":\"virtual\"}]", "two virtual devices");
    both("[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"ioPoint\":\"AI2\"},{\"ioPoint\":\"DO1\"},{\"ioPoint\":\"DI2\"}]}]", "IO points renamed");
    both("[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"ioPoint\":\"AI3\"}]}]", "IO channel missing");
    both("[{\"name\":\"v\",\"source\":\"virtual\",\"addr\":\"200\",\"nodes\":[{\"name\":\"x\",\"dtype\":\"bool\"}]}]", "virtual bool");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"p\"}]},{\"name\":\"b\",\"nodes\":[{\"name\":\"p\"}]}]", "duplicate point across devices");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"bad name\"}]}]", "invalid point name");
    both("[{\"name\":\"m\",\"protocol\":\"dlt645\",\"addr\":\"123\"}]", "DL/T645 address");
    both("[{\"name\":\"m\",\"protocol\":\"dlt645\",\"addr\":\"123456789012\",\"nodes\":[{\"name\":\"v\",\"di\":\"0201ZZ00\"}]}]", "DL/T645 di");
    both("[{\"name\":\"m\",\"protocol\":\"dlt645\",\"addr\":\"123456789012\",\"nodes\":[{\"name\":\"v\",\"di\":\"02010100\",\"dtype\":\"bool\"}]}]", "DL/T645 bool");
    both("[{\"name\":\"a\",\"addr\":\"0\"}]", "slave id 0");
    both("[{\"name\":\"a\",\"addr\":\"256\"}]", "slave id 256");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"c\",\"fc\":1,\"dtype\":\"uint16\"}]}]", "coil not bool");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"h\",\"fc\":3,\"dtype\":\"bool\"}]}]", "register bool");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"r\",\"reg\":65534,\"dtype\":\"float32_abcd\"}]}]", "registers past 65535");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"r\",\"mapEn\":true,\"mapAddr\":65535,\"dtype\":\"float64\"}]}]", "mapped past 65535");
    both("[{\"name\":\"a\",\"nodes\":[{\"name\":\"x\",\"reg\":10,\"dtype\":\"float32_abcd\"},{\"name\":\"y\",\"mapEn\":true,\"mapAddr\":11}]}]",
         "mapped overlaps an unmapped point");
    both("[{\"name\":\"a\",\"addr\":\"5\"},{\"name\":\"b\",\"addr\":\"6\",\"mapEn\":true,\"mapAddr\":5}]", "mapped id taken by an unmapped device");
    both("[{\"name\":\"v\",\"source\":\"virtual\",\"addr\":\"7\"},{\"name\":\"b\",\"addr\":\"7\"}]", "virtual id taken");
    char many[16384];
    size_t o = (size_t)snprintf(many, sizeof many, "[");
    for (int i = 0; i < 65; i++) o += (size_t)snprintf(many + o, sizeof many - o, "%s{\"name\":\"d%d\"}", i ? "," : "", i);
    snprintf(many + o, sizeof many - o, "]");
    both(many, "65 devices");
    o = (size_t)snprintf(many, sizeof many, "[");
    for (int i = 0; i < 6; i++) o += (size_t)snprintf(many + o, sizeof many - o, "%s{\"name\":\"e%d\",\"source\":\"eth\",\"addr\":\"%d\"}", i ? "," : "", i, i + 1);
    snprintf(many + o, sizeof many - o, "]");
    both(many, "6 Modbus TCP devices");
    o = (size_t)snprintf(many, sizeof many, "[{\"name\":\"a\",\"nodes\":[");
    for (int i = 0; i < 101; i++) o += (size_t)snprintf(many + o, sizeof many - o, "%s{\"name\":\"p%d\",\"reg\":%d}", i ? "," : "", i, i);
    snprintf(many + o, sizeof many - o, "]}]");
    both(many, "101 points");
}

// ---- Random models: every field drawn from a pool of valid and invalid values ----------

static unsigned s_rng = 12345;
static unsigned rnd(unsigned n) { return (s_rng = s_rng * 1103515245u + 12345u) / 65536u % n; }
static const char *pick(const char *const *pool, size_t n) { return pool[rnd((unsigned)n)]; }
#define PICK(pool) pick(pool, sizeof pool / sizeof *pool)

static const char *const NAMES[] = {"\"a\"", "\"b\"", "\"meter_1\"", "\"SBIOT_IO\"", "\"bad name\"", "\"\"", "\"abcdefghijklmnopq\"", "7"};
static const char *const SRC[] = {"\"uart\"", "\"uart\"", "\"eth\"", "\"io\"", "\"virtual\"", "\"bogus\""};
static const char *const PROTO[] = {"\"modbus\"", "\"dlt645\"", "\"x\""};
static const char *const ADDR[] = {"\"1\"", "\"5\"", "\"255\"", "\"0\"", "\"256\"", "\"abc\"", "\"123456789012\"", "\"1234567890123\"", "5"};
static const char *const BOOLS[] = {"true", "false", "\"1\"", "0", "\"x\""};
static const char *const SMALL[] = {"1", "5", "200", "0", "255", "256", "\"7\""};
static const char *const PNAMES[] = {"\"p\"", "\"q\"", "\"r\"", "\"t_1\"", "\"bad name\"", "\"\""};
static const char *const FC[] = {"1", "2", "3", "4", "3", "5", "\"3\""};
static const char *const REG[] = {"0", "1", "10", "11", "65534", "65535", "70000", "-1"};
static const char *const DTYPE[] = {"\"uint16\"", "\"bool\"", "\"float32_abcd\"", "\"float64\"", "\"int32_cdab\"", "\"nope\""};
static const char *const DI[] = {"\"02010100\"", "\"0201ZZ00\"", "\"\"", "\"123\""};
static const char *const MAPADDR[] = {"0", "10", "11", "65534", "65535", "70000"};
static const char *const IOPT[] = {"\"DO1\"", "\"DO2\"", "\"DI2\"", "\"AI1\"", "\"AI2\"", "\"AI3\"", "\"\""};

static size_t put(char *b, size_t o, size_t cap, const char *k, const char *v, bool *first)
{
    if (rnd(5) == 0) return o;  // leave the key out: the default applies
    o += (size_t)snprintf(b + o, cap - o, "%s\"%s\":%s", *first ? "" : ",", k, v);
    *first = false;
    return o;
}

static void random_model(char *b, size_t cap)
{
    size_t o = (size_t)snprintf(b, cap, "[");
    int ndev = (int)rnd(5);
    for (int d = 0; d < ndev; d++) {
        bool first = true;
        o += (size_t)snprintf(b + o, cap - o, "%s{", d ? "," : "");
        o = put(b, o, cap, "name", PICK(NAMES), &first);
        o = put(b, o, cap, "source", PICK(SRC), &first);
        o = put(b, o, cap, "protocol", PICK(PROTO), &first);
        o = put(b, o, cap, "addr", PICK(ADDR), &first);
        o = put(b, o, cap, "mapEn", PICK(BOOLS), &first);
        o = put(b, o, cap, "mapAddr", PICK(SMALL), &first);
        if (rnd(4) == 0) o = put(b, o, cap, "junk", "{\"x\":[1,2]}", &first);
        o += (size_t)snprintf(b + o, cap - o, "%s\"nodes\":[", first ? "" : ",");
        int nn = (int)rnd(5);
        for (int i = 0; i < nn; i++) {
            bool f2 = true;
            o += (size_t)snprintf(b + o, cap - o, "%s{", i ? "," : "");
            o = put(b, o, cap, "name", PICK(PNAMES), &f2);
            o = put(b, o, cap, "fc", PICK(FC), &f2);
            o = put(b, o, cap, "reg", PICK(REG), &f2);
            o = put(b, o, cap, "dtype", PICK(DTYPE), &f2);
            o = put(b, o, cap, "di", PICK(DI), &f2);
            o = put(b, o, cap, "mapEn", PICK(BOOLS), &f2);
            o = put(b, o, cap, "mapAddr", PICK(MAPADDR), &f2);
            o = put(b, o, cap, "ioPoint", PICK(IOPT), &f2);
            o += (size_t)snprintf(b + o, cap - o, "}");
        }
        o += (size_t)snprintf(b + o, cap - o, "]}");
    }
    snprintf(b + o, cap - o, "]");
}

static void test_random(void)
{
    char b[8192];
    int before = failures;
    for (int i = 0; i < 4000 && failures - before < 5; i++) {
        random_model(b, sizeof b);
        char label[32];
        snprintf(label, sizeof label, "random #%d", i);
        both(b, label);
    }
}

// ---- Limits and the arena -------------------------------------------------------------

static _Alignas(8) char region[64 * 1024];
static arena_t s_arena;

/** The largest model this firmware accepts: 64 devices, 100 points, every text field at its limit. */
static void largest_model(char *b, size_t cap)
{
    size_t o = (size_t)snprintf(b, cap, "[");
    for (int d = 0, k = 0; d < 64; d++) {
        o += (size_t)snprintf(b + o, cap - o,
                              "%s{\"name\":\"device_%02d_abcdef\",\"source\":\"uart\",\"uart\":%d,\"ip\":\"192.168.100.200\",\"ethPort\":502,"
                              "\"protocol\":\"modbus\",\"addr\":\"%d\",\"poll\":1000,\"mapEn\":false,\"mapAddr\":1,\"merge\":true,\"nodes\":[",
                              d ? "," : "", d, 1 + d % 2, d + 1);
        int per = 100 / 64 + (d < 100 % 64);
        for (int i = 0; i < per; i++, k++) {
            o += (size_t)snprintf(b + o, cap - o,
                                  "%s{\"name\":\"point_%03d_abcdef\",\"fc\":3,\"reg\":%d,\"di\":\"\",\"dtype\":\"float32_abcd\",\"timeout\":200,"
                                  "\"onChange\":true,\"range\":1,\"mapEn\":false,\"mapAddr\":0,\"formula\":\"=%%s*0.1+0.123456789-0.123456789*1.000000001+12345.6789-98765.43\","
                                  "\"ioPoint\":\"\",\"ioReport\":0,\"upper\":0,\"lower\":0}",
                                  i ? "," : "", k, i * 2);
        }
        o += (size_t)snprintf(b + o, cap - o, "]}");
    }
    snprintf(b + o, cap - o, "]");
}

static void test_limits(void)
{
    static char b[65536];
    largest_model(b, sizeof b);
    char err[200];
    sbuf_t out = {0};

    arena_begin(&s_arena);
    cfg_result_t r = cfg_edge_text(&ENV, b, strlen(b), true, CFG_EDGE_MAX, sink, &out, err, sizeof err);
    size_t peak = s_arena.peak;
    arena_end(&s_arena);
    CHECK(r == CFG_OK, "largest model rejected (%d: %s)", r, err);
    CHECK(out.n <= CFG_EDGE_MAX, "largest model normalises to %zu bytes", out.n);
    CHECK(peak <= 60 * 1024, "arena peak %zu bytes for the largest model (limit 60 KB)", peak);
    printf("  largest Edge model: %zu bytes in, %zu bytes out, arena peak %zu bytes\n", strlen(b), out.n, peak);

    // The normalised text over the limit: 413, and nothing is valid to keep.
    out.n = 0;
    r = cfg_edge_text(&ENV, b, strlen(b), true, 1000, sink, &out, err, sizeof err);
    CHECK(r == CFG_TOO_LARGE && strcmp(err, "The Edge model is too large.") == 0, "small max_out: %d %s", r, err);
    free(out.p);
}

/** Unknown keys are skipped without being parsed, and a full arena is reported, never half a model. */
static void test_memory_edges(void)
{
    static char b[49152];
    char err[200];
    size_t o = (size_t)snprintf(b, sizeof b, "[{\"name\":\"a\",\"junk\":[");
    while (o < 40000) o += (size_t)snprintf(b + o, sizeof b - o, "1,");
    snprintf(b + o, sizeof b - o, "1],\"nodes\":[{\"name\":\"p\"}]}]");
    sbuf_t out = {0};
    arena_begin(&s_arena);
    cfg_result_t r = cfg_edge_text(&ENV, b, strlen(b), true, CFG_EDGE_MAX, sink, &out, err, sizeof err);
    size_t peak = s_arena.peak;
    arena_end(&s_arena);
    CHECK(r == CFG_OK, "a 40 KB unknown key must be skipped (%d: %s)", r, err);
    CHECK(peak < 20 * 1024, "the unknown key was parsed: arena peak %zu bytes", peak);
    free(out.p);

    static _Alignas(8) char tiny[12 * 1024];
    arena_t small;
    arena_init(&small, tiny, sizeof tiny);
    largest_model(b, sizeof b);
    out = (sbuf_t){0};
    arena_begin(&small);
    r = cfg_edge_text(&ENV, b, strlen(b), true, CFG_EDGE_MAX, sink, &out, err, sizeof err);
    arena_end(&small);
    arena_init(&s_arena, region, sizeof region);  // json_free must keep recognising the main arena
    CHECK(r == CFG_NO_MEMORY && strcmp(err, "Out of memory.") == 0, "a full arena: %d %s", r, err);
    free(out.p);
}

static bool refuse(void *ctx, const char *s, size_t n) { (void)ctx, (void)s, (void)n; return false; }

static void test_abort(void)
{
    char err[200];
    cfg_result_t r = cfg_edge_text(&ENV, "[{\"name\":\"a\"}]", 14, true, CFG_EDGE_MAX, refuse, NULL, err, sizeof err);
    CHECK(r == CFG_ABORTED, "emit failure: %d %s", r, err);
}

int main(void)
{
    test_cases();
    test_random();
    arena_init(&s_arena, region, sizeof region);
    json_alloc_install();
    test_limits();
    test_abort();
    test_memory_edges();
    if (failures) printf("%d FAILED (%d payloads compared)\n", failures, compared);
    else printf("edge_text: all tests passed (%d payloads compared)\n", compared);
    return failures != 0;
}
```

Replace `stm32/firmware/test/host/Makefile` with:

```make
# Host tests for the hardware-free modules of the STM32 firmware: make -C stm32/firmware/test/host
CJSON := ../../third_party/cjson
TLSF := ../../third_party/tlsf
INC := -I../../components/cfgmodel/include -I../../components/proto/include -I../../components/arena/include -I$(CJSON) -I$(TLSF)
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined $(INC)
BUILD := build
CFG := ../../components/cfgmodel/cfgmodel.c ../../components/proto/ipacl.c ../../components/proto/jsonscan.c
ARENA := ../../components/arena/arena.c $(TLSF)/tlsf.c

all: $(BUILD)/test_cfgmodel $(BUILD)/test_proto $(BUILD)/test_jsonscan $(BUILD)/test_arena $(BUILD)/test_text_small $(BUILD)/test_edge_text
	$(BUILD)/test_cfgmodel
	$(BUILD)/test_proto
	$(BUILD)/test_jsonscan
	$(BUILD)/test_arena
	$(BUILD)/test_text_small
	$(BUILD)/test_edge_text

$(BUILD)/test_cfgmodel: test_cfgmodel.c $(CFG) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_proto: test_proto.c $(wildcard ../../components/proto/*.c) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_jsonscan: test_jsonscan.c ../../components/proto/jsonscan.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_arena: test_arena.c $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_text_small: test_text_small.c $(CFG) $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_edge_text: test_edge_text.c $(CFG) $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make -C stm32/firmware/test/host`
Expected: the `test_edge_text` link fails with `undefined reference to 'cfg_edge_text'`.

- [ ] **Step 3: Implement**

Append to the end of `stm32/firmware/components/cfgmodel/cfgmodel.c`:

```c
/** What the save-time rules need to know about one device, kept between passes. */
typedef struct {
    char name[17];
    char addr[13];
    bool dlt, own, virt, io, map_en;
    int map_addr;
} edev_t;

/** What the save-time rules need to know about one point. */
typedef struct {
    int dev;
    char name[17];
    char di[9];
    int fc, reg, regs, map_addr;
    bool is_bool, map_en;
} enode_t;

typedef struct {
    edev_t dev[64];
    enode_t node[MAX_NODES];
    int ndev, nnode;
} esum_t;

typedef struct {
    cfg_emit_fn emit;
    void *ctx;
    size_t used, max;
    cfg_result_t r;
} eout_t;

static bool out_put(eout_t *o, const char *s, size_t n)
{
    if (o->r != CFG_OK) return false;
    if (o->used + n > o->max) return o->r = CFG_TOO_LARGE, false;
    if (!o->emit(o->ctx, s, n)) return o->r = CFG_ABORTED, false;
    o->used += n;
    return true;
}

/** Builds the object cJSON would see, keeping only the schema's keys (first occurrence wins later). */
static cJSON *pick(js_span_t obj, const field_t *fields, size_t n)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    js_iter_t it;
    js_span_t k, v;
    js_iter_init(&it, obj);
    while (js_iter_next(&it, &k, &v)) {
        for (size_t i = 0; i < n; i++) {
            if (!js_key_is(k, fields[i].key, false)) continue;
            cJSON *val = cJSON_ParseWithLength(v.p, v.n);
            if (!val) {
                cJSON_Delete(o);
                return NULL;
            }
            cJSON_AddItemToObject(o, fields[i].key, val);
            break;
        }
    }
    return o;
}

/** The value of the first member named "nodes" in any case, as cJSON_GetObjectItem finds it. */
static js_span_t nodes_of(js_span_t dev)
{
    js_iter_t it;
    js_span_t k, v, none = {"", 0};
    if (js_type(dev) != JS_OBJECT) return none;
    js_iter_init(&it, dev);
    while (js_iter_next(&it, &k, &v))
        if (js_key_is(k, "nodes", true)) return v;
    return none;
}

/** One device through the header schema: NULL with `err` set on a schema error, or on no memory. */
static cJSON *edge_header(const cfg_env_t *env, js_span_t dev, const cJSON *base, char *err, size_t errlen)
{
    if (js_type(dev) != JS_OBJECT) return fail(err, errlen, "Expected an object for %s.", "edgeDevices", NULL), NULL;
    cJSON *in = pick(dev, device_fields, N(device_fields));
    if (!in) return NULL;
    cJSON *s = object(env, "edgeDevices", device_fields, N(device_fields), in, base, err, errlen);
    cJSON_Delete(in);
    return s;
}

static int unit_of(const edev_t *d) { return d->map_en ? d->map_addr : d->own ? atoi(d->addr) : -1; }

static bool span_of(const edev_t *d, const enode_t *n, int *fc, long *start, int *width)
{
    if (d->dlt && !n->map_en) return false;
    *fc = d->dlt ? 3 : n->fc;
    *start = n->map_en ? n->map_addr : n->reg;
    *width = *fc <= 2 ? 1 : n->regs;
    return true;
}

/** slaves_strict() over the summary, in the same order with the same messages. */
static bool edge_strict(const esum_t *m, char *err, size_t errlen)
{
    for (int i = 0; i < m->ndev; i++) {
        const edev_t *s = &m->dev[i];
        if (s->dlt && !all_digits(s->addr, 12, 12)) {
            return fail(err, errlen, "%s: enter the 12-digit meter address.", s->name, NULL), false;
        }
        if (!s->dlt && !s->io && !(all_digits(s->addr, 1, 3) && atoi(s->addr) >= 1 && atoi(s->addr) <= 255)) {
            return fail(err, errlen, "%s: the slave ID must be 1 to 255.", s->name, NULL), false;
        }
        int unit = unit_of(s);
        if (unit >= 0 && (s->map_en || s->virt)) {
            for (int k = 0; k < m->ndev; k++) {
                if (k != i && unit_of(&m->dev[k]) == unit) {
                    return fail(err, errlen, "%s: its slave ID is already answered for %s.", s->name, m->dev[k].name), false;
                }
            }
        }
        for (int j = 0; j < m->nnode; j++) {
            const enode_t *n = &m->node[j];
            if (n->dev != i) continue;
            if (s->dlt) {
                bool hex = strlen(n->di) == 8;
                for (const char *p = n->di; hex && *p; p++) hex = (*p >= '0' && *p <= '9') || ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f');
                if (!hex) return fail(err, errlen, "%s: the data identifier must be 8 hex digits.", n->name, NULL), false;
                if (n->is_bool) return fail(err, errlen, "%s: a meter value is a number, not bool.", n->name, NULL), false;
            } else if ((n->fc <= 2) != n->is_bool) {
                return fail(err, errlen, n->fc <= 2 ? "%s: coils and discrete inputs are bool." : "%s: registers hold numbers, not bool.",
                            n->name, NULL), false;
            }
            int width = n->fc <= 2 && !s->dlt ? 1 : n->regs;
            if (!s->dlt && n->reg + width > 65536) return fail(err, errlen, "%s: its registers run past 65535.", n->name, NULL), false;
            int xfc, xw;
            long start;
            if (!span_of(s, n, &xfc, &start, &xw)) continue;
            if (start + xw > 65536) return fail(err, errlen, "%s: its mapped registers run past 65535.", n->name, NULL), false;
            if (!n->map_en) continue;
            for (int q = 0; q < m->nnode; q++) {
                const enode_t *o = &m->node[q];
                int ofc, ow;
                long ostart;
                if (o->dev != i || q == j || !span_of(s, o, &ofc, &ostart, &ow) || ofc != xfc) continue;
                if (start < ostart + ow && ostart < start + xw) {
                    return fail(err, errlen, "%s: its mapped registers overlap %s.", n->name, o->name), false;
                }
            }
        }
    }
    return true;
}

/** Prints `doc` into buf without reallocation; false when it does not fit (no memory). */
static bool print_into(const cJSON *doc, char *buf, int size) { return cJSON_PrintPreallocated((cJSON *)doc, buf, size, false); }

cfg_result_t cfg_edge_text(const cfg_env_t *env, const char *text, size_t len, bool strict, size_t max_out,
                           cfg_emit_fn emit, void *ctx, char *err, size_t errlen)
{
    js_span_t root;
    if (!js_parse_root(text, len, &root)) return fail(err, errlen, NOT_JSON, NULL, NULL), CFG_INVALID;
    if (js_type(root) != JS_ARRAY) return fail(err, errlen, "Expected a list for %s.", "edgeDevices", NULL), CFG_INVALID;
    if (js_count(root) > 64) return fail(err, errlen, "Too many entries in %s.", "edgeDevices", NULL), CFG_INVALID;

    enum { PRINT_MAX = 1024 };
    cfg_result_t r = CFG_NO_MEMORY;
    esum_t *m = cJSON_malloc(sizeof *m);
    char *buf = cJSON_malloc(PRINT_MAX);
    cJSON *dev_base = cJSON_Parse(ITEM_SLAVE), *node_base = cJSON_Parse(ITEM_NODE);
    cJSON *s = NULL, *o = NULL;
    eout_t out = {emit, ctx, 0, max_out, CFG_OK};
    if (!m || !buf || !dev_base || !node_base) goto done;
    m->ndev = m->nnode = 0;
    if (err && errlen) err[0] = '\0';

    // Pass 1, as list(): every device header against the schema, before anything else.
    js_iter_t it;
    js_span_t dev;
    js_iter_init(&it, root);
    while (js_iter_next(&it, NULL, &dev)) {
        s = edge_header(env, dev, dev_base, err, errlen);
        if (!s) goto bad;
        cJSON_Delete(s);
        s = NULL;
    }

    // Pass 2, as slaves(): each device's own rules, then its points, emitting the normalised text.
    int io = 0, virt = 0, eth = 0;
    out_put(&out, "[", 1);
    js_iter_init(&it, root);
    while (js_iter_next(&it, NULL, &dev)) {
        s = edge_header(env, dev, dev_base, err, errlen);
        if (!s) goto bad;
        const char *src = sget(s, "source");
        bool is_io = strcmp(src, "io") == 0, is_virt = strcmp(src, "virtual") == 0;
        if ((is_io && ++io > 1) || (is_virt && ++virt > 1)) {
            fail(err, errlen, is_io ? "Only one built-in IO device is allowed." : "Only one virtual device is allowed.", NULL, NULL);
            goto bad;
        }
        if (is_io || is_virt) cJSON_ReplaceItemInObject(s, "name", cJSON_CreateString(is_io ? IO_DEVICE : VIRTUAL_DEVICE));
        const char *sname = sget(s, "name");
        if (!sname) goto done;  // the replacement string could not be allocated
        if (!name_ok(sname)) {
            fail(err, errlen, "Device name \"%s\" is not valid.", sname, NULL);
            goto bad;
        }
        if (!is_io && !is_virt && (strcmp(sname, IO_DEVICE) == 0 || strcmp(sname, VIRTUAL_DEVICE) == 0)) {
            fail(err, errlen, "\"%s\" is reserved for the built-in IO and virtual devices.", sname, NULL);
            goto bad;
        }
        for (int k = 0; k < m->ndev; k++) {
            if (strcmp(m->dev[k].name, sname) == 0) {
                fail(err, errlen, "Device name \"%s\" is used twice.", sname, NULL);
                goto bad;
            }
        }
        if (strcmp(src, "eth") == 0 && ++eth > MAX_ETH_SLAVES) {
            fail(err, errlen, "At most 5 Modbus TCP devices.", NULL, NULL);
            goto bad;
        }
        edev_t *d = &m->dev[m->ndev];
        snprintf(d->name, sizeof d->name, "%s", sname);
        snprintf(d->addr, sizeof d->addr, "%s", sget(s, "addr"));
        d->dlt = strcmp(src, "uart") == 0 && strcmp(sget(s, "protocol"), "dlt645") == 0;
        d->own = (strcmp(src, "uart") == 0 && strcmp(sget(s, "protocol"), "modbus") == 0) || strcmp(src, "eth") == 0 || is_virt;
        d->virt = is_virt;
        d->io = is_io;
        d->map_en = bget(s, "mapEn");
        d->map_addr = iget(s, "mapAddr");
        if (!print_into(s, buf, PRINT_MAX)) goto done;
        cJSON_Delete(s);
        s = NULL;
        if (m->ndev) out_put(&out, ",", 1);
        out_put(&out, buf, strlen(buf) - 1);  // the header without its closing brace
        out_put(&out, ",\"nodes\":[", 10);

        js_iter_t nit;
        js_span_t node;
        int here = 0;
        js_iter_init(&nit, nodes_of(dev));
        while (js_iter_next(&nit, NULL, &node)) {
            if (m->nnode + 1 > MAX_NODES) {
                fail(err, errlen, "At most 100 points across all devices.", NULL, NULL);
                goto bad;
            }
            if (js_type(node) != JS_OBJECT) {
                fail(err, errlen, "Expected an object for %s.", "edgeDevices", NULL);
                goto bad;
            }
            cJSON *in = pick(node, node_fields, N(node_fields));
            if (!in) goto done;
            o = object(env, "edgeDevices", node_fields, N(node_fields), in, node_base, err, errlen);
            cJSON_Delete(in);
            if (!o) goto bad;
            if (is_io) {
                const char *ch = sget(o, "ioPoint");
                if (!io_channel_ok(env, ch)) {
                    fail(err, errlen, "IO channel \"%s\" does not exist on this gateway.", ch ? ch : "", NULL);
                    goto bad;
                }
                int idx = atoi(ch + 2) - 1, fc = ch[1] == 'O' ? 1 : ch[1] == 'I' && ch[0] == 'D' ? 2 : 4;
                cJSON_ReplaceItemInObject(o, "name", cJSON_CreateString(ch));
                cJSON_ReplaceItemInObject(o, "fc", cJSON_CreateNumber(fc));
                cJSON_ReplaceItemInObject(o, "reg", cJSON_CreateNumber(fc == 4 ? idx * 2 : idx));
                cJSON_ReplaceItemInObject(o, "dtype", cJSON_CreateString(fc == 4 ? "float32_abcd" : "bool"));
            } else if (is_virt) {
                cJSON_ReplaceItemInObject(o, "fc", cJSON_CreateNumber(4));
                if (strcmp(sget(o, "dtype"), "bool") == 0) {
                    fail(err, errlen, "Virtual point \"%s\" is an input register: choose a number type, not bool.", sget(o, "name"), NULL);
                    goto bad;
                }
            }
            const char *nname = sget(o, "name"), *dtype = sget(o, "dtype");
            if (!nname || !dtype) goto done;  // a replacement could not be allocated
            if (!name_ok(nname)) {
                fail(err, errlen, "Point name \"%s\" is not valid.", nname, NULL);
                goto bad;
            }
            for (int k = 0; k < m->nnode; k++) {
                if (strcmp(m->node[k].name, nname) == 0) {
                    fail(err, errlen, "Point name \"%s\" is used twice.", nname, NULL);
                    goto bad;
                }
            }
            enode_t *e = &m->node[m->nnode++];
            e->dev = m->ndev;
            snprintf(e->name, sizeof e->name, "%s", nname);
            snprintf(e->di, sizeof e->di, "%s", sget(o, "di"));
            e->fc = iget(o, "fc");
            e->reg = iget(o, "reg");
            e->regs = regs_of(dtype);
            e->is_bool = strcmp(dtype, "bool") == 0;
            e->map_en = bget(o, "mapEn");
            e->map_addr = iget(o, "mapAddr");
            if (!print_into(o, buf, PRINT_MAX)) goto done;
            cJSON_Delete(o);
            o = NULL;
            if (here++) out_put(&out, ",", 1);
            out_put(&out, buf, strlen(buf));
        }
        out_put(&out, "]}", 2);
        m->ndev++;
    }
    out_put(&out, "]", 1);

    // Pass 3, as slaves_strict(): the save-time rules across devices and points.
    if (strict && !edge_strict(m, err, errlen)) goto bad;
    r = out.r;
    if (r == CFG_TOO_LARGE) fail(err, errlen, "The Edge model is too large.", NULL, NULL);
    if (r == CFG_ABORTED) fail(err, errlen, "The Edge model could not be stored.", NULL, NULL);
    goto done;
bad:
    // A failed allocation inside object() leaves no message; a schema error always writes one.
    r = (err && errlen && err[0]) ? CFG_INVALID : CFG_NO_MEMORY;
    if (out.r == CFG_TOO_LARGE && r == CFG_INVALID) r = CFG_INVALID;  // the schema error is the more useful answer
done:
    if (r == CFG_NO_MEMORY) fail(err, errlen, "Out of memory.", NULL, NULL);
    cJSON_Delete(o);
    cJSON_Delete(s);
    cJSON_Delete(node_base);
    cJSON_Delete(dev_base);
    cJSON_free(buf);
    cJSON_free(m);
    return r;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make -C stm32/firmware/test/host`
Expected:
- `  largest Edge model: 37904 bytes in, 37904 bytes out, arena peak` followed by a number ≤ 61440 (about 15 700);
- `edge_text: all tests passed (8086 payloads compared)`;
- every earlier suite still passes.

- [ ] **Step 5: Align the docs, then commit**

In the spec §9.4, replace the whole line that starts with `  Hash FNV-1a chỉ dùng để tìm nhanh.` with this line (keep the two leading spaces):

```
  Với tối đa 64 thiết bị và 100 điểm, các tên được so sánh tuyến tính bằng `strcmp`, không cần hash.
```

In the roadmap row 03, replace the text `` `cfg_edge_begin/device/node/end` (3 phases) `` with `` `cfg_edge_text` (3 passes mirroring `list`/`slaves`/`slaves_strict`), `cfg_validate_text` ``.

```bash
git add stm32/firmware/components/cfgmodel stm32/firmware/test/host/test_edge_text.c stm32/firmware/test/host/Makefile stm32/docs/specs/2026-10-14-stm32-firmware-design.md stm32/docs/plans/2026-10-14-00-roadmap.md
git commit -m "stm32 cfgmodel: cfg_edge_text, edgeDevices node by node, byte-identical to cfg_validate"
```

- [ ] **Step 6: Prove the differential test bites (mutation check)**

Run each mutant, check the result, then restore:

```bash
C=stm32/firmware/components/cfgmodel/cfgmodel.c
sed -i 's/if (js_key_is(k, "nodes", true)) return v;/if (js_key_is(k, "nodes", false)) return v;/' $C
make -C stm32/firmware/test/host build/test_edge_text >/dev/null && (cd stm32/firmware/test/host && ./build/test_edge_text | tail -1)
git checkout $C
sed -i 's/if (k != i \&\& unit_of(\&m->dev\[k\]) == unit)/if (k > i \&\& unit_of(\&m->dev[k]) == unit)/' $C
make -C stm32/firmware/test/host build/test_edge_text >/dev/null && (cd stm32/firmware/test/host && ./build/test_edge_text | tail -1)
git checkout $C
```

Expected: each mutant prints a `… FAILED (… payloads compared)` line, and `git checkout` restores the file.

Nothing to commit after this step: the mutants are reverted.

---

## Plan 03 done when

- `make -C stm32/firmware/test/host` passes all six suites.
- The arena peaks printed by the tests are ≤ 61 440 bytes.
- `edge_text` compares 8 086 payloads without a difference, and both mutants in Task 4 Step 5 are caught.
- `python3 stm32/firmware/tools/check_schema.py` still prints `consistent`.
