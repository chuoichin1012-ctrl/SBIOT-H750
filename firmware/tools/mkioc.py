#!/usr/bin/env python3
"""How cube/boot/boot.ioc and cube/app/app.ioc were authored (spec §3, §6.2).

    python3 stm32/firmware/tools/mkioc.py boot stm32/firmware/cube/boot/boot.ioc
    python3 stm32/firmware/tools/mkioc.py app  stm32/firmware/cube/app/app.ioc

CubeMX canonicalises both files on the first tools/cubegen.sh run. From then on the committed .ioc
files are the source: change them in CubeMX (or by hand), then run cubegen.sh and check_pins.py.
This script records every non-default value and why it is there.
"""
import sys

kind, out = sys.argv[1], sys.argv[2]
assert kind in ('boot', 'app')
BOOT = kind == 'boot'

pins = []


def pin(p, **kv):
    pins.append((p, kv))


pin('PH0-OSC_IN\\ (PH0)', Mode='HSE-External-Oscillator', Signal='RCC_OSC_IN')
pin('PH1-OSC_OUT\\ (PH1)', Mode='HSE-External-Oscillator', Signal='RCC_OSC_OUT')
pin('PA13\\ (JTMS/SWDIO)', Mode='Serial_Wire', Signal='DEBUG_JTMS-SWDIO')
pin('PA14\\ (JTCK/SWCLK)', Mode='Serial_Wire', Signal='DEBUG_JTCK-SWCLK')
for p, s in [('PB2', 'QUADSPI_CLK'), ('PC11', 'QUADSPI_BK2_NCS'), ('PE7', 'QUADSPI_BK2_IO0'),
             ('PE8', 'QUADSPI_BK2_IO1'), ('PE9', 'QUADSPI_BK2_IO2'), ('PE10', 'QUADSPI_BK2_IO3')]:
    pin(p, Mode='Single Bank 2', Signal=s)
pin('PB3\\ (JTDO/TRACESWO)', Mode='Asynchronous', Signal='UART7_RX')
pin('PB4\\ (NJTRST)', Mode='Asynchronous', Signal='UART7_TX')
if not BOOT:
    pin('PA9', Mode='Asynchronous', Signal='USART1_TX')
    pin('PA10', Mode='Asynchronous', Signal='USART1_RX')
    pin('PA12', Mode='Hardware Flow Control (RS485)', Signal='USART1_DE')
    pin('PD5', Mode='Asynchronous', Signal='USART2_TX')
    pin('PD6', Mode='Asynchronous', Signal='USART2_RX')
    pin('PD4', Mode='Hardware Flow Control (RS485)', Signal='USART2_DE')
    pin('PD8', Mode='Asynchronous', Signal='USART3_TX')
    pin('PD9', Mode='Asynchronous', Signal='USART3_RX')
    for p, s in [('PA1', 'ETH_REF_CLK'), ('PA2', 'ETH_MDIO'), ('PA7', 'ETH_CRS_DV'), ('PC1', 'ETH_MDC'),
                 ('PC4', 'ETH_RXD0'), ('PC5', 'ETH_RXD1'), ('PB11', 'ETH_TX_EN'), ('PB12', 'ETH_TXD0'),
                 ('PB13', 'ETH_TXD1')]:
        pin(p, Mode='RMII', Signal=s)
    pin('PE2', Mode='Full_Duplex_Master', Signal='SPI4_SCK')
    pin('PE5', Mode='Full_Duplex_Master', Signal='SPI4_MISO')
    pin('PE6', Mode='Full_Duplex_Master', Signal='SPI4_MOSI')
    pin('PC2_C', Mode='IN0-Single-Ended', Signal='ADC3_INP0')
    pin('PC3_C', Mode='IN1-Single-Ended', Signal='ADC3_INP1')


def out_pin(p, label, state):
    pin(p, Signal='GPIO_Output', GPIO_Label=label, PinState=state, Locked='true', GPIOParameters='PinState,GPIO_Label')


def in_pin(p, label, pupd=None):
    kv = dict(Signal='GPIO_Input', GPIO_Label=label, Locked='true', GPIOParameters='GPIO_Label')
    if pupd:
        kv.update(GPIO_PuPd=pupd, GPIOParameters='GPIO_PuPd,GPIO_Label')
    pin(p, **kv)


