"""Bench access from WSL to the board's UART7 (ST-Link VCP, COM4 on Windows) and to its reset.

The COM port belongs to Windows, so a PowerShell process reads it; every received line comes back
with the milliseconds since the port was opened. Set SBIOT_COM to use another port.
"""
import base64
import os
import subprocess
import threading

COM = os.environ.get('SBIOT_COM', 'COM4')
CLI = os.environ.get('CUBEPROG_CLI',
                     '/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe')

# Two flat arrays for the timed sends: PowerShell unrolls a one-element array of pairs.
_PS = r'''
$p = New-Object System.IO.Ports.SerialPort '%(port)s', 115200
$p.Open(); Start-Sleep -Milliseconds 50; $p.DiscardInBuffer()   # drop what the VCP buffered while the port was closed
$w = [Diagnostics.Stopwatch]::StartNew(); $buf = ''
[Console]::Out.WriteLine('0|__ready__'); [Console]::Out.Flush()
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


def listen(seconds, sends=(), during=None, during_timeout=120):
    """Reads UART7 for `seconds`. sends[i] = (at_s, text) is written at at_s; during() runs once the
    port is open. Returns [(ms, line)] only after during() has finished: it raises RuntimeError when
    during() raised, or when it is still running during_timeout seconds after the window closed (so
    the next case never starts while this one still drives the board)."""
    at = ','.join(str(int(t * 1000)) for t, _ in sends)
    tx = ','.join("'%s'" % text for _, text in sends)
    script = _PS % {'port': COM, 'ms': int(seconds * 1000), 'at': at, 'tx': tx}
    enc = base64.b64encode(script.encode('utf-16-le')).decode()
    proc = subprocess.Popen(['powershell.exe', '-NoProfile', '-EncodedCommand', enc], cwd='/mnt/c',
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    lines, errors, worker = [], [], None

    def run():
        try:
            during()
        except Exception as e:  # noqa: BLE001 - handed to the caller below
            errors.append(e)

    for raw in proc.stdout:
        ms, sep, line = raw.rstrip('\n').partition('|')
        if line == '__ready__':  # the port is open and drained: only now may during() act on the board
            if during:
                worker = threading.Thread(target=run, daemon=True)
                worker.start()
            continue
        if sep and ms.isdigit():
            lines.append((int(ms), line))
    proc.wait()
    if worker:
        worker.join(during_timeout)
        if worker.is_alive():
            raise RuntimeError('during() still running %d s after the listen window' % during_timeout)
        if errors:
            raise RuntimeError('during() failed: %s' % errors[0])
    return lines


# The bench OTA upload (app console 'u', app/bench_ota.h): the image goes in 4 KB blocks, each one
# only after the app acknowledged the one before, then the port stays open to see the reboot, until the new
# app's web line (or a failure). With 500 KB images that is about a minute: 50 s of transfer at 115200 baud
# (4 KB blocks, each written to the SPI flash and acknowledged), then the bootloader's install.
_PS_UPLOAD = r"""
$img = [IO.File]::ReadAllBytes('%(file)s')
$p = New-Object System.IO.Ports.SerialPort '%(port)s', 115200
$p.Open(); Start-Sleep -Milliseconds 50; $p.DiscardInBuffer()
$w = [Diagnostics.Stopwatch]::StartNew(); $buf = ''; $sent = 0; $state = 'wait-ready'; $end = %(ms)d
$p.Write([byte[]][char[]]'%(cmd)s', 0, 1); $p.Write([BitConverter]::GetBytes([uint32]$img.Length), 0, 4)
while ($w.ElapsedMilliseconds -lt $end) {
  $buf += $p.ReadExisting()
  while (($n = $buf.IndexOf("`n")) -ge 0) {
    $line = $buf.Substring(0, $n).TrimEnd("`r"); $buf = $buf.Substring($n + 1)
    [Console]::Out.WriteLine([string]$w.ElapsedMilliseconds + '|' + $line)
    $go = ($state -eq 'wait-ready' -and $line -match '^ota: ready \d+ 0$') -or ($state -eq 'sending' -and $line -match '^ota: \d+$')
    if ($line -match '^ota: (done|timeout|no length)' -or ($line -match '^ota: ready' -and -not $go)) { $state = 'after' }
    if (%(stop)s -and $line -match '^ota: done') { $end = $w.ElapsedMilliseconds }
    # Done once the new app is up (or the upload failed): `seconds` is only the ceiling.
    if ($state -eq 'after' -and $line -match '^web: UI and API') { $end = [Math]::Min($end, $w.ElapsedMilliseconds + 500) }
    if ($line -match '^ota: (timeout|no length)' -or $line -match '^ota: done -?[1-9]') { $end = [Math]::Min($end, $w.ElapsedMilliseconds + 1000) }
    if ($go -and $sent -lt $img.Length -and $sent -lt %(limit)d) {
      $k = [Math]::Min(4096, $img.Length - $sent); $p.Write($img, $sent, $k); $sent += $k; $state = 'sending'
    }
  }
  Start-Sleep -Milliseconds 2
}
$p.Close()
"""


def upload(image, seconds, stop_at_done=False, reset=True, send=None):
    """Streams the file `image` into staging through the app console and reads UART7 for `seconds`
    in all (upload, reset, the bootloader's install, the new app), or only until "ota: done" with
    stop_at_done (to reset the board in the middle of the install). reset=False uses console 'U'
    (the app does not reset). send=N announces the whole file but sends only its first N bytes
    (rounded up to a block): an upload cut off. Returns [(ms, line)]."""
    stage = subprocess.run(['powershell.exe', '-NoProfile', '-Command', '$env:USERPROFILE'], cwd='/mnt/c',
                           capture_output=True, text=True).stdout.strip()
    win_dir = stage + r'\sbiot'
    wsl_dir = subprocess.run(['wslpath', '-u', win_dir], capture_output=True, text=True).stdout.strip()
    os.makedirs(wsl_dir, exist_ok=True)
    name = 'upload_' + os.path.basename(image)
    with open(image, 'rb') as src, open(os.path.join(wsl_dir, name), 'wb') as dst:
        dst.write(src.read())
    script = _PS_UPLOAD % {'port': COM, 'ms': int(seconds * 1000), 'file': win_dir + '\\' + name,
                           'stop': '$true' if stop_at_done else '$false', 'cmd': 'u' if reset else 'U',
                           'limit': send if send is not None else 1 << 30}
    enc = base64.b64encode(script.encode('utf-16-le')).decode()
    out = subprocess.run(['powershell.exe', '-NoProfile', '-EncodedCommand', enc], cwd='/mnt/c',
                         capture_output=True, text=True).stdout
    lines = []
    for raw in out.splitlines():
        ms, sep, line = raw.partition('|')
        if sep and ms.isdigit():
            lines.append((int(ms), line))
    return lines


def qspi_read32(addr, words):
    """QSPI words at `addr` over SWD with the team's loader (CubeProgrammer, under reset): proves
    what the flash really holds, independently of the firmware."""
    loader = os.path.join(os.path.dirname(CLI), 'ExternalLoader', 'STM32H750VBT6_GD25Q32ESIG.stldr')
    win = subprocess.run(['wslpath', '-w', loader], capture_output=True, text=True, check=True).stdout.strip()
    out = subprocess.run([CLI, '-c', 'port=SWD', 'mode=UR', '-el', win, '-r32', '0x%08X' % addr, str(words * 4)],
                         cwd='/mnt/c', capture_output=True, text=True).stdout
    vals = []
    for line in out.splitlines():
        head, sep, rest = line.partition(' : ')
        if sep and head.strip().lower().startswith('0x9'):
            vals += [int(w, 16) for w in rest.split()]
    return vals[:words]


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
