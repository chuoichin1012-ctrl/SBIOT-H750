#include "blog.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "usart.h"  // huart7 (CubeMX)

static StaticSemaphore_t s_mutex_mem;
static SemaphoreHandle_t s_mutex;

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
}
