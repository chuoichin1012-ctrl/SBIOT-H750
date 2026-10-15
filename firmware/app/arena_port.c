// The arena's owner (components/arena): a scope belongs to the FreeRTOS task that opened it, so
// another task's cJSON stays on the heap (Codex 06b r1).
#include "arena.h"
#include "FreeRTOS.h"
#include "task.h"

void *arena_port_self(void)
{
    return xTaskGetCurrentTaskHandle();
}
