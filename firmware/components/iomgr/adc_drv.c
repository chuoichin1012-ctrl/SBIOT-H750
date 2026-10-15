#include "adc_drv.h"
#include "adc.h"  // hadc3 (CubeMX: HAL_ADC_MspInit sets PLL2P 20 MHz, the ADC3 clock and the analog switches)
#include "blog.h"
#include "tim.h"  // htim6, MX_TIM6_Init: 240 MHz / 240 / 1000 = 1 kHz

#define ITCM __attribute__((section(".itcm_text")))
#define IRQ_PRIO 6  // under configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY: the ISRs notify

// The scan must end well inside the 1 ms trigger period (spec §6.6: computed < 900 µs). In 0.1 µs at the
// 10 MHz analog clock: channels × ratio × (sampling + 8.5) cycles; sampling 64.5 is 129 half cycles.
#define SAMPLING_HALF_CYCLES 129u  // ADC_SAMPLETIME_64CYCLES_5
#define OVS_RATIO 16u
#define SCAN_TENTHS_US (ADC_CH * OVS_RATIO * (SAMPLING_HALF_CYCLES + 17u) / 2u)
_Static_assert(SCAN_TENTHS_US < 9000u, "the ADC scan must take under 900 us (spec 6.6)");

static uint16_t s_adc_buf[2][ADC_HALF][ADC_CH] __attribute__((section(".adc_buf"), aligned(32)));
static volatile uint32_t s_seq, s_half, s_irqs, s_errors, s_recoveries;
static TaskHandle_t s_io;

// ---- interrupts -----------------------------------------------------------------------------------

ITCM void BDMA_Channel0_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t f = BDMA->ISR & (BDMA_ISR_TCIF0 | BDMA_ISR_HTIF0 | BDMA_ISR_TEIF0);
    BDMA->IFCR = f | BDMA_IFCR_CGIF0;
    s_irqs++;
    uint32_t bits = 0;
    if (f & BDMA_ISR_TEIF0) {
        s_errors++;
        bits = ADC_N_FAULT;
    } else if (f & (BDMA_ISR_HTIF0 | BDMA_ISR_TCIF0)) {
        s_half = (f & BDMA_ISR_TCIF0) ? 1u : 0u;
        s_seq++;
        bits = ADC_N_HALF;
    }
    if (bits && s_io)
        xTaskNotifyFromISR(s_io, bits, eSetBits, &woken);
    portYIELD_FROM_ISR(woken);
}

