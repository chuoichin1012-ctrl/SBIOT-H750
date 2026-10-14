#include "jumpcheck.h"

// An initial stack pointer is the end of the stack: above the region base, at most its end.
static bool in_ram(uint32_t sp, uint32_t base, uint32_t size)
{
    return sp > base && sp - base <= size;
}

bool boot_app_ok(uint32_t msp, uint32_t reset)
{
    bool msp_ok = (msp & 7u) == 0 && (in_ram(msp, 0x20000000u, 128u * 1024) || in_ram(msp, 0x24000000u, 512u * 1024));
    uint32_t pc = reset & ~1u;
    bool pc_ok = (reset & 1u) && pc >= APP_VECTORS && pc < APP_BASE + APP_SLOT_SIZE;
    return msp_ok && pc_ok;
}
