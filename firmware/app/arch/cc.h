// lwIP's compiler and platform settings for the app (arm-none-eabi-gcc, newlib-nano).
#pragma once
#include <stdint.h>
#include <stdlib.h>
// newlib's struct timeval (LWIP_TIMEVAL_PRIVATE 0) and fd_set, in every lwIP file: the same types
// Mongoose sees (06b). app/fdset_check.c checks FD_SETSIZE against the sockets.
#include <sys/select.h>
#include <sys/time.h>

#define LWIP_NO_STDINT_H 0
#define LWIP_ERRNO_INCLUDE <errno.h>
// No LWIP_PROVIDE_ERRNO at all: lwIP tests it with #ifdef, so even 0 gave it its own errno variable and
// codes, and the callers (the bridges, Mongoose) read newlib's per-task errno, always 0 (Plan 08a).
// BYTE_ORDER comes from newlib (machine/endian.h): little endian

void netmgr_assert(const char *msg, const char *file, int line);
#define LWIP_PLATFORM_ASSERT(x) netmgr_assert((x), __FILE__, __LINE__)
#define LWIP_PLATFORM_DIAG(x) \
    do {                      \
    } while (0)

// Placement (spec §5.6): every memp pool and PBUF_POOL in AXI, since only the CPU touches them; the
// MEM heap in SRAM1 (netmgr.c defines lwip_ram_heap there). Nothing of lwIP's reaches a DMA engine:
// ETH TX copies into eth_drv's own buffers, and ETH RX uses eth_drv's own pool (spec §5.4).
#define LWIP_DECLARE_MEMORY_ALIGNED(name, size) \
    uint8_t name[LWIP_MEM_ALIGN_BUFFER(size)] __attribute__((section(".axi_bss"), aligned(32)))
extern uint8_t lwip_ram_heap[];
#define LWIP_RAM_HEAP_POINTER lwip_ram_heap
