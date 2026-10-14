// Fault injection for the text entry points: every single allocation is made to fail in
// turn, and each run must end in CFG_NO_MEMORY ("Out of memory.") with nothing leaked and no
// partial result (spec §9.3). Links cfgmodel without the arena, with its own failing
// allocator and its own json_alloc_failures(): make -C stm32/firmware/test/host
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static long s_count, s_fail_at;  // allocations so far; the 1-based one to fail (0: none)
static unsigned s_failed;

unsigned json_alloc_failures(void) { return s_failed; }

static void *flaky_malloc(size_t n)
{
    if (++s_count == s_fail_at) {
        s_failed++;
        return NULL;
    }
    return malloc(n);
}

typedef cfg_result_t (*entry_fn)(const char *text, char *err, size_t errlen);

static char s_out[CFG_SMALL_BUF];
static const char *s_name;
static cfg_result_t small(const char *text, char *err, size_t errlen)
{
    return cfg_validate_text(&ENV, s_name, text, strlen(text), NULL, s_out, sizeof s_out, err, errlen);
}
/** Runs `fn` once clean to count its allocations, then once per allocation with that one failing. */
static void each_allocation(entry_fn fn, const char *text, const char *label)
{
    char err[200];
    s_count = 0, s_fail_at = 0;
    cfg_result_t clean = fn(text, err, sizeof err);
    long total = s_count;
    CHECK(clean == CFG_OK || clean == CFG_INVALID, "%s: clean run gives %d (%s)", label, clean, err);
    for (long k = 1; k <= total; k++) {
        s_count = 0, s_fail_at = k;
        cfg_result_t r = fn(text, err, sizeof err);
        CHECK(r == CFG_NO_MEMORY && strcmp(err, "Out of memory.") == 0, "%s: allocation %ld of %ld failing gives %d (%s)", label, k,
              total, r, err);
    }
    printf("  %-28s %ld allocations, each failed once\n", label, total);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    cJSON_Hooks h = {.malloc_fn = flaky_malloc, .free_fn = free};
    cJSON_InitHooks(&h);

    s_name = "port1";
    each_allocation(small, "{\"baudRate\":9600,\"remoteHost\":\"data.example.net\",\"tls\":1}", "port1");
    s_name = "mqttPublish";
    each_allocation(small, "[{\"topic\":\"a/b\",\"identifier\":\"x\"},{\"topic\":\"c\",\"qos\":2}]", "mqttPublish");
    s_name = "port1";
    each_allocation(small, "{\"baudRate\":300}", "port1 schema error");

    printf(failures ? "%d FAILED\n" : "oom: all tests passed\n", failures);
    return failures != 0;
}
