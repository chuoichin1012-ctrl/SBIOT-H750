#include "syslog.h"
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "store.h"

#define RING_SIZE (32 * 1024)
#define LINE_MAX 200
#define QUEUE_LEN 32
#define FLASH_BUDGET_PER_MIN 30  // persisted warning/error lines per minute (flash wear)
#define STACK_WORDS 768          // 3 KB (spec §6.1)

typedef struct {
    uint16_t len;
    char text[LINE_MAX];
} line_t;

static char s_ring[RING_SIZE] __attribute__((section(".sram4_bss")));
static size_t s_head, s_used;  // circular bytes of '\n'-terminated lines
static StaticSemaphore_t s_ring_lock_mem;
static SemaphoreHandle_t s_ring_lock;  // one writer (the syslog task), readers in HTTP handlers
static StaticQueue_t s_queue_mem;
static uint8_t s_queue_buf[QUEUE_LEN * sizeof(line_t)] __attribute__((section(".axi_bss")));
static QueueHandle_t s_queue;
static StaticTask_t s_tcb;
static StackType_t s_stack[STACK_WORDS];
static syslog_line_fn s_tap;
static void *s_tap_ctx;

void blog_tap(const char *s, size_t n)
{
    if (!s_queue)
        return;
    line_t line;
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        n--;
    if (!n)
        return;
    line.len = (uint16_t)(n < LINE_MAX - 1 ? n : LINE_MAX - 1);
    memcpy(line.text, s, line.len);
    xQueueSend(s_queue, &line, 0);  // never block a logging task; a full queue drops the line
}

static void ring_put(const char *s, size_t n)
{
    xSemaphoreTake(s_ring_lock, portMAX_DELAY);
    for (size_t i = 0; i < n; i++) {
        s_ring[s_head] = s[i];
        s_head = (s_head + 1) % RING_SIZE;
        if (s_used < RING_SIZE)
            s_used++;
    }
    xSemaphoreGive(s_ring_lock);
}

void syslog_ram_iter(syslog_line_fn fn, void *ctx)
{
    char *copy = malloc(RING_SIZE);
    if (!copy)
        return;
    xSemaphoreTake(s_ring_lock, portMAX_DELAY);
    size_t used = s_used, start = (s_head + RING_SIZE - s_used) % RING_SIZE;
    for (size_t i = 0; i < used; i++)
        copy[i] = s_ring[(start + i) % RING_SIZE];
    xSemaphoreGive(s_ring_lock);
    size_t i = 0;
    if (used == RING_SIZE) {  // the oldest line was cut; skip to the next whole one
        while (i < used && copy[i] != '\n')
            i++;
        i++;
    }
    while (i < used) {
        size_t j = i;
        while (j < used && copy[j] != '\n')
            j++;
        fn(copy + i, j - i, ctx);
        i = j + 1;
    }
    free(copy);
}

void syslog_set_tap(syslog_line_fn fn, void *ctx)
{
    s_tap_ctx = ctx;
    s_tap = fn;
}

static void syslog_task(void *arg)
{
    (void)arg;
    line_t line;
    uint64_t window = 0;
    int budget = FLASH_BUDGET_PER_MIN;
    for (;;) {
        if (xQueueReceive(s_queue, &line, portMAX_DELAY) != pdTRUE)
            continue;
        line.text[line.len] = '\n';
        ring_put(line.text, line.len + 1u);
        if (s_tap)
            s_tap(line.text, line.len, s_tap_ctx);
        if (line.text[0] == 'W' || line.text[0] == 'E') {  // "W (1234) tag: ...": kept across restarts
            uint64_t now = now_us();
            if (now - window > 60ull * 1000 * 1000) {
                window = now;
                budget = FLASH_BUDGET_PER_MIN;
            }
            if (budget > 0) {
                budget--;
                store_log_append(line.text, line.len);  // dropped until the store is mounted
            }
        }
    }
}

void syslog_init(void)
{
    s_ring_lock = xSemaphoreCreateMutexStatic(&s_ring_lock_mem);
    s_queue = xQueueCreateStatic(QUEUE_LEN, sizeof(line_t), s_queue_buf, &s_queue_mem);
    xTaskCreateStatic(syslog_task, "syslog", STACK_WORDS, NULL, 2, s_stack, &s_tcb);
}
