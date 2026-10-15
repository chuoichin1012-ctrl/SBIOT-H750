#!/usr/bin/env python3
"""Pin and clock gate for cube/boot/boot.ioc and cube/app/app.ioc (spec §3, §6.2).

    python3 stm32/firmware/tools/check_pins.py [--db DIR] [BOOT_IOC APP_IOC]

Checks both files:
- every pin of the spec §3 table carries its signal, GPIO label and initial level;
- no other pin is assigned;
- every signal exists on its pin in the CubeMX MCU database;
- what the bootloader freezes (PLL1, bus prescalers, supply, QSPI, UART7) is identical in the two files.
Prints "pins: ok" or the failures and exits 1.
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)

# spec §3: pin -> signal. The pins both images configure, then the app-only peripherals.
COMMON = {
    'PH0': 'RCC_OSC_IN', 'PH1': 'RCC_OSC_OUT', 'PA13': 'DEBUG_JTMS-SWDIO', 'PA14': 'DEBUG_JTCK-SWCLK',
    'PB2': 'QUADSPI_CLK', 'PC11': 'QUADSPI_BK2_NCS', 'PE7': 'QUADSPI_BK2_IO0', 'PE8': 'QUADSPI_BK2_IO1',
    'PE9': 'QUADSPI_BK2_IO2', 'PE10': 'QUADSPI_BK2_IO3',
    'PB3': 'UART7_RX', 'PB4': 'UART7_TX',
    'PC10': 'GPIO_Output', 'PE11': 'GPIO_Output', 'PD10': 'GPIO_Output', 'PD11': 'GPIO_Output',
    'PD14': 'GPIO_Output', 'PD15': 'GPIO_Output', 'PA5': 'GPIO_Output', 'PA4': 'GPIO_Output',
    'PD13': 'GPIO_Output', 'PE4': 'GPIO_Output', 'PA8': 'GPIO_Input', 'PB7': 'GPIO_Input', 'PB6': 'GPIO_Input',
    'PE2': 'SPI4_SCK', 'PE5': 'SPI4_MISO', 'PE6': 'SPI4_MOSI',  # the bootloader's OTA flash since Plan 04b
}
APP_ONLY = {
    'PA9': 'USART1_TX', 'PA10': 'USART1_RX', 'PA12': 'USART1_DE',
    'PD5': 'USART2_TX', 'PD6': 'USART2_RX', 'PD4': 'USART2_DE', 'PD8': 'USART3_TX', 'PD9': 'USART3_RX',
    'PA1': 'ETH_REF_CLK', 'PA2': 'ETH_MDIO', 'PA7': 'ETH_CRS_DV', 'PC1': 'ETH_MDC', 'PC4': 'ETH_RXD0',
    'PC5': 'ETH_RXD1', 'PB11': 'ETH_TX_EN', 'PB12': 'ETH_TXD0', 'PB13': 'ETH_TXD1',
    'PC2_C': 'ADC3_INP0', 'PC3_C': 'ADC3_INP1',
}
# GPIO label and level after MX_GPIO_Init: the safe state of spec §11.2 step 2.
GPIO = {
    'PC10': ('WDI_LED', 'GPIO_PIN_RESET'), 'PE11': ('LED_NET', 'GPIO_PIN_RESET'),
    'PD10': ('DO1_D', 'GPIO_PIN_RESET'), 'PD11': ('DO2_D', 'GPIO_PIN_RESET'),
    'PD14': ('LATCH_LE', 'GPIO_PIN_RESET'), 'PD15': ('LATCH_OE', 'GPIO_PIN_SET'),
    'PA5': ('LTE_PWR', 'GPIO_PIN_RESET'), 'PA4': ('LTE_RESET', 'GPIO_PIN_RESET'),
    'PD13': ('ETH_RESET', 'GPIO_PIN_RESET'), 'PE4': ('SPI4_NSS', 'GPIO_PIN_SET'),
    'PA8': ('RELOAD', None), 'PB7': ('DI1', None), 'PB6': ('DI2', None),
}
# Output speed by peripheral (team rule): QSPI, ETH and SPI very high, UART high; SPI4's software NSS too.
SPEED = [('QUADSPI_', 'GPIO_SPEED_FREQ_VERY_HIGH'), ('ETH_', 'GPIO_SPEED_FREQ_VERY_HIGH'),
         ('SPI4_', 'GPIO_SPEED_FREQ_VERY_HIGH'), ('UART', 'GPIO_SPEED_FREQ_HIGH'), ('USART', 'GPIO_SPEED_FREQ_HIGH')]


def speed_for(pin, sig):
    key = GPIO[pin][0] if sig.startswith('GPIO_') and pin in GPIO else sig
    return next((s for prefix, s in SPEED if key.startswith(prefix)), None)
# What the bootloader sets up once and the app relies on (spec §6.2): identical in both files.
FROZEN = re.compile(r'^(RCC\.(HSE_VALUE|DIV[MNPQR]1|SYSCLKSource|PLLSourceVirtual|D1CPRE|HPRE|D[123]PPRE[12]?|'
                    r'PWR_Regulator_Voltage_Scale|SupplySource|QSPICLockSelection|SYSCLKFreq_VALUE|HCLKFreq_Value|'
                    r'APB[1234]Freq_Value|QSPIFreq_Value|FLatency)|QUADSPI\..*|UART7\..*)$')
EXPECT_KEYS = {
    'RCC.SYSCLKFreq_VALUE': '480000000', 'RCC.HCLKFreq_Value': '240000000', 'RCC.APB1Freq_Value': '120000000',
    'RCC.QSPIFreq_Value': '160000000', 'RCC.DIVQ1': '6', 'QUADSPI.ClockPrescaler': '1', 'QUADSPI.FlashSize': '22',
    'RCC.PWR_Regulator_Voltage_Scale': 'PWR_REGULATOR_VOLTAGE_SCALE0', 'RCC.SupplySource': 'PWR_LDO_SUPPLY',
}


def read_ioc(path):
    keys = {}
    for line in open(path, encoding='latin-1'):
        line = line.rstrip('\n')
        if line.startswith('#') or '=' not in line:
            continue
        k, v = line.split('=', 1)
        keys[k.replace('\\ ', ' ')] = v
    return keys


def short(pin):
    """'PH0-OSC_IN (PH0)' -> 'PH0', 'PA13 (JTMS/SWDIO)' -> 'PA13'."""
    return re.split(r'[- ]', pin, 1)[0]


def db_signals(db, mcu):
    out = {}
    for p in ET.parse(os.path.join(db, mcu + '.xml')).getroot().iter():
        if p.tag.endswith('Pin') and p.get('Name'):
            out[short(p.get('Name'))] = {s.get('Name') for s in p if s.tag.endswith('Signal')}
    return out


def check_file(name, keys, expected, db, fail):
    assigned = {short(k[:-len('.Signal')]): v for k, v in keys.items() if k.endswith('.Signal') and k.startswith('P')}
    for pin, sig in expected.items():
        if assigned.get(pin) != sig:
            fail('%s: %s carries %s, expected %s' % (name, pin, assigned.get(pin), sig))
    for pin, sig in assigned.items():
        if pin not in expected:
            fail('%s: %s carries %s, which spec §3 does not assign' % (name, pin, sig))
        elif not sig.startswith('GPIO_') and sig not in db.get(pin, set()):
            fail('%s: %s is not a signal of %s in the CubeMX database' % (name, sig, pin))
    full = {short(k.split('.')[0]): k.split('.')[0] for k in keys if k.startswith('P')}
    for pin, (label, level) in GPIO.items():
        k = full.get(pin, pin)
        if keys.get(k + '.GPIO_Label') != label:
            fail('%s: %s label %s, expected %s' % (name, pin, keys.get(k + '.GPIO_Label'), label))
        if level and keys.get(k + '.PinState', 'GPIO_PIN_RESET') != level:
            fail('%s: %s initial level %s, expected %s' % (name, pin, keys.get(k + '.PinState'), level))
    for pin, sig in expected.items():
        want = speed_for(pin, sig)
        got = keys.get(full.get(pin, pin) + '.GPIO_Speed')
        if want and got != want:
            fail('%s: %s (%s) speed %s, expected %s' % (name, pin, sig, got, want))
    for k, v in EXPECT_KEYS.items():
        if keys.get(k) != v:
            fail('%s: %s = %s, expected %s' % (name, k, keys.get(k), v))


def main(argv):
    db = os.path.expanduser('~/opt/stm32cubemx/db/mcu')
    if len(argv) > 1 and argv[1] == '--db':
        db, argv = argv[2], argv[:1] + argv[3:]
    boot_path = argv[1] if len(argv) > 2 else os.path.join(FW, 'cube/boot/boot.ioc')
    app_path = argv[2] if len(argv) > 2 else os.path.join(FW, 'cube/app/app.ioc')
    failures = []
    fail = failures.append
    boot, app = read_ioc(boot_path), read_ioc(app_path)
    sig = db_signals(db, boot['Mcu.Name'])
    check_file('boot.ioc', boot, COMMON, sig, fail)
    check_file('app.ioc', app, {**COMMON, **APP_ONLY}, sig, fail)
    for k in sorted(set(boot) | set(app)):
        if FROZEN.match(k) and boot.get(k) != app.get(k):
            fail('%s differs: boot.ioc %s, app.ioc %s' % (k, boot.get(k), app.get(k)))
    for f in failures:
        print('FAIL ' + f)
    print('pins: ok' if not failures else 'pins: %d failure(s)' % len(failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