ITCM void ADC3_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    if (ADC3->ISR & ADC_ISR_OVR) {
        ADC3->IER &= ~ADC_IER_OVRIE;  // every conversion overruns until the recovery: one interrupt
        ADC3->ISR = ADC_ISR_OVR;
        s_irqs++;
        s_errors++;
        if (s_io)
            xTaskNotifyFromISR(s_io, ADC_N_FAULT, eSetBits, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

// ---- start and recovery ---------------------------------------------------------------------------

static void bdma_start(void)
{
    BDMA_Channel0->CCR = 0;
    BDMA->IFCR = BDMA_IFCR_CGIF0 | BDMA_IFCR_CTCIF0 | BDMA_IFCR_CHTIF0 | BDMA_IFCR_CTEIF0;
    BDMA_Channel0->CPAR = (uint32_t)&ADC3->DR;
    BDMA_Channel0->CM0AR = (uint32_t)s_adc_buf;
    BDMA_Channel0->CNDTR = 2u * ADC_HALF * ADC_CH;
    DMAMUX2_Channel0->CCR = BDMA_REQUEST_ADC3;
    // peripheral to memory, half-words both sides, memory increments, circular, HT/TC/TE interrupts
    BDMA_Channel0->CCR = BDMA_CCR_MINC | BDMA_CCR_CIRC | BDMA_CCR_PSIZE_0 | BDMA_CCR_MSIZE_0 | BDMA_CCR_PL_1 |
                         BDMA_CCR_HTIE | BDMA_CCR_TCIE | BDMA_CCR_TEIE;
    BDMA_Channel0->CCR |= BDMA_CCR_EN;
}

static void adc_go(void)
{
    ADC3->ISR = ADC_ISR_OVR | ADC_ISR_EOC | ADC_ISR_EOS;
    ADC3->IER |= ADC_IER_OVRIE;
    LL_ADC_REG_StartConversion(ADC3);  // ADSTART: the conversions now wait for TIM6
}

int adc_drv_start(TaskHandle_t io)
{
    s_io = io;
    ADC_ChannelConfTypeDef ch = {0};
    hadc3.Instance = ADC3;
    hadc3.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;  // PLL2P 20 MHz, /2 inside on rev V: 10 MHz, BOOST 01
    hadc3.Init.Resolution = ADC_RESOLUTION_16B;
    hadc3.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc3.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    hadc3.Init.LowPowerAutoWait = DISABLE;
    hadc3.Init.ContinuousConvMode = DISABLE;
    hadc3.Init.NbrOfConversion = ADC_CH;
    hadc3.Init.DiscontinuousConvMode = DISABLE;
    hadc3.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
    hadc3.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc3.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
    hadc3.Init.Overrun = ADC_OVR_DATA_PRESERVED;  // an overrun stops the stream: a fault, recovered
    hadc3.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
    hadc3.Init.OversamplingMode = ENABLE;
    hadc3.Init.Oversampling.Ratio = OVS_RATIO;  // OVSR = 15
    hadc3.Init.Oversampling.RightBitShift = ADC_RIGHTBITSHIFT_4;
    hadc3.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;  // TROVS 0: 16 from one trigger
    hadc3.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;
    if (HAL_ADC_Init(&hadc3) != HAL_OK) {
        blog("adc: init FAILED\r\n");
        return -1;
    }
    ch.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
    ch.SingleDiff = ADC_SINGLE_ENDED;
    ch.OffsetNumber = ADC_OFFSET_NONE;
    ch.Channel = ADC_CHANNEL_0;  // AI1, PC2_C
    ch.Rank = ADC_REGULAR_RANK_1;
    bool ok = HAL_ADC_ConfigChannel(&hadc3, &ch) == HAL_OK;
    ch.Channel = ADC_CHANNEL_1;  // AI2, PC3_C
    ch.Rank = ADC_REGULAR_RANK_2;
    ok = ok && HAL_ADC_ConfigChannel(&hadc3, &ch) == HAL_OK;
    // After the regulator is on, before the ADC is enabled (spec §6.6).
    ok = ok && HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED) == HAL_OK;
    ok = ok && HAL_ADC_Start(&hadc3) == HAL_OK;  // enables ADC3 and sets ADSTART (waits for TIM6)
    if (!ok) {
        blog("adc: channels, calibration or enable FAILED\r\n");
        return -1;
    }
    LL_ADC_REG_StopConversion(ADC3);
    while (LL_ADC_REG_IsConversionOngoing(ADC3)) {
    }
    __HAL_RCC_BDMA_CLK_ENABLE();
    MX_TIM6_Init();
    TIM6->CR2 = (TIM6->CR2 & ~TIM_CR2_MMS) | TIM_TRGO_UPDATE;  // TRGO on update (CubeMX leaves it reset)
    bdma_start();
    adc_go();
    HAL_NVIC_SetPriority(BDMA_Channel0_IRQn, IRQ_PRIO, 0);
    HAL_NVIC_SetPriority(ADC3_IRQn, IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(BDMA_Channel0_IRQn);
    HAL_NVIC_EnableIRQ(ADC3_IRQn);
    TIM6->CR1 |= TIM_CR1_CEN;  // last
    blog("adc: ADC3 INP0/INP1, x%u oversampling, scan %u.%u us of the 1 ms trigger\r\n", (unsigned)OVS_RATIO,
         (unsigned)(SCAN_TENTHS_US / 10u), (unsigned)(SCAN_TENTHS_US % 10u));
    return 0;
}

void adc_drv_recover(void)
{
    TIM6->CR1 &= ~TIM_CR1_CEN;
    LL_ADC_REG_StopConversion(ADC3);
    while (LL_ADC_REG_IsConversionOngoing(ADC3)) {
    }
    BDMA_Channel0->CCR &= ~BDMA_CCR_EN;
    ADC3->ISR = ADC_ISR_OVR | ADC_ISR_EOC | ADC_ISR_EOS;
    bdma_start();
    adc_go();
    TIM6->CR1 |= TIM_CR1_CEN;
    s_recoveries++;
}

void adc_drv_inject_fault(void) { BDMA_Channel0->CCR &= ~BDMA_CCR_EN; }

const uint16_t (*adc_drv_half(void))[ADC_CH] { return s_adc_buf[s_half]; }

uint32_t adc_drv_seq(void) { return s_seq; }

void adc_drv_info(adc_drv_info_t *i)
{
    i->irqs = s_irqs;
    i->errors = s_errors;
    i->recoveries = s_recoveries;
    i->cr = ADC3->CR;
    i->cfgr = ADC3->CFGR;
    i->cfgr2 = ADC3->CFGR2;
    i->smpr1 = ADC3->SMPR1;
}
