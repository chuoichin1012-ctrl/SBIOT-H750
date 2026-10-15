#include "uart_drv.h"
#include <string.h>
#include "now_us.h"
#include "semphr.h"
#include "task.h"
#include "usart.h"  // huart1, huart2 (CubeMX: their MspInit sets the pins and clocks)

#define ISR_PRIO 6       // under configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY: the ISRs may notify
#define F_DMA_DONE 1u
#define F_USART_TC 2u
#define F_DMA_ERR 4u
#define F_FLOW 8u
#define ITCM __attribute__((section(".itcm_text")))  // ISRs in ITCM (spec §5.3)

typedef struct {
    USART_TypeDef *u;
    UART_HandleTypeDef *h;
    DMA_Stream_TypeDef *rx, *tx;
    DMAMUX_Channel_TypeDef *mrx, *mtx;
    uint8_t rx_req, tx_req, rx_idx, tx_idx;  // DMAMUX requests, DMA1 stream numbers
    IRQn_Type irq_u, irq_rx, irq_tx;
    uint8_t cc;  // TIM2 channel of the t3.5 one-shot (1 or 2)
    // RX
    QueueHandle_t q;
    volatile uint32_t laps, last_wr, wr_at_rto;
    volatile uint8_t mode;  // sf_mode_t
    volatile bool armed;    // the t3.5 one-shot is running
    uint32_t gap_bits, t15_bits, t35_minus_us;
    volatile uint32_t errors, lost;
    // TX
    SemaphoreHandle_t tx_sem;
    StaticSemaphore_t tx_sem_mem;
    volatile uint32_t tx_flags;
    volatile bool paused;  // the device's XOFF
    bool inject;
    uint32_t char_us;
    sf_tx_t txc;  // the TX controller (serial_frame)
} port_t;

static uint8_t s_uart_ring[UART_PORTS][UART_RING] __attribute__((section(".sram1_bss"), aligned(32)));
static uint8_t s_uart_txbuf[UART_PORTS][UART_TXBUF] __attribute__((section(".sram1_bss"), aligned(32)));

static port_t s_ports[UART_PORTS] = {
    {.u = USART1, .h = &huart1, .rx = DMA1_Stream2, .tx = DMA1_Stream3, .mrx = DMAMUX1_Channel2, .mtx = DMAMUX1_Channel3,
     .rx_req = DMA_REQUEST_USART1_RX, .tx_req = DMA_REQUEST_USART1_TX, .rx_idx = 2, .tx_idx = 3, .irq_u = USART1_IRQn,
     .irq_rx = DMA1_Stream2_IRQn, .irq_tx = DMA1_Stream3_IRQn, .cc = 1},
    {.u = USART2, .h = &huart2, .rx = DMA1_Stream4, .tx = DMA1_Stream5, .mrx = DMAMUX1_Channel4, .mtx = DMAMUX1_Channel5,
     .rx_req = DMA_REQUEST_USART2_RX, .tx_req = DMA_REQUEST_USART2_TX, .rx_idx = 4, .tx_idx = 5, .irq_u = USART2_IRQn,
     .irq_rx = DMA1_Stream4_IRQn, .irq_tx = DMA1_Stream5_IRQn, .cc = 2},
};

// ---- DMA flags: streams 0-3 in LISR/LIFCR, 4-7 in HISR/HIFCR, at bit 0, 6, 16, 22 ----------------

#define DMA_TC 0x20u
#define DMA_HT 0x10u
#define DMA_TE 0x08u
#define DMA_ALL 0x3Du

static uint32_t dma_shift(uint8_t idx)
{
    static const uint8_t k[4] = {0, 6, 16, 22};
    return k[idx & 3];
}

static ITCM uint32_t dma_flags(uint8_t idx)
{
    return ((idx < 4 ? DMA1->LISR : DMA1->HISR) >> dma_shift(idx)) & DMA_ALL;
}

static ITCM void dma_clear(uint8_t idx, uint32_t f)
{
    if (idx < 4)
        DMA1->LIFCR = f << dma_shift(idx);
    else
        DMA1->HIFCR = f << dma_shift(idx);
}

