#include "blog.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "usart.h"  // huart7 (CubeMX)

static StaticSemaphore_t s_mutex_mem;
static SemaphoreHandle_t s_mutex;

// syslog (components/syslog) overrides it: every line also goes to its ring and, for warnings and
// errors, to the persistent log.
__attribute__((weak)) void blog_tap(const char *line, size_t len)
{
    (void)line;
    (void)len;
}

void blog_init(void)
{
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_mem);
}

void blog(const char *fmt, ...)
{
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof line)
        n = sizeof line - 1;
    bool locked = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (locked)
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    HAL_UART_Transmit(&huart7, (const uint8_t *)line, (uint16_t)n, 100);
    if (locked)
        xSemaphoreGive(s_mutex);
    blog_tap(line, (size_t)n);
}
