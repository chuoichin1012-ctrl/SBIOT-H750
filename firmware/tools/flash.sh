#!/bin/bash
# Flashes over SWD with STM32CubeProgrammer (spec §12.4): boot.bin into internal flash, app.bin into
# QSPI bank 2 through the board's external loader, then resets the board. Run from WSL.
#   stm32/firmware/tools/flash.sh boot|app|both|blank [BUILD_DIR]
#   stm32/firmware/tools/flash.sh bind KEY|dev [BUILD_DIR]
# "blank" programs the first 4 KB of the QSPI slot to 0xFF (header and vector table erased).
# "bind" reads the chip's UID and writes its anti-clone record, signed with the private KEY
# (tools/provision.py), at QSPI 0x903F0000 (spec §11.5). Do it before RDP Level 1 is set.
set -euo pipefail
usage() { echo "usage: flash.sh boot|app|both|blank [BUILD_DIR] | bind KEY|dev [BUILD_DIR]" >&2; exit 2; }
what=${1:-}
tools=$(cd "$(dirname "$0")" && pwd)
case "$what" in
boot|app|both|blank) dir=${2:-} ;;
bind)
    [ -n "${2:-}" ] || usage
    key=$2
    [ "$key" = dev ] || key=$(realpath "$key")
    dir=${3:-} ;;
*) usage ;;
esac
build=$(cd "${dir:-$tools/../build/h750}" && pwd)
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
qspi() {  # $1 = local file, written at $2 (default 0x90000000)
    local at=${2:-0x90000000}
    cp "$1" "$stage/$(basename "$1")"
    prog -c port=SWD mode=UR -el "$(wslpath -w "$LOADER")" -w "$(wslpath -w "$stage/$(basename "$1")")" "$at" -v
    echo "flash: $(basename "$1") ($(stat -c %s "$1") bytes) at $at verified"
}
case "$what" in boot|both)
    cp "$build/boot.bin" "$stage/boot.bin"
    prog -c port=SWD mode=UR -w "$(wslpath -w "$stage/boot.bin")" 0x08000000 -v
    echo "flash: boot.bin ($(stat -c %s "$build/boot.bin") bytes) at 0x08000000 verified" ;;
esac
case "$what" in app|both) qspi "$build/app.bin" ;; esac
if [ "$what" = bind ]; then
    "$CLI" -c port=SWD mode=UR -r32 0x1FF1E800 12 > "$build/uid.txt" 2>&1 || true
    uid=$(python3 "$tools/provision.py" uid "$build/uid.txt") || { tail -5 "$build/uid.txt"; echo "flash: cannot read the UID"; exit 1; }
    python3 "$tools/provision.py" record "$key" "$uid" "$build/bind.bin"
    qspi "$build/bind.bin" 0x903F0000
    echo "flash: bound to UID $uid"
fi
if [ "$what" = blank ]; then
    head -c 4096 /dev/zero | tr '\0' '\377' > "$build/blank4k.bin"
    qspi "$build/blank4k.bin"
fi
"$CLI" -c port=SWD mode=HOTPLUG -hardRst > /dev/null 2>&1
echo "flash: board reset"