# Safe levels (spec §11.2 step 2): latch outputs disabled (OE high), D1/D2/LE low, modem PWRKEY and
# RESET_N released, PHY held in reset, SPI4 flash deselected (software NSS on PE4).
out_pin('PC10', 'WDI_LED', 'GPIO_PIN_RESET')
out_pin('PE11', 'LED_NET', 'GPIO_PIN_RESET')
out_pin('PD10', 'DO1_D', 'GPIO_PIN_RESET')
out_pin('PD11', 'DO2_D', 'GPIO_PIN_RESET')
out_pin('PD14', 'LATCH_LE', 'GPIO_PIN_RESET')
out_pin('PD15', 'LATCH_OE', 'GPIO_PIN_SET')
out_pin('PA5', 'LTE_PWR', 'GPIO_PIN_RESET')
out_pin('PA4', 'LTE_RESET', 'GPIO_PIN_RESET')
out_pin('PD13', 'ETH_RESET', 'GPIO_PIN_RESET')
out_pin('PE4', 'SPI4_NSS', 'GPIO_PIN_SET')
in_pin('PA8', 'RELOAD', 'GPIO_PULLUP')
in_pin('PB7', 'DI1')
in_pin('PB6', 'DI2')

# GPIO speed per peripheral: the output slew must suit the signal rate, or the peripheral fails (with
# CubeMX's default "Low" every QSPI read timed out at 80 MHz). QSPI, ETH (RMII 50 MHz) and SPI very
# high, UART high (team rule, 2026-10-15). The software NSS of SPI4 follows its bus.
SPEED = [('QUADSPI_', 'GPIO_SPEED_FREQ_VERY_HIGH'), ('ETH_', 'GPIO_SPEED_FREQ_VERY_HIGH'),
         ('SPI4_', 'GPIO_SPEED_FREQ_VERY_HIGH'), ('UART', 'GPIO_SPEED_FREQ_HIGH'), ('USART', 'GPIO_SPEED_FREQ_HIGH')]
for p, kv in pins:
    key = kv.get('GPIO_Label', '') if kv['Signal'].startswith('GPIO_') else kv['Signal']
    for prefix, speed in SPEED:
        if key.startswith(prefix):
            kv['GPIO_Speed'] = speed
            kv['GPIOParameters'] = ','.join(x for x in [kv.get('GPIOParameters'), 'GPIO_Speed'] if x)

vps = [('VP_SYS_VS_Systick', 'SysTick', 'SYS_VS_Systick'),
       ('VP_MEMORYMAP_VS_MEMORYMAP', 'CurAppReg', 'MEMORYMAP_VS_MEMORYMAP')]
if BOOT:
    vps.append(('VP_IWDG1_VS_IWDG', 'IWDG_Activate', 'IWDG1_VS_IWDG'))
else:
    vps += [('VP_TIM2_VS_ClockSourceINT', 'Internal', 'TIM2_VS_ClockSourceINT'),
            ('VP_TIM6_VS_ClockSourceINT', 'Enable_Timer', 'TIM6_VS_ClockSourceINT')]

ips = ['CORTEX_M7', 'DEBUG', 'MEMORYMAP', 'NVIC', 'QUADSPI', 'RCC', 'SYS', 'UART7']
ips += ['IWDG1'] if BOOT else ['ADC3', 'ETH', 'SPI4', 'TIM2', 'TIM6', 'USART1', 'USART2', 'USART3']
ips.sort()

L = []
a = L.append
a('#MicroXplorer Configuration settings - do not modify')
a('File.Version=6')
a('KeepUserPlacement=false')
a('Mcu.CPN=STM32H750VBT6')
a('Mcu.Family=STM32H7')
for i, ip in enumerate(ips):
    a('Mcu.IP%d=%s' % (i, ip))
a('Mcu.IPNb=%d' % len(ips))
a('Mcu.Name=STM32H750VBTx')
a('Mcu.Package=LQFP100')
allp = [p for p, _ in pins] + [v[0] for v in vps]
for i, p in enumerate(allp):
    a('Mcu.Pin%d=%s' % (i, p))
a('Mcu.PinsNb=%d' % len(allp))
a('Mcu.ThirdPartyNb=0')
a('Mcu.UserConstants=')
a('Mcu.UserName=STM32H750VBTx')
for p, kv in pins:
    for k in sorted(kv):
        a('%s.%s=%s' % (p, k, kv[k]))
for v, m, s in vps:
    a('%s.Mode=%s' % (v, m))
    a('%s.Signal=%s' % (v, s))


