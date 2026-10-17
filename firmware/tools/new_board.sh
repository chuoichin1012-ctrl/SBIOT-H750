#!/bin/bash
# Programs a new board in one go (spec §12.4, the production order): build, the anti-clone record first, then the
# bootloader and the app, then a check of the first boot on the console. Run from WSL with the ST-Link on SWD (P6)
# and the board powered. RDP Level 1 is set only with --rdp, after a typed confirmation: it is the last step and
# cannot be undone without erasing the chip.
#   stm32/firmware/tools/new_board.sh [--key dev|KEY_FILE] [--sn SB2610000123] [--erase] [--no-build]
#                                     [--build-dir DIR] [--rdp]
#   --key       the private key for the record: dev (a development board, the default) or a production key file;
#               a production key needs a build with SBIOT_BIND_PUBKEY_FILE (its public half)
#   --sn        this board's serial number; default: the next one of ~/.sbiot/sn_counter (SBIOT_SN_COUNTER)
#   --erase     mass-erase internal flash and QSPI first (a board that ran another firmware)
#   --no-build  use the images already in the build directory
#   --rdp       set RDP Level 1 at the end (production boards only)
# Not for a board with R20 fitted: the QSPI loader does not kick the external watchdog (spec §12.4).
set -euo pipefail
tools=$(cd "$(dirname "$0")" && pwd)
fw=$(cd "$tools/.." && pwd)
key=dev sn= erase=0 build=1 rdp=0 dir=$fw/build/h750
usage() { sed -n '6,14p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
    --key) key=${2:?}; shift 2 ;;
    --sn) sn=${2:?}; shift 2 ;;
    --erase) erase=1; shift ;;
    --no-build) build=0; shift ;;
    --build-dir) dir=$(realpath "${2:?}"); shift 2 ;;
    --rdp) rdp=1; shift ;;
    -h|--help) usage ;;
    *) echo "new_board: unknown option $1" >&2; usage ;;
    esac
done
step() { echo; echo "=== $*"; }
CLI=${CUBEPROG_CLI:-"/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"}

# The record's key and the firmware's public key must be a pair: a dev record on a production build (or the other
# way round) makes the app refuse to start.
pub=$(grep -s '^SBIOT_BIND_PUBKEY_FILE' "$dir/CMakeCache.txt" | cut -d= -f2- || true)
if [ "$key" = dev ] && [ -n "$pub" ]; then
    echo "new_board: $dir is a production build ($pub); use --key with its private key" >&2; exit 1
fi
if [ "$key" != dev ]; then
    [ -f "$key" ] || { echo "new_board: no key file $key" >&2; exit 1; }
    [ -n "$pub" ] || { echo "new_board: a production key needs a build with SBIOT_BIND_PUBKEY_FILE" >&2; exit 1; }
fi

if [ "$build" = 1 ]; then
    step "build: web UI and firmware"
    (cd "$fw/../web" && npm ci --silent && npm run -s build:firmware)
    cmake --build "$dir" -j8
fi
for f in boot.bin app.img; do [ -f "$dir/$f" ] || { echo "new_board: no $dir/$f (build first)" >&2; exit 1; }; done

step "probe: the ST-Link on SWD"
probe=$(cd /mnt/c && "$CLI" -l st-link 2>&1 | grep -m1 'ST-LINK SN' | sed 's/.*: *//' | tr -d '\r' || true)
[ -n "$probe" ] || { echo "new_board: no ST-Link found (replug it, or attach it with usbipd)" >&2; exit 1; }
echo "ST-Link $probe"

if [ "$erase" = 1 ]; then
    step "erase: internal flash and QSPI"
    "$tools/flash.sh" erase "$dir"
fi

step "bind: the anti-clone record, MAC and SN (before anything else)"
bound=$(SBIOT_SN=${sn:-${SBIOT_SN:-}} "$tools/flash.sh" bind "$key" "$dir" | tee /dev/stderr | grep 'flash: bound to' || true)
[ -n "$bound" ] || { echo "new_board: bind FAILED" >&2; exit 1; }

step "flash: bootloader and app (the first boot keeps this app as the factory firmware)"
"$tools/flash.sh" both "$dir"

step "check: the first boots on the console (${SBIOT_COM:-COM4})"
# After flash.sh's own reset: one more, read from the start. The record checked, the app started, the store mounted.
check=$(cd "$fw/test/bench" && python3 - <<'EOF'
import board
t = [l for _, l in board.listen(12, during=board.hard_reset)]
for l in t:
    if l.startswith(('boot:', 'bind:', 'SBIOT-H750', 'app: SN', 'app: store')):
        print('  ' + l)
ok = any('bind: ok' in l for l in t) and any(l.startswith('SBIOT-H750 app ') for l in t)
print('CHECK ' + ('ok' if ok else 'FAILED'))
EOF
)
echo "$check"
grep -q '^CHECK ok' <<<"$check" || { echo "new_board: the board did not start as expected (see the console lines above)" >&2; exit 1; }

if [ "$rdp" = 1 ]; then
    step "RDP Level 1 (read protection, the last step)"
    read -r -p "Type RDP to set Level 1 on this board (undoing it erases the chip): " answer
    if [ "$answer" = RDP ]; then
        (cd /mnt/c && "$CLI" -c port=SWD -ob RDP=0xBB) | grep -iE 'rdp|success|error|fail' || true
    else
        echo "RDP not set"
    fi
fi

step "done"
echo "${bound#flash: }"
echo "Next: find the board with FinderTool (it answers even without an address), set its network, open the web UI."
echo "      If it has no working SIM, set WAN & LAN priority to Ethernet only (0). ADC: tools/factory_cal.py."
