// FreeRTOS V11.3.1 for the app (spec §6.1). Plan 02 has only static objects; Plan 05 adds heap_5.
#pragma once

#include <stdint.h>
extern uint32_t SystemCoreClock;

#define configCPU_CLOCK_HZ (SystemCoreClock)
#define configTICK_RATE_HZ 1000
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configMAX_PRIORITIES 32
#define configMINIMAL_STACK_SIZE 256  // words
#define configMAX_TASK_NAME_LEN 12
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_NEWLIB_REENTRANT 1  // newlib state per task (spec §5.5)
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configRECORD_STACK_HIGH_ADDRESS 1
#define configUSE_TRACE_FACILITY 1  // uxTaskGetSystemState: the lowest stack left, for /api/status.mem
// CPU time per task in microseconds from TIM2 (now_us), for the bench's idle-time measurement.
#define configGENERATE_RUN_TIME_STATS 1
#define configRUN_TIME_COUNTER_TYPE uint64_t
uint64_t now_us(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE() now_us()

#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1  // heap_5 (app/heap_newlib.c)
#define configKERNEL_PROVIDED_STATIC_MEMORY 1  // idle and timer task memory come from the kernel

// Timer task: 2 KB, priority 30; its callbacks only send notifications (spec §6.1).
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 30
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH 512

// Cortex-M7 with 4 priority bits. ISRs at priority 5..15 may call FromISR APIs; 0..4 never do.
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

// A failed assertion stops everything; wdog stops with it and IWDG1 resets the board.
#define configASSERT(x)               \
    do {                              \
        if (!(x)) {                   \
            taskDISABLE_INTERRUPTS(); \
            for (;;) {                \
            }                         \
        }                             \
    } while (0)

#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1

// The port's handlers take the CMSIS vector names (cube/app does not generate these three).
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#define INCLUDE_xTaskGetIdleTaskHandle 1  // ulTaskGetIdleRunTimeCounter (bench)