static void stream_stop(DMA_Stream_TypeDef *s)
{
    s->CR &= ~DMA_SxCR_EN;
    while (s->CR & DMA_SxCR_EN) {
    }
}

// ---- RX ---------------------------------------------------------------------------------------------

// The DMA's absolute write position; in an ISR, or in a task inside a critical section.
static ITCM uint32_t pos(port_t *p)
{
    uint32_t wr = sf_ring_wr(p->laps, p->rx->NDTR, UART_RING, p->last_wr);
    p->last_wr = wr;
    return wr;
}

static ITCM void post(port_t *p, uint8_t type, uint32_t wr, BaseType_t *woken)
{
    uart_ev_t ev = {.type = type, .wr = wr};
    if (xQueueSendFromISR(p->q, &ev, woken) != pdTRUE)
        p->lost++;  // serial_rx sees the count move and resyncs, as after an overrun
}

static void rx_start(port_t *p)
{
    stream_stop(p->rx);
    dma_clear(p->rx_idx, DMA_ALL);
    p->rx->PAR = (uint32_t)&p->u->RDR;
    p->rx->M0AR = (uint32_t)s_uart_ring[p - s_ports];
    p->rx->NDTR = UART_RING;
    p->rx->FCR = 0;  // direct mode
    // peripheral to memory, bytes, memory increments, circular; TRBUFF for a U(S)ART (as HAL_DMA_Init, ES0392)
    p->rx->CR = DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_PL_0 | DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_TRBUFF;
    p->mrx->CCR = p->rx_req;
    p->rx->CR |= DMA_SxCR_EN;
}

static ITCM void arm_oneshot(port_t *p)
{
    volatile uint32_t *ccr = p->cc == 1 ? &TIM2->CCR1 : &TIM2->CCR2;
    uint32_t ie = p->cc == 1 ? TIM_DIER_CC1IE : TIM_DIER_CC2IE, flag = p->cc == 1 ? TIM_SR_CC1IF : TIM_SR_CC2IF;
    *ccr = TIM2->CNT + p->t35_minus_us;
    TIM2->SR = ~flag;
    TIM2->DIER |= ie;
    p->armed = true;
}

// A line error is posted before any event that hands its byte on: the DMA moves a byte only once the
// USART has set the byte's error flags, so both interrupts look at them first.
static ITCM void rx_errors(port_t *p, BaseType_t *woken)
{
    if (p->u->ISR & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) {
        p->u->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
        p->errors++;
        post(p, UART_EV_ERROR, pos(p), woken);
    }
}

