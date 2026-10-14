#!/bin/bash
# Flashes over SWD with STM32CubeProgrammer (spec §12.4): boot.bin into internal flash, app.bin into
# QSPI bank 2 through the board's external loader, then resets the board. Run from WSL.
#   stm32/firmware/tools/flash.sh boot|app|both|blank [BUILD_DIR]
# "blank" programs the first 4 KB of the QSPI slot to 0xFF (header and vector table erased).
set -euo pipefail
what=${1:-}
case "$what" in boot|app|both|blank) ;; *) echo "usage: flash.sh boot|app|both|blank [BUILD_DIR]" >&2; exit 2 ;; esac
build=$(cd "${2:-$(dirname "$0")/../build/h750}" && pwd)
CLI=${CUBEPROG_CLI:-"/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"}
LOADER="$(dirname "$CLI")/ExternalLoader/STM32H750VBT6_GD25Q32ESIG.stldr"
# The team's loader for this board (Dev_info: 0x90000000, 4 MB, 256 B pages, 1024 x 4 KB sectors).
# Proved on the spare block 0x903E0000 before first use (Plan 02, Task 4). Plan 04 replaces it.
LOADER_SHA256=5ef2de362969bb2ad9d0c92604e58f27b018614533fe421e6ac5974c9bcb0699
echo "$LOADER_SHA256  $LOADER" | sha256sum -c --quiet - || { echo "flash: the loader is not the pinned build"; exit 1; }
# CubeProgrammer is a Windows program: it reads the images from the Windows profile and runs from C:.
stage=$(wslpath -u "$(cd /mnt/c && powershell.exe -NoProfile -Command '$env:USERPROFILE' | tr -d '\r')")/sbiot
mkdir -p "$stage"
cd /mnt/c
prog() {
    local out
    out=$("$CLI" "$@" 2>&1) || true
    if ! grep -q 'Download verified successfully' <<<"$out"; then
        grep -iE 'error|fail' <<<"$out" || tail -5 <<<"$out"
        echo "flash: FAILED"
        exit 1
    fi
}
qspi() {  # $1 = local file, written at 0x90000000
    cp "$1" "$stage/$(basename "$1")"
    prog -c port=SWD mode=UR -el "$(wslpath -w "$LOADER")" -w "$(wslpath -w "$stage/$(basename "$1")")" 0x90000000 -v
    echo "flash: $(basename "$1") ($(stat -c %s "$1") bytes) at 0x90000000 verified"
}
case "$what" in boot|both)
    cp "$build/boot.bin" "$stage/boot.bin"
    prog -c port=SWD mode=UR -w "$(wslpath -w "$stage/boot.bin")" 0x08000000 -v
    echo "flash: boot.bin ($(stat -c %s "$build/boot.bin") bytes) at 0x08000000 verified" ;;
esac
case "$what" in app|both) qspi "$build/app.bin" ;; esac
if [ "$what" = blank ]; then
    head -c 4096 /dev/zero | tr '\0' '\377' > "$build/blank4k.bin"
    qspi "$build/blank4k.bin"
fi
"$CLI" -c port=SWD mode=HOTPLUG -hardRst > /dev/null 2>&1
echo "flash: board reset"
