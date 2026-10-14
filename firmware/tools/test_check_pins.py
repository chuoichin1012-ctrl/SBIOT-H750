#!/usr/bin/env python3
"""Proves that check_pins.py fails on each kind of mistake it exists for.

    python3 stm32/firmware/tools/test_check_pins.py
"""
import contextlib
import io
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_pins  # noqa: E402

FW = os.path.dirname(HERE)
BOOT = os.path.join(FW, 'cube/boot/boot.ioc')
APP = os.path.join(FW, 'cube/app/app.ioc')
DB = os.path.expanduser('~/opt/stm32cubemx/db/mcu')


def run(boot, app, db=DB):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = check_pins.main(['check_pins.py', '--db', db, boot, app])
    return rc, out.getvalue()


def mutate(src, dst, edits):
    text = open(src, encoding='latin-1').read()
    for old, new in edits:
        assert re.search(old, text, re.M), old
        text = re.sub(old, new, text, count=1, flags=re.M)
    open(dst, 'w', encoding='latin-1').write(text)


def main():
    failures = 0
    tmp = tempfile.mkdtemp()
    try:
        rc, out = run(BOOT, APP)
        if rc != 0:
            print('FAIL unmodified files rejected:\n' + out)
            failures += 1
        cases = [
            ('UART7 RX/TX swapped', APP,
             [(r'^PB3\\ \(JTDO/TRACESWO\)\.Signal=UART7_RX', r'PB3\\ (JTDO/TRACESWO).Signal=UART7_TX'),
              (r'^PB4\\ \(NJTRST\)\.Signal=UART7_TX', r'PB4\\ (NJTRST).Signal=UART7_RX')], 'PB3 carries UART7_TX'),
            ('PLL1 N differs from the bootloader', APP, [(r'^RCC\.DIVN1=192', 'RCC.DIVN1=180')], 'RCC.DIVN1 differs'),
            ('latch OE enabled at reset', BOOT, [(r'^PD15\.PinState=GPIO_PIN_SET', 'PD15.PinState=GPIO_PIN_RESET')],
             'PD15 initial level'),
            ('FSIZE without the ES0396 margin', BOOT, [(r'^QUADSPI\.FlashSize=22', 'QUADSPI.FlashSize=21')],
             'QUADSPI.FlashSize'),
            ('QSPI pin at low speed', APP,
             [(r'^PE9\.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH', 'PE9.GPIO_Speed=GPIO_SPEED_FREQ_LOW')],
             'PE9 (QUADSPI_BK2_IO2) speed GPIO_SPEED_FREQ_LOW'),
            ('ETH pin at low speed', APP,
             [(r'^PA1\.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH', 'PA1.GPIO_Speed=GPIO_SPEED_FREQ_LOW')],
             'PA1 (ETH_REF_CLK) speed GPIO_SPEED_FREQ_LOW'),
            ('UART pin without a speed', BOOT, [(r'^PB4\\ \(NJTRST\)\.GPIO_Speed=GPIO_SPEED_FREQ_HIGH\n', '')],
             'PB4 (UART7_TX) speed None'),
            ('a pin spec §3 does not assign', APP, [(r'^(PA8\.Signal=GPIO_Input)', r'\1\nPB14.Signal=GPIO_Output')],
             'PB14 carries GPIO_Output'),
        ]
        for what, src, edits, expect in cases:
            boot, app = BOOT, APP
            dst = os.path.join(tmp, os.path.basename(src))
            mutate(src, dst, edits)
            if src == BOOT:
                boot = dst
            else:
                app = dst
            rc, out = run(boot, app)
            ok = rc == 1 and expect in out
            print(('ok   ' if ok else 'FAIL ') + what)
            failures += not ok
            os.remove(dst)
        # A signal the silicon does not have on that pin: drop QUADSPI_CLK from PB2 in a copy of the database.
        fake = os.path.join(tmp, 'db')
        os.mkdir(fake)
        name = 'STM32H750VBTx.xml'
        xml = open(os.path.join(DB, name), encoding='utf-8').read()
        i = xml.index('<Pin Name="PB2"')
        j = xml.index('</Pin>', i)
        open(os.path.join(fake, name), 'w', encoding='utf-8').write(
            xml[:i] + xml[i:j].replace('<Signal Name="QUADSPI_CLK"/>', '') + xml[j:])
        rc, out = run(BOOT, APP, fake)
        ok = rc == 1 and 'QUADSPI_CLK is not a signal of PB2' in out
        print(('ok   ' if ok else 'FAIL ') + 'signal missing from the CubeMX database')
        failures += not ok
    finally:
        shutil.rmtree(tmp)
    print('test_check_pins: %s' % ('ok' if not failures else 'FAILED'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
