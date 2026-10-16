#!/usr/bin/env python3
"""Layout and ownership gates for boot.elf and app.elf (spec §5.1, §5.3, §6.2).

    python3 stm32/firmware/tools/check_elf.py BUILD_DIR

Prints one line per check and "elf: ok" when all pass; exits 1 otherwise. Needs arm-none-eabi-nm,
-readelf and -objdump on PATH (source ~/opt/xpack/env.sh).
"""
import re
import subprocess
import sys

# The app must not configure what the bootloader owns (spec §6.2):
# - no clock or QSPI setup;
# - no CubeMX MPU_Config (Reset_Handler programs the MPU once);
# - no CubeMX/CMSIS SystemInit or ExitRun0Mode, which left with the weak Reset_Handler that called them.
APP_FORBIDDEN = [
    'SystemInit', 'ExitRun0Mode', 'SystemClock_Config', 'MPU_Config', 'HAL_RCC_OscConfig',
    'HAL_RCC_ClockConfig', 'HAL_RCC_DeInit', 'HAL_PWREx_ConfigSupply', 'HAL_QSPI_Init',
    'HAL_QSPI_MemoryMapped', 'MX_QUADSPI_Init', 'HAL_MPU_ConfigRegion',
    # one heap (spec §5.5): newlib-nano's allocator and the real _sbrk_r never linked
    '__malloc_free_list', '__malloc_sbrk_start', '_sbrk_r',
    # CubeMX's inits that end in Error_Handler, a spin with interrupts off (Plan 06a): netmgr and
    # board_rng initialise the ETH and the RNG themselves, and report a failure instead
    'MX_ETH_Init', 'MX_RNG_Init',
]
# Hot code that must run from ITCM (spec §5.3), above the 1 KB null guard.
APP_ITCM = ['vTaskSwitchContext', 'xTaskIncrementTick', 'PendSV_Handler', 'SysTick_Handler', 'SVC_Handler',
            'memcpy', 'memset', 'ETH_IRQHandler', 'HAL_ETH_IRQHandler',
            # the serial ports (Plan 07a)
            'USART1_IRQHandler', 'USART2_IRQHandler', 'DMA1_Stream2_IRQHandler', 'DMA1_Stream3_IRQHandler',
            'DMA1_Stream4_IRQHandler', 'DMA1_Stream5_IRQHandler', 'TIM2_IRQHandler',
            # the ADC (Plan 07b)
            'BDMA_Channel0_IRQHandler', 'ADC3_IRQHandler',
            # mbedTLS's bignum core (Plan 08b)
            'mbedtls_mpi_core_mla']
ITCM = (0x00000400, 0x00010000)
AXI = (0x24000000, 0x24080000)
SRAM1 = (0x30000000, 0x30020000)
# What a DMA engine touches, plus lwIP's MEM heap: SRAM1, non-cacheable (spec §5.4, §5.6).
APP_SRAM1 = ['lwip_ram_heap', 'DMARxDscrTab', 'DMATxDscrTab', 's_eth_rx_bufs', 's_eth_tx_bufs', 's_uart_ring', 's_uart_txbuf']
# The bridges' serial -> network queues and bufferOffline: SRAM2+3 (spec §5.3, Plan 08a).
SRAM23 = (0x30020000, 0x30048000)
APP_SRAM23 = ['s_mb_store']
# newlib's close/fcntl on an lwIP socket would leak it (LWIP_POSIX_SOCKETS_IO_NAMES 0, Plan 08a).
APP_NO_LIBC_IO = ['close', 'fcntl']  # _close is syscalls.c's stub, always there
# The JSON arena, the Edge and certificate body and the web stack: static, in AXI (spec §5.3, Plan 06b).
APP_AXI = ['s_web_arena', 's_edge_body', 's_web_stack', 's_bridge_stack']  # bridge[n] (Plan 08a)
# The BDMA's ADC buffer: the 1 KB non-cacheable region in SRAM4 (MPU region 3, spec §6.6).
ADCBUF = (0x3800FC00, 0x38010000)
DTCM = (0x20000000, 0x20020000)
DTCM_BUDGET = 120 * 1024
QSPI_SLOT = (0x90000000, 0x90000000 + 1472 * 1024)
BOOT_MAX = 64 * 1024


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def symbols(elf):
    out = {}
    for line in run('arm-none-eabi-nm', elf).splitlines():
        parts = line.split()
        if len(parts) == 3:
            out[parts[2]] = int(parts[0], 16)
    return out