static ITCM void usart_isr(port_t *p)
{
    BaseType_t woken = pdFALSE;
    rx_errors(p, &woken);
    uint32_t isr = p->u->ISR;
    if (isr & USART_ISR_RTOF) {
        p->u->ICR = USART_ICR_RTOCF;
        uint32_t wr = pos(p);
        if (p->mode == SF_RTU) {
            // A one-shot still running means bytes came inside t1.5..t3.5 (above 19 200 baud the
            // next RTO can come before it expires): a violation now, and the one-shot starts again.
            if (p->armed)
                post(p, UART_EV_RTU_VIOL, wr, &woken);
            p->wr_at_rto = wr;
            arm_oneshot(p);
        } else {
            post(p, UART_EV_GAP, wr, &woken);
        }
    }
    if ((p->u->CR1 & USART_CR1_TCIE) && (isr & USART_ISR_TC)) {
        p->u->CR1 &= ~USART_CR1_TCIE;
        p->tx_flags |= F_USART_TC;
        xSemaphoreGiveFromISR(p->tx_sem, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static ITCM void rx_dma_isr(port_t *p)
{
    BaseType_t woken = pdFALSE;
    rx_errors(p, &woken);
    uint32_t f = dma_flags(p->rx_idx);
    dma_clear(p->rx_idx, f);
    if (f & DMA_TC)
        p->laps++;
    if (f & DMA_TE) {  // the stream stopped: start it again on a new lap; the reader sees an overrun
        p->errors++;
        p->laps++;
        rx_start(p);
        post(p, UART_EV_ERROR, pos(p), &woken);
    } else if (f & (DMA_HT | DMA_TC)) {
        post(p, UART_EV_DATA, pos(p), &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static ITCM void tx_dma_isr(port_t *p)
{
    BaseType_t woken = pdFALSE;
    uint32_t f = dma_flags(p->tx_idx);
    dma_clear(p->tx_idx, f);
    if (f & DMA_TE)
        p->tx_flags |= F_DMA_ERR;
    else if (f & DMA_TC)
        p->tx_flags |= F_DMA_DONE;
    if (f & (DMA_TE | DMA_TC))
        xSemaphoreGiveFromISR(p->tx_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

static ITCM void oneshot_isr(port_t *p, uint32_t ie, uint32_t flag)
{
    if (!(TIM2->DIER & ie) || !(TIM2->SR & flag))
        return;
    TIM2->DIER &= ~ie;
    TIM2->SR = ~flag;
    p->armed = false;
    BaseType_t woken = pdFALSE;
    uint32_t wr = pos(p);
    bool busy = (p->u->ISR & USART_ISR_BUSY) != 0;
    post(p, sf_rtu_violation(p->wr_at_rto, wr, busy) ? UART_EV_RTU_VIOL : UART_EV_RTU_END, wr, &woken);
    portYIELD_FROM_ISR(woken);
}

ITCM void USART1_IRQHandler(void) { usart_isr(&s_ports[0]); }
ITCM void USART2_IRQHandler(void) { usart_isr(&s_ports[1]); }
ITCM void DMA1_Stream2_IRQHandler(void) { rx_dma_isr(&s_ports[0]); }
ITCM void DMA1_Stream3_IRQHandler(void) { tx_dma_isr(&s_ports[0]); }
ITCM void DMA1_Stream4_IRQHandler(void) { rx_dma_isr(&s_ports[1]); }
ITCM void DMA1_Stream5_IRQHandler(void) { tx_dma_isr(&s_ports[1]); }
ITCM void TIM2_IRQHandler(void)
{
    oneshot_isr(&s_ports[0], TIM_DIER_CC1IE, TIM_SR_CC1IF);
    oneshot_isr(&s_ports[1], TIM_DIER_CC2IE, TIM_SR_CC2IF);
}

// ---- init -------------------------------------------------------------------------------------------

static uint32_t word_length(const sf_line_t *l)
{
    unsigned bits = l->data_bits + (l->parity ? 1u : 0u);  // the USART's word includes the parity bit
    return bits == 7 ? UART_WORDLENGTH_7B : bits == 9 ? UART_WORDLENGTH_9B : UART_WORDLENGTH_8B;
}

int uart_drv_init(int port, const sf_line_t *line, uint32_t gap_us, bool xonxoff, QueueHandle_t q)
{
    (void)xonxoff;
    if (port < 1 || port > UART_PORTS)
        return -1;
    port_t *p = &s_ports[port - 1];
    uint32_t presc, brr;
    int32_t err;
    if (!sf_brr(120000000u, line->baud, &presc, &brr, &err))
        return -1;
    p->q = q;
    p->tx_sem = xSemaphoreCreateBinaryStatic(&p->tx_sem_mem);
    p->gap_bits = sf_rto_bits(line, gap_us);
    p->t15_bits = sf_rto_bits(line, sf_t15_us(line));
    p->t35_minus_us = sf_t35_oneshot_us(line);  // from the RTO as programmed, in whole bits
    p->char_us = sf_char_us(line);
    p->mode = SF_TRANSPARENT;

    UART_HandleTypeDef *h = p->h;
    h->Instance = p->u;
    h->Init.BaudRate = line->baud;
    h->Init.WordLength = word_length(line);
    h->Init.StopBits = line->stop_bits == 2 ? UART_STOPBITS_2 : UART_STOPBITS_1;
    h->Init.Parity = line->parity == 1 ? UART_PARITY_ODD : line->parity == 2 ? UART_PARITY_EVEN : UART_PARITY_NONE;
    h->Init.Mode = UART_MODE_TX_RX;
    h->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    h->Init.OverSampling = UART_OVERSAMPLING_16;
    h->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    h->Init.ClockPrescaler = presc;
    h->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    // The chip's RS485 driver-enable on the USARTx_DE pin: up 1 bit before the start bit, down 1 bit
    // after the last stop bit (16 sample times at 16x oversampling).
    if (HAL_RS485Ex_Init(h, UART_DE_POLARITY_HIGH, 16, 16) != HAL_OK)
        return -1;
    HAL_UARTEx_DisableFifoMode(h);

    USART_TypeDef *u = p->u;
    u->CR1 &= ~USART_CR1_UE;
    u->RTOR = p->gap_bits;
    u->CR2 |= USART_CR2_RTOEN;
    u->CR3 |= USART_CR3_DMAR | USART_CR3_EIE;
    u->CR1 |= USART_CR1_RTOIE | USART_CR1_PEIE;
    u->ICR = 0xFFFFFFFFu;
    u->CR1 |= USART_CR1_UE;

    __HAL_RCC_DMA1_CLK_ENABLE();
    rx_start(p);
    HAL_NVIC_SetPriority(p->irq_u, ISR_PRIO, 0);
    HAL_NVIC_SetPriority(p->irq_rx, ISR_PRIO, 0);
    HAL_NVIC_SetPriority(p->irq_tx, ISR_PRIO, 0);
    HAL_NVIC_SetPriority(TIM2_IRQn, ISR_PRIO, 0);
    HAL_NVIC_EnableIRQ(p->irq_u);
    HAL_NVIC_EnableIRQ(p->irq_rx);
    HAL_NVIC_EnableIRQ(p->irq_tx);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    return 0;
}

const uint8_t *uart_drv_ring(int port)
{
    return s_uart_ring[port - 1];
}

uint32_t uart_drv_wr(int port)
{
    port_t *p = &s_ports[port - 1];
    taskENTER_CRITICAL();  // pos() also runs in this port's ISRs
    uint32_t wr = pos(p);
    taskEXIT_CRITICAL();
    return wr;
}

bool uart_drv_rx_busy(int port)
{
    return (s_ports[port - 1].u->ISR & USART_ISR_BUSY) != 0;
}

void uart_drv_set_timing(int port, sf_mode_t mode)
{
    port_t *p = &s_ports[port - 1];
    taskENTER_CRITICAL();
    p->mode = (uint8_t)mode;
    p->u->RTOR = mode == SF_RTU ? p->t15_bits : p->gap_bits;
    if (mode != SF_RTU) {
        TIM2->DIER &= ~(p->cc == 1 ? TIM_DIER_CC1IE : TIM_DIER_CC2IE);
        p->armed = false;
    }
    taskEXIT_CRITICAL();
}

void uart_drv_set_mode(int port, sf_mode_t mode)
{
    port_t *p = &s_ports[port - 1];
    uart_drv_set_timing(port, mode);
    uart_ev_t ev = {.type = UART_EV_MODE, .arg = (uint8_t)mode, .wr = uart_drv_wr(port)};
    xQueueSend(p->q, &ev, pdMS_TO_TICKS(100));
}

// ---- TX ---------------------------------------------------------------------------------------------

static void tx_dma_start(port_t *p, size_t n)
{
    stream_stop(p->tx);
    dma_clear(p->tx_idx, DMA_ALL);
    p->tx->PAR = (uint32_t)&p->u->TDR;
    p->tx->M0AR = (uint32_t)s_uart_txbuf[p - s_ports];
    p->tx->NDTR = (uint32_t)n;
    p->tx->FCR = 0;
    p->tx->CR = DMA_SxCR_DIR_0 | DMA_SxCR_MINC | DMA_SxCR_PL_0 | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_TRBUFF;
    p->mtx->CCR = p->tx_req;
    p->u->ICR = USART_ICR_TCCF;
    __DMB();  // the copy into the TX buffer before the DMA reads it (SRAM1, non-cacheable)
    p->tx->CR |= DMA_SxCR_EN;
    p->u->CR3 |= USART_CR3_DMAT;
}

static void tx_abort(port_t *p)
{
    stream_stop(p->tx);
    dma_clear(p->tx_idx, DMA_ALL);
    p->u->CR3 &= ~USART_CR3_DMAT;
    p->u->CR1 &= ~USART_CR1_TCIE;
    // What the USART still holds goes out in a few character times.
    for (uint64_t t = now_us(); !(p->u->ISR & USART_ISR_TC) && now_us() - t < 4u * p->char_us + 100u;) {
    }
}

// Carries out one step of the TX controller; false once the write is over.
static bool apply(port_t *p, const uint8_t *data, sf_tx_step_t s)
{
    switch (s.action) {
    case SF_TX_START:
        memcpy(s_uart_txbuf[p - s_ports], data + s.off, s.n);
        tx_dma_start(p, s.n);
        return true;
    case SF_TX_PAUSE:
        p->u->CR3 &= ~USART_CR3_DMAT;  // the stream stops requesting; what the USART holds finishes
        return true;
    case SF_TX_RESUME:
        p->u->CR3 |= USART_CR3_DMAT;
        return true;
    case SF_TX_ABORT:
        tx_abort(p);
        return false;
    case SF_TX_DONE:
        return false;
    default:
        if (p->txc.draining)
            p->u->CR1 |= USART_CR1_TCIE;  // the last chunk is in: tell us when the last bit has left
        return true;
    }
}

int uart_drv_write(int port, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    port_t *p = &s_ports[port - 1];
    uint64_t start = now_us(), deadline = start + (uint64_t)timeout_ms * 1000u + (uint64_t)len * 2u * p->char_us;
    bool inject = p->inject, injected_off = false, injected_on = false;
    p->inject = false;
    p->tx_flags = 0;
    xSemaphoreTake(p->tx_sem, 0);
    bool go = apply(p, data, sf_tx_begin(&p->txc, len, UART_TXBUF, deadline, p->paused));
    while (go) {
        uint64_t now = now_us();
        uint32_t wait_ms = deadline > now ? (uint32_t)((deadline - now) / 1000u) + 1u : 1u;
        if (inject && wait_ms > 10)
            wait_ms = 10;  // the injected XOFF and XON are timed from here
        xSemaphoreTake(p->tx_sem, pdMS_TO_TICKS(wait_ms));
        taskENTER_CRITICAL();
        uint32_t f = p->tx_flags;
        p->tx_flags = 0;
        taskEXIT_CRITICAL();
        now = now_us();
        if (inject && !injected_off && now - start >= 20000u) {  // bench: an XOFF 20 ms in
            injected_off = true;
            p->paused = true;
            f |= F_FLOW;
        }
        if (inject && injected_off && !injected_on && now - start >= 1020000u) {  // and an XON 1 s later
            injected_on = true;
            p->paused = false;
            f |= F_FLOW;
        }
        if (go && (f & F_DMA_ERR))
            go = apply(p, data, sf_tx_dma_error(&p->txc));
        if (go && (f & F_DMA_DONE))
            go = apply(p, data, sf_tx_chunk_done(&p->txc));
        if (go && (f & F_FLOW))
            go = apply(p, data, p->paused ? sf_tx_xoff(&p->txc) : sf_tx_xon(&p->txc));
        if (go && (f & F_USART_TC))
            go = apply(p, data, sf_tx_usart_tc(&p->txc));
        if (go)
            go = apply(p, data, sf_tx_tick(&p->txc, now));
    }
    if (inject && injected_off && !injected_on)
        p->paused = false;  // the injection never leaves an XOFF behind
    return p->txc.result;
}

void uart_drv_flow(int port, bool paused)
{
    port_t *p = &s_ports[port - 1];
    if (p->paused == paused)
        return;
    p->paused = paused;
    taskENTER_CRITICAL();
    p->tx_flags |= F_FLOW;
    taskEXIT_CRITICAL();
    xSemaphoreGive(p->tx_sem);
}

void uart_drv_inject_xoff(int port)
{
    s_ports[port - 1].inject = true;
}

void uart_drv_counters(int port, uint32_t *errors, uint32_t *lost)
{
    *errors = s_ports[port - 1].errors;
    *lost = s_ports[port - 1].lost;
}
