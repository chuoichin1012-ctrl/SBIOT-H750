# Puts the running board's GD25Q32E into states a warm reset can meet, then resets the MCU with
# SYSRESETREQ (Plan 02, Review Focus 1). Each proc halts the core, leaves memory-mapped mode, sends
# raw QUADSPI commands and ends with "reset run". Any failed check raises a Tcl error, which makes
# OpenOCD exit non-zero (test/bench/board.py's openocd() then raises).
# Needs ERRATA_FN (qf_errata_timing in boot.elf) and PARK (Default_Handler in boot.elf), set by openocd().
set QSPI 0x52005000

proc qspi_wait {} {
    global QSPI
    for {set i 0} {$i < 1000} {incr i} {
        if {!([read_memory [expr {$QSPI + 0x08}] 32 1] & 0x20)} { return }
    }
    error "QUADSPI stays BUSY"
}

# Runs qf_errata_timing() on the MCU, in the bootloader's internal flash: the ES0392 §2.7.4
# sequence needs its 127-kernel-clock window, which register writes over SWD cannot keep. The
# function returns to PARK, where a hardware breakpoint halts the core again.
proc run_errata_on_mcu {} {
    global ERRATA_FN PARK
    reg primask 1
    reg xpsr 0x01000000
    reg lr [expr {$PARK | 1}]
    reg pc [expr {$ERRATA_FN & ~1}]
    bp [expr {$PARK & ~1}] 2 hw
    resume
    wait_halt 1000
    rbp [expr {$PARK & ~1}]
    if {([lindex [reg pc] 2] & ~1) != ($PARK & ~1)} { error "qf_errata_timing did not return" }
}

# Halts, aborts memory-mapped mode, applies the workaround, then restores DCR and CR (bank 2, prescaler).
proc qspi_leave_mapped {} {
    global QSPI
    halt
    set cr [read_memory $QSPI 32 1]
    set dcr [read_memory [expr {$QSPI + 0x04}] 32 1]
    mmw $QSPI 0x2 0
    qspi_wait
    run_errata_on_mcu
    mww [expr {$QSPI + 0x04}] $dcr
    mww $QSPI $cr
}

# One indirect command; fails on TEF and when the transfer does not complete (TCF).
proc qspi_cmd {ccr {ar ""} {dlr ""}} {
    global QSPI
    mww [expr {$QSPI + 0x0C}] 0x1B                       ;# FCR: clear the flags
    if {$dlr ne ""} { mww [expr {$QSPI + 0x10}] $dlr }   ;# DLR: data length - 1
    mww [expr {$QSPI + 0x14}] $ccr                       ;# CCR: starts at once without an address phase
    if {$ar ne ""} { mww [expr {$QSPI + 0x18}] $ar }     ;# AR: starts the command
}
proc qspi_done {} {
    global QSPI
    qspi_wait
    set sr [read_memory [expr {$QSPI + 0x08}] 32 1]
    if {$sr & 0x1} { error [format "QUADSPI transfer error (SR %08x)" $sr] }
    if {!($sr & 0x2)} { error [format "QUADSPI transfer not complete (SR %08x)" $sr] }
}

# EBh with M7-0 = A0h (M5-4 = 10b): afterwards the flash takes the next clocks as an address.
# CCR: indirect read (01), data 4 lines, 4 dummy, alternate 8 bit on 4 lines, address 24 bit on
# 4 lines, instruction on 1 line, EBh.
proc qspi_hostile_continuous {} {
    global QSPI
    qspi_leave_mapped
    mww [expr {$QSPI + 0x1C}] 0xA0                       ;# ABR
    qspi_cmd 0x0710EDEB 0x0 3
    set d [read_memory [expr {$QSPI + 0x20}] 32 1]       ;# one 32-bit read pops the 4 bytes at 0x0
    qspi_done
    # The image header's magic "SBIO" (spec §11.1; Plan 02's placeholder header read ffffffff). A
    # flash left in continuous-read mode by an earlier failed run reads garbage here, not the magic.
    if {$d != 0x4f494253} { error [format "EBh read %08x, expected the image magic SBIO" $d] }
    echo [format "hostile: continuous-read mode entered (read %08x)" $d]
    reset run
}

# WREN, then sector erase 20h of the spare block 0x3E0000 (tSE 45-300 ms): the reset lands while
# the flash is busy. SR1 must show WIP before the reset.
proc qspi_hostile_erase {} {
    global QSPI
    qspi_leave_mapped
    qspi_cmd 0x00000106; qspi_done                       ;# 06h
    qspi_cmd 0x00002520 0x003E0000; qspi_done            ;# 20h, address on 1 line
    qspi_cmd 0x05000105 "" 0                             ;# 05h: read SR1, 1 byte
    set sr1 [read_memory [expr {$QSPI + 0x20}] 8 1]
    qspi_done
    if {!($sr1 & 0x1)} { error [format "sector erase not running (SR1 %02x)" $sr1] }
    echo [format "hostile: sector erase running (SR1 %02x)" $sr1]
    reset run
}