def ipp(ip, kv, order=None):
    # CubeMX applies parameters in IPParameters order and drops any whose condition is not yet true,
    # so the enabling parameters come first when given.
    keys = (order or []) + sorted(k for k in kv if k not in (order or []))
    for k in keys:
        a('%s.%s=%s' % (ip, k, kv[k]))
    a('%s.IPParameters=%s' % (ip, ','.join(keys)))


# PLL1 and the bus tree are the frozen part (bootloader, spec §6.2). PLL2 (ADC) is configured by the
# app's HAL_ADC_MspInit, so only app.ioc carries it.
rcc = dict(HSE_VALUE='25000000', PLLSourceVirtual='RCC_PLLSOURCE_HSE', SYSCLKSource='RCC_SYSCLKSOURCE_PLLCLK',
           DIVM1='5', DIVN1='192', DIVP1='2', DIVQ1='6', DIVR1='2',
           D1CPRE='RCC_SYSCLK_DIV1', HPRE='RCC_HCLK_DIV2', D1PPRE='RCC_APB3_DIV2', D2PPRE1='RCC_APB1_DIV2',
           D2PPRE2='RCC_APB2_DIV2', D3PPRE='RCC_APB4_DIV2',
           PWR_Regulator_Voltage_Scale='PWR_REGULATOR_VOLTAGE_SCALE0', SupplySource='PWR_LDO_SUPPLY',
           # QSPI from pll1_q_ck 160 MHz, divided by 2: an even division keeps the 50 % duty cycle that
           # ES0392 §2.7.6 asks for (240 MHz HCLK / 3 would not).
           QSPICLockSelection='RCC_QSPICLKSOURCE_PLL',
           USART234578CLockSelection='RCC_USART234578CLKSOURCE_D2PCLK1')
if not BOOT:
    rcc.update(DIVM2='5', DIVN2='80', DIVP2='20', DIVQ2='4', DIVR2='4', ADCCLockSelection='RCC_ADCCLKSOURCE_PLL2',
               Spi45ClockSelection='RCC_SPI45CLKSOURCE_D2PCLK1', USART16CLockSelection='RCC_USART16CLKSOURCE_D2PCLK2')
ipp('RCC', rcc)
# FlashSize 22 (8 MB) for a 4 MB part: ES0396 §2.4.4, a memory-mapped read of the last byte of the
# FSIZE region returns 0 and a repeated read stalls the AXI bus. MPU region 1 still ends at 4 MB.
ipp('QUADSPI', dict(ClockPrescaler='1', FlashSize='22', SampleShifting='QSPI_SAMPLE_SHIFTING_HALFCYCLE',
                    ChipSelectHighTime='QSPI_CS_HIGH_TIME_2_CYCLE', FifoThreshold='4', DualFlash='QSPI_DUALFLASH_DISABLE'))
ipp('UART7', dict(BaudRate='115200', VirtualMode='VM_ASYNC'))
if BOOT:
    # LSI 32 kHz / 32 = 1 kHz; 1600 ticks = 1.6 s nominal (1.5 to 1.74 s over the LSI range).
    ipp('IWDG1', dict(Prescaler='IWDG_PRESCALER_32', Reload='1600'))
    # Background region 0 (CubeMX default: 0x60000000-0xDFFFFFFF no access) plus the 4 MB QSPI window.
    # With default_mode_Activation=1 CubeMX uses the "_S" variant of every region parameter.
    r1 = 'Cortex_Memory_Protection_Unit_Region1_Settings_S'
    cm = {'MPU_Control': 'MPU_PRIVILEGED_DEFAULT', 'default_mode_Activation': '1',
          'Enable_S-' + r1: 'MPU_REGION_ENABLE', 'BaseAddress_S-' + r1: '0x90000000', 'Size_S-' + r1: 'MPU_REGION_SIZE_4MB',
          'SubRegionDisable_S-' + r1: '0x0', 'TypeExtField_S-' + r1: 'MPU_TEX_LEVEL0',
          'AccessPermission_S-' + r1: 'MPU_REGION_PRIV_RO_URO', 'DisableExec_S-' + r1: 'MPU_INSTRUCTION_ACCESS_ENABLE',
          'IsShareable_S-' + r1: 'MPU_ACCESS_NOT_SHAREABLE', 'IsCacheable_S-' + r1: 'MPU_ACCESS_CACHEABLE',
          'IsBufferable_S-' + r1: 'MPU_ACCESS_NOT_BUFFERABLE'}
    ipp('CORTEX_M7', cm, ['default_mode_Activation', 'MPU_Control', 'Enable_S-' + r1])
