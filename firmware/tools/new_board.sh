#!/bin/bash
# Programs a new board in one go (spec §12.4, the production order): build, the anti-clone record first, then the
# bootloader and the app with the console read through the first boot. RDP Level 1 is set only with --rdp, after a
# typed confirmation: it is the last step, and undoing it erases the chip. Run from WSL, ST-Link on SWD (P6).
# Usage:
#   stm32/firmware/tools/new_board.sh [--key dev|KEY_FILE] [--sn SB2610000123] [--no-build] [--build-dir DIR] [--rdp]
#   --key       the private key for the record: dev (a development board, the default) or a production key file;
#               a production key needs a build with SBIOT_BIND_PUBKEY_FILE (its public half)
#   --sn        this board's serial number; default: the next one of ~/.sbiot/sn_counter (SBIOT_SN_COUNTER)
#   --no-build  use the images already in the build directory
#   --rdp       set RDP Level 1 at the end (production boards only)
# A new board only: its first boot must seed the factory firmware from the app flashed here. A board that ran other
# firmware (a factory or staging image in SPI4) is stopped at the check: rework it as spec §12.4 says.
# Not for a board with R20 fitted: the QSPI loader does not kick the external watchdog (spec §12.4).
set -euo pipefail
tools=$(cd "$(dirname "$0")" && pwd)
fw=$(cd "$tools/.." && pwd)
key=dev sn= build=1 rdp=0 dir=$fw/build/h750
usage() { sed -n '6,11p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }
fail() { echo "new_board: $*" >&2; exit 1; }
step() { echo; echo "=== $*"; }
while [ $# -gt 0 ]; do
    case "$1" in
    --key) key=${2:?}; shift 2 ;;
    --sn) sn=${2:?}; shift 2 ;;
    --no-build) build=0; shift ;;
    --build-dir) dir=$(realpath "${2:?}"); shift 2 ;;
    --rdp) rdp=1; shift ;;
    -h|--help) usage ;;
    *) echo "new_board: unknown option $1" >&2; usage ;;
    esac
done
CLI=${CUBEPROG_CLI:-"/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"}

# The record's key and the firmware's public key must be a pair: otherwise the app refuses to start.
pub=$(grep -s '^SBIOT_BIND_PUBKEY_FILE' "$dir/CMakeCache.txt" | cut -d= -f2- || true)
if [ "$key" = dev ]; then
    [ -z "$pub" ] || fail "$dir is a production build ($pub): use --key with its private key"
else
    [ -f "$key" ] || fail "no key file $key"
    [ -n "$pub" ] || fail "a production key needs a build with SBIOT_BIND_PUBKEY_FILE"
fi

if [ "$build" = 1 ]; then
    step "build: web UI and firmware"
    (cd "$fw/../web" && npm ci --silent && npm run -s build:firmware)
    cmake --build "$dir" -j8
fi
for f in boot.bin app.img; do [ -f "$dir/$f" ] || fail "no $dir/$f (build first)"; done

step "probe: the ST-Link on SWD"
probe=$(cd /mnt/c && "$CLI" -l st-link 2>&1 | grep -m1 'ST-LINK SN' | sed 's/.*: *//' | tr -d '\r' || true)
[ -n "$probe" ] || fail "no ST-Link found (replug it, or attach it with usbipd)"
echo "ST-Link $probe"

step "bind: the anti-clone record, MAC and SN (before anything else)"
out=$(SBIOT_SN=${sn:-${SBIOT_SN:-}} "$tools/flash.sh" bind "$key" "$dir") || { echo "$out"; fail "bind FAILED"; }
echo "$out"
bound=$(grep 'flash: bound to' <<<"$out") || fail "bind printed no record"

step "flash: bootloader and app, the console read through the first boot (${SBIOT_COM:-COM4})"
# The first boot after flash.sh's reset is the one to check: it seeds the factory firmware from this app. The console
# is opened first, so that boot is read from its first line.
FLASH="$tools/flash.sh" BUILD="$dir" python3 "$tools/new_board_check.py" ||
    fail "the first boot is not as expected (see above)"

if [ "$rdp" = 1 ]; then
    step "RDP Level 1 (read protection, the last step)"
    read -r -p "Type RDP to set Level 1 on this board (undoing it erases the chip): " answer
    [ "$answer" = RDP ] || fail "RDP not set (the board is programmed and checked)"
    (cd /mnt/c && "$CLI" -c port=SWD -ob RDP=0xBB) > "$dir/rdp.txt" 2>&1 || true
    ob=$(cd /mnt/c && "$CLI" -c port=SWD -ob displ 2>&1 | grep -i 'RDP' || true)
    echo "$ob"
    grep -qi '0xBB' <<<"$ob" || { tail -5 "$dir/rdp.txt"; fail "RDP Level 1 is not set (option bytes above)"; }
    echo "RDP Level 1 set"
fi

step "done"
echo "${bound#flash: }"
echo "Next: find the board with FinderTool (it answers even without an address), set its network, open the web UI."
echo "      Without a working SIM, set WAN & LAN priority to Ethernet only (0). ADC: tools/factory_cal.py."
