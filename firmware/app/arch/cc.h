// lwIP's compiler and platform settings for the app (arm-none-eabi-gcc, newlib-nano).
#pragma once
#include <stdint.h>
#include <stdlib.h>

#define LWIP_NO_STDINT_H 0
#define LWIP_ERRNO_INCLUDE <errno.h>
#define LWIP_PROVIDE_ERRNO 0
#define BYTE_ORDER LITTLE_ENDIAN

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
