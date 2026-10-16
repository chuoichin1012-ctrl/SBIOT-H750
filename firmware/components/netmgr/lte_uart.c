// UART3 to the modem (spec §7.2, Plan 10). See lte_uart.h. The DMA register handling follows uart_drv.c.
#include "lte_uart.h"
#include <string.h>
#include "semphr.h"
#include "serial_frame.h"  // sf_ring_wr
#include "usart.h"         // huart3 (CubeMX: its MspInit sets PD8/PD9 and the clock)

#define ISR_PRIO 6  // under configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY: the ISRs may notify
#define ITCM __attribute__((section(".itcm_text")))
#define RX_IDX 6u   // DMA1 stream numbers
#define TX_IDX 7u
#define DMA_TC 0x20u
#define DMA_HT 0x10u
#define DMA_TE 0x08u
#define DMA_ALL 0x3Du

static uint8_t s_lte_ring[LTE_UART_RING] __attribute__((section(".sram1_bss"), aligned(32)));
static uint8_t s_lte_tx[LTE_UART_TXBUF] __attribute__((section(".sram1_bss"), aligned(32)));
static TaskHandle_t volatile s_reader;
static volatile uint32_t s_laps, s_last_wr, s_errors, s_tx_flags;
static SemaphoreHandle_t s_tx_sem;
static StaticSemaphore_t s_tx_sem_mem;

// streams 4-7: HISR/HIFCR at bit 0, 6, 16, 22
static uint32_t shift(uint32_t idx)
{
    static const uint8_t k[4] = {0, 6, 16, 22};
    return k[idx & 3];
}
static ITCM uint32_t flags(uint32_t idx) { return (DMA1->HISR >> shift(idx)) & DMA_ALL; }
static ITCM void clear(uint32_t idx, uint32_t f) { DMA1->HIFCR = f << shift(idx); }

static void stream_stop(DMA_Stream_TypeDef *s)
{
    s->CR &= ~DMA_SxCR_EN;
    while (s->CR & DMA_SxCR_EN) {
    }
}

static ITCM uint32_t pos(void)
{
    uint32_t wr = sf_ring_wr(s_laps, DMA1_Stream6->NDTR, LTE_UART_RING, s_last_wr);
    s_last_wr = wr;
    return wr;
}

static ITCM void wake(BaseType_t *woken)
{
    if (s_reader)
        vTaskNotifyGiveFromISR(s_reader, woken);
}

static void rx_start(void)
{
    DMA_Stream_TypeDef *s = DMA1_Stream6;
    stream_stop(s);
    clear(RX_IDX, DMA_ALL);
    s->PAR = (uint32_t)&USART3->RDR;
    s->M0AR = (uint32_t)s_lte_ring;
    s->NDTR = LTE_UART_RING;
    s->FCR = 0;
    s->CR = DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_PL_0 | DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_TRBUFF;
    DMAMUX1_Channel6->CCR = DMA_REQUEST_USART3_RX;
    s->CR |= DMA_SxCR_EN;
}

