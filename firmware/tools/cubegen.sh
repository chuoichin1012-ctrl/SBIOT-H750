#!/bin/bash
# Regenerates cube/boot and cube/app from their .ioc with STM32CubeMX in headless mode (spec §4:
# CubeMX only generates code; our edits live in USER CODE blocks, which CubeMX keeps).
#   stm32/firmware/tools/cubegen.sh
# CUBEMX is the install dir (default ~/opt/stm32cubemx), CUBEMX_JAVA a Java 17+ runtime.
set -euo pipefail
FW=$(cd "$(dirname "$0")/.." && pwd)
CUBEMX=${CUBEMX:-$HOME/opt/stm32cubemx}
JAVA=${CUBEMX_JAVA:-$HOME/.local/opt/android-studio/jbr/bin/java}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

generate() {  # $1 = boot|app
    printf 'config load %s\nproject name %s\nproject toolchain CMake\nproject path %s\nproject generate\nexit\n' \
        "$FW/cube/$1/$1.ioc" "$1" "$FW/cube" > "$work/$1.txt"
    (cd "$CUBEMX" && "$JAVA" -jar STM32CubeMX.exe -q "$work/$1.txt") > "$work/$1.log" 2>&1 || true
    if [ "$(grep -c '^OK' "$work/$1.log")" -ne 5 ] || grep -q '^KO' "$work/$1.log"; then
        grep -B3 '^KO' "$work/$1.log" || tail -20 "$work/$1.log"
        echo "cubegen: $1 FAILED"
        exit 1
    fi
}

for k in boot app; do
    d="$FW/cube/$k"
    # A fresh .ioc may get its toolchain files under Projects/ on the first generation; generation is
    # idempotent once they sit next to Src/ and ToolChainLocation is empty (Plan 02, B12).
    for try in 1 2 3 4; do
        generate "$k"
        if [ -f "$d/startup_stm32h750xx.s" ] && [ -f "$d/Src/syscalls.c" ] && grep -q '^ProjectManager.ToolChainLocation=$' "$d/$k.ioc"; then
            break
        fi
        [ "$try" -lt 4 ] || { echo "cubegen: $k toolchain files not placed next to Src/"; exit 1; }
        rm -rf "$d/Projects"
        sed -i 's/^ProjectManager.ToolChainLocation=.*/ProjectManager.ToolChainLocation=/' "$d/$k.ioc"
    done
    # Both images build against the one HAL in third_party/stm32cube_h7; CubeMX's own CMake project is unused.
    rm -rf "$d/Drivers" "$d/CMakeLists.txt" "$d/CMakePresets.json" "$d/cmake"
    echo "cubegen: $k ok"
done