else:
    ipp('TIM2', dict(Prescaler='239', Period='4294967295'))  # 240 MHz / 240 = 1 MHz, 32-bit free run
    ipp('TIM6', dict(Prescaler='239', Period='999'))         # 1 kHz ADC trigger (Plan 07)

# NVIC flags: enabled:preempt:sub:?:?:generate-handler:?:call-HAL:? (field 6 generates the handler).
for irq in ['NonMaskableInt', 'HardFault', 'MemoryManagement', 'BusFault', 'UsageFault', 'DebugMonitor']:
    a('NVIC.%s_IRQn=true\\:0\\:0\\:false\\:false\\:true\\:false\\:false\\:false' % irq)
own = 'true' if BOOT else 'false'  # in the app, FreeRTOS owns SVC, PendSV and SysTick
for irq in ['SVCall', 'PendSV']:
    a('NVIC.%s_IRQn=true\\:0\\:0\\:false\\:false\\:%s\\:false\\:false\\:false' % (irq, own))
a('NVIC.SysTick_IRQn=true\\:15\\:0\\:false\\:false\\:%s\\:false\\:%s\\:false' % (own, own))
a('NVIC.PriorityGroup=NVIC_PRIORITYGROUP_4')
a('NVIC.ForceEnableDMAVector=true')
a('board=custom')


def fl(entries):
    return ','.join('%d-%s-%s-%s-HAL-%s' % e for e in entries)


if BOOT:
    funcs = [(1, 'SystemClock_Config', 'RCC', 'false', 'false'), (2, 'MX_GPIO_Init', 'GPIO', 'false', 'true'),
             (3, 'MX_IWDG1_Init', 'IWDG1', 'false', 'true'), (4, 'MX_QUADSPI_Init', 'QUADSPI', 'false', 'true'),
             (5, 'MX_UART7_Init', 'UART7', 'false', 'true'), (0, 'MX_CORTEX_M7_Init', 'CORTEX_M7', 'false', 'true')]
else:
    # "true" in the 4th field = do not generate the call: the clocks and QSPI belong to the bootloader,
    # the MPU to Reset_Handler; the peripherals of later plans stay uncalled until those plans.
    funcs = [(1, 'SystemClock_Config', 'RCC', 'true', 'false'), (2, 'MX_GPIO_Init', 'GPIO', 'false', 'true'),
             (3, 'MX_TIM2_Init', 'TIM2', 'false', 'true'), (4, 'MX_UART7_Init', 'UART7', 'false', 'true'),
             (5, 'MX_QUADSPI_Init', 'QUADSPI', 'true', 'true'), (6, 'MX_ADC3_Init', 'ADC3', 'true', 'true'),
             (7, 'MX_ETH_Init', 'ETH', 'true', 'true'), (8, 'MX_SPI4_Init', 'SPI4', 'true', 'true'),
             (9, 'MX_TIM6_Init', 'TIM6', 'true', 'true'), (10, 'MX_USART1_UART_Init', 'USART1', 'true', 'true'),
             (11, 'MX_USART2_UART_Init', 'USART2', 'true', 'true'), (12, 'MX_USART3_UART_Init', 'USART3', 'true', 'true'),
             (0, 'MX_CORTEX_M7_Init', 'CORTEX_M7', 'true', 'true')]
pm = dict(AskForMigrate='false', BackupPrevious='false', CompilerOptimize='6', ComputerToolchain='false',
          CoupleFile='true', CustomerFirmwarePackage='', DefaultFWLocation='true', DeletePrevious='true',
          DeviceId='STM32H750VBTx', FirmwarePackage='STM32Cube FW_H7 V1.12.0', FreePins='false',
          HalAssertFull='false', HeapSize='0x0', KeepUserCode='true', LastFirmware='false', LibraryCopy='1',
          MainLocation='Src', NoMain='false', PreviousToolchain='', ProjectBuild='false',
          ProjectFileName=kind + '.ioc', ProjectName=kind, ProjectStructure='', RegisterCallBack='',
          StackSize='0x1000', TargetToolchain='CMake', ToolChainLocation='', UAScriptAfterPath='',
          UAScriptBeforePath='', UnderRoot='false', functionlistsort=fl(funcs))
for k in sorted(pm):
    a('ProjectManager.%s=%s' % (k, pm[k]))
open(out, 'w').write('\n'.join(L) + '\n')