ITCM void USART3_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t isr = USART3->ISR;
    if (isr & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) {
        USART3->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
        s_errors++;
    }
    if (isr & USART_ISR_IDLE) {
        USART3->ICR = USART_ICR_IDLECF;
        pos();
        wake(&woken);
    }
    if ((USART3->CR1 & USART_CR1_TCIE) && (isr & USART_ISR_TC)) {
        USART3->CR1 &= ~USART_CR1_TCIE;
        s_tx_flags |= 2u;
        xSemaphoreGiveFromISR(s_tx_sem, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

ITCM void DMA1_Stream6_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t f = flags(RX_IDX);
    clear(RX_IDX, f);
    if (f & DMA_TC)
        s_laps++;
    if (f & DMA_TE) {  // the stream stopped: a new lap; the reader sees an overrun
        s_errors++;
        s_laps++;
        rx_start();
    }
    pos();
    wake(&woken);
    portYIELD_FROM_ISR(woken);
}

ITCM void DMA1_Stream7_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t f = flags(TX_IDX);
    clear(TX_IDX, f);
    if (f & DMA_TE)
        s_tx_flags |= 4u;
    else if (f & DMA_TC)
        USART3->CR1 |= USART_CR1_TCIE;  // the last byte is in the USART: wait for it to leave
    if (f & DMA_TE)
        xSemaphoreGiveFromISR(s_tx_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

void lte_uart_reader(TaskHandle_t reader) { s_reader = reader; }

int lte_uart_init(void)
{
    s_tx_sem = xSemaphoreCreateBinaryStatic(&s_tx_sem_mem);
    UART_HandleTypeDef *h = &huart3;
    h->Instance = USART3;
    h->Init.BaudRate = 115200;
    h->Init.WordLength = UART_WORDLENGTH_8B;
    h->Init.StopBits = UART_STOPBITS_1;
    h->Init.Parity = UART_PARITY_NONE;
    h->Init.Mode = UART_MODE_TX_RX;
    h->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    h->Init.OverSampling = UART_OVERSAMPLING_16;
    h->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    h->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    h->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(h) != HAL_OK)
        return -1;
    HAL_UARTEx_DisableFifoMode(h);
    USART3->CR1 &= ~USART_CR1_UE;
    USART3->CR3 |= USART_CR3_DMAR | USART_CR3_EIE;
    USART3->CR1 |= USART_CR1_IDLEIE | USART_CR1_PEIE;
    USART3->ICR = 0xFFFFFFFFu;
    USART3->CR1 |= USART_CR1_UE;
    __HAL_RCC_DMA1_CLK_ENABLE();
    rx_start();
    HAL_NVIC_SetPriority(USART3_IRQn, ISR_PRIO, 0);
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, ISR_PRIO, 0);
    HAL_NVIC_SetPriority(DMA1_Stream7_IRQn, ISR_PRIO, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);
    return 0;
}

const uint8_t *lte_uart_ring(void) { return s_lte_ring; }

uint32_t lte_uart_wr(void)
{
    taskENTER_CRITICAL();  // pos() also runs in the ISRs
    uint32_t wr = pos();
    taskEXIT_CRITICAL();
    return wr;
}

int lte_uart_write(const uint8_t *p, size_t n, uint32_t timeout_ms)
{
    if (!n)
        return 0;
    if (n > LTE_UART_TXBUF)
        return -1;
    if (p != s_lte_tx)
        memcpy(s_lte_tx, p, n);
    xSemaphoreTake(s_tx_sem, 0);
    s_tx_flags = 0;
    DMA_Stream_TypeDef *s = DMA1_Stream7;
    stream_stop(s);
    clear(TX_IDX, DMA_ALL);
    s->PAR = (uint32_t)&USART3->TDR;
    s->M0AR = (uint32_t)s_lte_tx;
    s->NDTR = (uint32_t)n;
    s->FCR = 0;
    s->CR = DMA_SxCR_DIR_0 | DMA_SxCR_MINC | DMA_SxCR_PL_0 | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_TRBUFF;
    DMAMUX1_Channel7->CCR = DMA_REQUEST_USART3_TX;
    USART3->ICR = USART_ICR_TCCF;
    __DMB();  // the copy before the DMA reads it (SRAM1, non-cacheable)
    s->CR |= DMA_SxCR_EN;
    USART3->CR3 |= USART_CR3_DMAT;
    // 115200 baud: 87 us a byte; the deadline adds that to the caller's
    TickType_t wait = pdMS_TO_TICKS(timeout_ms + (uint32_t)(n * 87u / 1000u) + 2u);
    bool done = xSemaphoreTake(s_tx_sem, wait) == pdTRUE && !(s_tx_flags & 4u);
    if (!done) {
        stream_stop(s);
        clear(TX_IDX, DMA_ALL);
        USART3->CR1 &= ~USART_CR1_TCIE;
        s_errors++;
    }
    USART3->CR3 &= ~USART_CR3_DMAT;
    return done ? 0 : -1;
}

uint8_t *lte_uart_txbuf(void) { return s_lte_tx; }
uint32_t lte_uart_errors(void) { return s_errors; }
