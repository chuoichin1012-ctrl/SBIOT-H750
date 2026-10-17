/* XIP app (spec §5.1, §5.3, §6.2): 1 KB header and vector table at 0x90000000/0x90000400 in QSPI,
   hot code copied to ITCM, data and task stacks in DTCM, buffers in the other RAMs by section. */
ENTRY(Reset_Handler)

MEMORY
{
  QSPI    (rx)  : ORIGIN = 0x90000000, LENGTH = 1472K  /* the slot: header + body */
  ITCM    (rwx) : ORIGIN = 0x00000400, LENGTH = 63K    /* 0x0-0x3FF is the MPU null guard */
  DTCM    (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  AXI     (rw)  : ORIGIN = 0x24000000, LENGTH = 512K
  SRAM1   (rw)  : ORIGIN = 0x30000000, LENGTH = 128K   /* non-cacheable (MPU region 2) */
  SRAM23  (rw)  : ORIGIN = 0x30020000, LENGTH = 160K
  SRAM4   (rw)  : ORIGIN = 0x38000000, LENGTH = 63K
  ADCBUF  (rw)  : ORIGIN = 0x3800FC00, LENGTH = 1K     /* non-cacheable (MPU region 3) */
  BKPSRAM (rw)  : ORIGIN = 0x38800000, LENGTH = 4K
}

_estack = ORIGIN(DTCM) + LENGTH(DTCM);  /* MSP: 4 KB at the top of DTCM (spec §5.3) */
_Min_Stack_Size = 4K;
__image_start__ = ORIGIN(QSPI);

SECTIONS
{
  .image_header :
  {
    KEEP(*(.image_header))
    . = 0x400;
  } >QSPI

  .isr_vector :
  {
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >QSPI

  /* Before .text, which would otherwise take these input sections first. */
  .itcm_text :
  {
    . = ALIGN(4);
    *tasks.c.obj(.text .text.*)
    *queue.c.obj(.text .text.*)
    *list.c.obj(.text .text.*)
    *timers.c.obj(.text .text.*)
    *port.c.obj(.text .text.*)
    *_nano.a:libc_a-memcpy*.o(.text .text.*)  /* libg_nano.a with --specs=nano.specs */
    *_nano.a:libc_a-memset.o(.text .text.*)
    *stm32h7xx_it.c.obj(.text.ETH_IRQHandler)        /* Plan 06a (spec §5.3) */
    *stm32h7xx_hal_eth.c.obj(.text.HAL_ETH_IRQHandler)
    *bignum_core.c.obj(.text .text.*)  /* mbedTLS's bignum core (spec §5.3, Plan 08b) */
    *(.itcm_text .itcm_text.*)  /* ISRs tagged by later plans */
    . = ALIGN(4);
  } >ITCM AT> QSPI

  .text :
  {
    . = ALIGN(4);
    *(.text .text.*)
    *(.glue_7) *(.glue_7t) *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
  } >QSPI

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } >QSPI

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >QSPI
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >QSPI

  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >QSPI
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >QSPI
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >QSPI

  /* Reset_Handler walks these tables (startup_app.c). */
  .copy.table :
  {
    . = ALIGN(4);
    __app_copy_start__ = .;
    LONG(LOADADDR(.itcm_text)) LONG(ADDR(.itcm_text)) LONG(SIZEOF(.itcm_text))
    LONG(LOADADDR(.data))      LONG(ADDR(.data))      LONG(SIZEOF(.data))
    __app_copy_end__ = .;
  } >QSPI
  .zero.table :
  {
    . = ALIGN(4);
    __app_zero_start__ = .;
    LONG(ADDR(.bss))        LONG(SIZEOF(.bss))
    LONG(ADDR(.axi_bss))    LONG(SIZEOF(.axi_bss))
    LONG(ADDR(.sram1_bss))  LONG(SIZEOF(.sram1_bss))
    LONG(ADDR(.sram23_bss)) LONG(SIZEOF(.sram23_bss))
    LONG(ADDR(.sram4_bss))  LONG(SIZEOF(.sram4_bss))
    LONG(ADDR(.adc_buf))    LONG(SIZEOF(.adc_buf))
    __app_zero_end__ = .;
  } >QSPI

  /* CubeMX's weak Reset_Handler names these; it is replaced, but keep them defined. */
  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data .data.*)
    . = ALIGN(4);
    _edata = .;
  } >DTCM AT> QSPI
  __image_end__ = LOADADDR(.data) + SIZEOF(.data);

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >DTCM

  .axi_bss (NOLOAD)    : { . = ALIGN(32); *(.axi_bss .axi_bss.*)       . = ALIGN(32); } >AXI
  /* SRAM1 also takes CubeMX's ETH descriptors (eth.c), which name sections of their own. */
  .sram1_bss (NOLOAD)  : { . = ALIGN(32); *(.sram1_bss .sram1_bss.*) *(.RxDescripSection) *(.TxDescripSection) . = ALIGN(32); } >SRAM1
  .sram23_bss (NOLOAD) : { . = ALIGN(32); *(.sram23_bss .sram23_bss.*) . = ALIGN(32); } >SRAM23
  .sram4_bss (NOLOAD)  : { . = ALIGN(32); *(.sram4_bss .sram4_bss.*)   . = ALIGN(32); } >SRAM4
  /* Kept across a reset (Plan 11a task 0b): in no zero table, and the bootloader uses DTCM only. */
  .noinit (NOLOAD)     : { . = ALIGN(32); *(.noinit .noinit.*)         . = ALIGN(32); } >SRAM4
  .adc_buf (NOLOAD)    : { . = ALIGN(32); *(.adc_buf .adc_buf.*)       . = ALIGN(32); } >ADCBUF
  .bkp_noinit (NOLOAD) : { *(.bkp_noinit .bkp_noinit.*) } >BKPSRAM   /* survives resets; never zeroed */
}

/* DTCM budget (spec §5.3): .data, .bss (which holds the static task stacks) and the MSP <= 120 KB. */
ASSERT(SIZEOF(.data) + SIZEOF(.bss) + _Min_Stack_Size <= 120K, "DTCM budget of 120 KB exceeded")
ASSERT(_ebss <= _estack - _Min_Stack_Size, "no room for the 4 KB MSP stack")
ASSERT(ADDR(.isr_vector) == 0x90000400, "vector table must start at 0x90000400")
