"""Bench access from WSL to the board's UART7 (ST-Link VCP, COM4 on Windows) and to its reset.

The COM port belongs to Windows, so a PowerShell process reads it; every received line comes back
with the milliseconds since the port was opened. Set SBIOT_COM to use another port.
"""
import base64
import os
import subprocess
import threading
import time

COM = os.environ.get('SBIOT_COM', 'COM4')
CLI = os.environ.get('CUBEPROG_CLI',
                     '/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe')

# Two flat arrays for the timed sends: PowerShell unrolls a one-element array of pairs.
_PS = r'''
$p = New-Object System.IO.Ports.SerialPort '%(port)s', 115200
$p.Open(); $w = [Diagnostics.Stopwatch]::StartNew(); $buf = ''
$at = @(%(at)s); $tx = @(%(tx)s); $i = 0
while ($w.ElapsedMilliseconds -lt %(ms)d) {
  if ($i -lt $at.Count -and $w.ElapsedMilliseconds -ge $at[$i]) { $p.Write($tx[$i]); $i++ }
  $buf += $p.ReadExisting()
  while (($n = $buf.IndexOf("`n")) -ge 0) {
    $line = $buf.Substring(0, $n).TrimEnd("`r"); $buf = $buf.Substring($n + 1)
    [Console]::Out.WriteLine([string]$w.ElapsedMilliseconds + '|' + $line)
  }
  Start-Sleep -Milliseconds 5
}
$p.Close()
'''


def listen(seconds, sends=(), during=None):
    """Reads UART7 for `seconds`. sends[i] = (at_s, text) is written at at_s; during() runs once the
    port is open. Returns [(ms, line)]."""
    at = ','.join(str(int(t * 1000)) for t, _ in sends)
    tx = ','.join("'%s'" % text for _, text in sends)
    script = _PS % {'port': COM, 'ms': int(seconds * 1000), 'at': at, 'tx': tx}
    enc = base64.b64encode(script.encode('utf-16-le')).decode()
    proc = subprocess.Popen(['powershell.exe', '-NoProfile', '-EncodedCommand', enc], cwd='/mnt/c',
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    if during:
        time.sleep(1.5)  # PowerShell needs about a second to open the port
        threading.Thread(target=during).start()
    lines = []
    for raw in proc.stdout:
        ms, sep, line = raw.rstrip('\n').partition('|')
        if sep and ms.isdigit():
            lines.append((int(ms), line))
    proc.wait()
    return lines


def hard_reset():
    """Pulses NRST through the ST-Link."""
    subprocess.run([CLI, '-c', 'port=SWD', 'mode=HOTPLUG', '-hardRst'], cwd='/mnt/c', capture_output=True, check=True)


OPENOCD = os.environ.get('SBIOT_OPENOCD', '/mnt/c/Users/linh_bui/opt/xpack-openocd-0.12.0-7')
HERE = os.path.dirname(os.path.abspath(__file__))
OCD_DIR = os.path.normpath(os.path.join(HERE, '../../tools/openocd'))


def symbol(elf, name):
    """Address of `name` in `elf` (arm-none-eabi-nm on PATH)."""
    out = subprocess.run(['arm-none-eabi-nm', elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise RuntimeError('%s not in %s' % (name, elf))


def openocd(commands, boot_elf=None):
    """Runs OpenOCD (Windows build) with tools/openocd/sbiot-h750.cfg and qspi_hostile.tcl, then the
    given Tcl commands, and returns its output (OpenOCD's echo writes to stderr, so both streams).
    With boot_elf, the Tcl variables ERRATA_FN (qf_errata_timing) and PARK (Default_Handler, used as
    a return address with a hardware breakpoint) are set first. Raises when OpenOCD fails, so a Tcl
    `error` in a hostile proc fails the test. The Windows paths are computed before changing to C:."""
    win = lambda path: subprocess.run(['wslpath', '-w', path], capture_output=True, text=True, check=True).stdout.strip()
    pre = ''
    if boot_elf:
        pre = 'set ERRATA_FN 0x%08x; set PARK 0x%08x; ' % (symbol(boot_elf, 'qf_errata_timing'),
                                                           symbol(boot_elf, 'Default_Handler'))
    args = [OPENOCD + '/bin/openocd.exe', '-s', win(OPENOCD + '/openocd/scripts'),
            '-f', win(OCD_DIR + '/sbiot-h750.cfg'), '-f', win(OCD_DIR + '/qspi_hostile.tcl'),
            '-c', 'init; ' + pre + commands + '; shutdown']
    r = subprocess.run(args, cwd='/mnt/c', capture_output=True, text=True)
    out = r.stdout + r.stderr
    if r.returncode != 0:
        raise RuntimeError('openocd exit %d:\n%s' % (r.returncode, out[-2000:]))
    return out


if __name__ == '__main__':  # python3 board.py SECONDS: print what UART7 says after a reset
    import sys
    for ms, line in listen(float(sys.argv[1]) if len(sys.argv) > 1 else 5, during=hard_reset):
        print('%6d %s' % (ms, line))