def sections(elf):
    """name -> (addr, size) for allocated sections."""
    out = {}
    for line in run('arm-none-eabi-readelf', '-SW', elf).splitlines():
        m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]{8})\s+[0-9a-f]+\s+([0-9a-f]+)\s+\S+\s+(\S*A\S*)', line)
        if m:
            out[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return out


def calls_in(elf, func):
    """Names called (bl/blx/b.w) from the body of func, or None when func is missing."""
    dis = run('arm-none-eabi-objdump', '-d', '--no-show-raw-insn', elf)
    body = re.search(r'<%s>:\n(.*?)\n\n' % re.escape(func), dis, re.S)
    return set(re.findall(r'\s(?:bl|blx|b\.w)\s+[0-9a-f]+ <([^>+]+)', body.group(1))) if body else None


def errata_window(elf):
    """Instructions between the first QUADSPI_CCR store (offset #20) and the store that disables the
    QUADSPI (offset 0, same base) in qf_errata_timing, or None when the pattern is missing."""
    dis = run('arm-none-eabi-objdump', '-d', '--no-show-raw-insn', elf)
    body = re.search(r'<qf_errata_timing>:\n(.*?)\n\n', dis, re.S)
    if not body:
        return None
    ins = [l.split('\t', 1)[1].strip() for l in body.group(1).splitlines() if ':\t' in l]
    first = next((i for i, x in enumerate(ins) if re.match(r'str\s+\w+, \[(\w+), #20\]', x)), None)
    if first is None:
        return None
    base = re.match(r'str\s+\w+, \[(\w+), #20\]', ins[first]).group(1)
    for j in range(first + 1, len(ins)):
        if re.match(r'str\s+\w+, \[%s(, #0)?\]' % base, ins[j]):  # objdump writes [r3, #0]
            return ins[first + 1:j]
    return None


def main():
    build = sys.argv[1]
    failures = []

    def check(ok, what):
        print(('ok   ' if ok else 'FAIL ') + what)
        if not ok:
            failures.append(what)

    bs = sections(build + '/boot.elf')
    end = max(a + s for a, s in bs.values() if 0x08000000 <= a < 0x08020000)
    check(bs['.isr_vector'][0] == 0x08000000, 'boot: vector table at 0x08000000')
    check(end - 0x08000000 <= BOOT_MAX, 'boot: %d bytes of internal flash <= %d' % (end - 0x08000000, BOOT_MAX))
    # ES0392 §2.7.4: CCR, CCR, CR within 127 kernel clocks, so nothing but stores and register moves between.
    w = errata_window(build + '/boot.elf')
    branch = re.compile(r'(b|bl|blx|bx|cbz|cbnz)(\.[nw])?$|b(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)(\.[nw])?$|it[te]*$')
    check(w is not None and len(w) <= 4 and not any(branch.match(x.split()[0]) for x in w),
          'boot: QUADSPI errata window has no branch (%s)' % ('; '.join(w) if w is not None else 'pattern missing'))

    app = build + '/app.elf'
    sym = symbols(app)
    sec = sections(app)
    for name in APP_FORBIDDEN:
        check(name not in sym, 'app: no %s' % name)
    for name in APP_ITCM:
        a = sym.get(name, 0)
        check(ITCM[0] <= a < ITCM[1], 'app: %s in ITCM (0x%08x)' % (name, a))
    for name in APP_SRAM23:
        a = sym.get(name)
        check(a is not None and SRAM23[0] <= a < SRAM23[1], 'app: %s in SRAM2+3 (0x%08x)' % (name, a or 0))
    for name in APP_NO_LIBC_IO:
        check(name not in sym, 'app: no %s (lwIP sockets use closesocket / lwip_fcntl)' % name)
    a = sym.get('s_adc_buf')
    check(a is not None and ADCBUF[0] <= a < ADCBUF[1], 'app: s_adc_buf in the ADC region (0x%08x)' % (a or 0))
    for name in APP_SRAM1:
        a = sym.get(name, 0)
        check(SRAM1[0] <= a < SRAM1[1], 'app: %s in SRAM1 (0x%08x)' % (name, a))
    for name in APP_AXI:
        a = sym.get(name, 0)
        check(AXI[0] <= a < AXI[1], 'app: %s in AXI (0x%08x)' % (name, a))
    # lwIP's memp pools are CPU-only: AXI, cacheable (spec §5.6, pool table).
    memp = sorted(n for n in sym if re.fullmatch(r'memp_memory_\w+_base', n))
    off = [n for n in memp if not AXI[0] <= sym[n] < AXI[1]]
    check(len(memp) >= 10 and not off, 'app: %d lwIP memp pools in AXI%s' % (len(memp), (' (not: %s)' % ', '.join(off)) if off else ''))
    check(sec.get('.image_header') == (0x90000000, 0x400), 'app: 1 KB header at 0x90000000')
    check(sec.get('.isr_vector', (0, 0))[0] == 0x90000400, 'app: vector table at 0x90000400')
    called = calls_in(app, 'Reset_Handler')
    # A call may go through a long-branch veneer (__memset_veneer), so match on the name.
    mem = [c for c in called or [] if re.search(r'mem(cpy|set|move)', c)]
    check(called is not None and not mem,
          'app: Reset_Handler calls no memcpy/memset (calls: %s)' % ', '.join(sorted(called or [])))
    check(not any(c.startswith('HAL_') for c in called or []), 'app: Reset_Handler calls no HAL function')
    dtcm = sum(s for a, s in sec.values() if DTCM[0] <= a < DTCM[1]) + 4096
    check(dtcm <= DTCM_BUDGET, 'app: DTCM %d bytes (data + bss + 4 KB MSP) <= %d' % (dtcm, DTCM_BUDGET))
    check(all(n in sym for n in ('malloc', '_malloc_r', 'sb_malloc')), 'app: malloc and _malloc_r are the heap_5 wrapper')
    image_end = sym.get('__image_end__', 0)
    check(QSPI_SLOT[0] < image_end <= QSPI_SLOT[1], 'app: image ends at 0x%08x, inside the slot' % image_end)

    print('elf: ok' if not failures else 'elf: %d check(s) failed' % len(failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
