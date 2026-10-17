"""new_board.sh's check: runs `flash.sh both` with the UART7 console open and reads the first boot after it. A new
board seeds its factory firmware from the app just flashed; the record is checked, the app starts, the store mounts,
and nothing halts or fails. FLASH and BUILD come from the environment.
Exit 0 when all of it holds."""
import os
import subprocess
import sys


def judge(lines):
    """{what: ok} for the first boot's console lines."""
    install = [l for l in lines if l.startswith('boot: install')]
    first = install[0] if install else ''
    return {
        'the factory firmware seeded from this app (a new board)': 'seeded-factory' in first
            and not any(x in first for x in ('installed-staging', 'restored-factory', 'SEED-FAILED')),
        'the record checked (bind: ok)': any('bind: ok' in l for l in lines),
        'the app started': any(l.startswith('SBIOT-H750 app ') for l in lines),
        'the store mounted': any(l.startswith('app: store mounted') for l in lines),
        'no halt, no INVALID, no FAILED': not any('halted' in l or 'INVALID' in l or 'FAILED' in l for l in lines),
    }


def load_board():
    """test/bench/board.py (the console over the ST-Link's COM port), from wherever this script is run."""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'test', 'bench'))
    import board
    return board


def main():
    board = load_board()
    flash = lambda: subprocess.run([os.environ['FLASH'], 'both', os.environ['BUILD']], check=True)
    lines = [l for _, l in board.listen(90, during=flash)]
    for l in lines:
        if l.startswith(('boot:', 'bind:', 'SBIOT-H750', 'app: SN', 'app: store', 'app: anti-clone')):
            print('  ' + l)
    need = judge(lines)
    for what, ok in need.items():
        print(('  ok    ' if ok else '  FAIL  ') + what)
    if not need['the factory firmware seeded from this app (a new board)'] and any(l.startswith('boot: install') for l in lines):
        print('  This board is not new (SPI4 holds a factory or staging image): rework it as spec §12.4 says.')
    return 0 if all(need.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
