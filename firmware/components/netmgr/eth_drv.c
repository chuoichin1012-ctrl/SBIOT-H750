#include "wdog.h"
#include "eth_drv.h"
#include <stddef.h>
#include <string.h>
#include "FreeRTOS.h"
#include "blog.h"
#include "eth.h"  // heth (CubeMX)
#include "ip101.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "main.h"  // ETH_RESET_Pin
#include "netif/ethernet.h"
#include "semphr.h"
#include "task.h"

#define RX_BUFS 16u          // the pool table (spec §5.6): more than the 4 descriptors + IP reassembly
#define BUF_LEN 1536u        // a whole frame per buffer; a multiple of 4 (RxBuffLen) and of 32 (DMA alignment)
#define TX_BUFS ETH_TX_DESC_CNT
#define RX_PER_WAKE 8        // spec §6.1: then vTaskDelay(1), so a flood cannot starve lower tasks
#define LINK_POLL_MS 1000u   // spec §7.1
#define TX_WAIT_MS 20u
#define ETH_RX_STACK_WORDS 512  // 2 KB (spec §6.1)
#define ETH_RX_PRIO 22

// RX: zero-copy (spec §5.4). A buffer goes to a descriptor, then to lwIP as a custom pbuf, and back
// to the free list when lwIP frees it. Our own pool, not LWIP_MEMPOOL: lwIP's pools live in AXI.
typedef struct {
    struct pbuf_custom pc;
    uint8_t buf[BUF_LEN] __attribute__((aligned(32)));
} rx_buf_t;
static rx_buf_t s_eth_rx_bufs[RX_BUFS] __attribute__((section(".sram1_bss"), aligned(32)));
static uint8_t s_rx_free[RX_BUFS];
static volatile unsigned s_rx_nfree;
static volatile bool s_rx_starved;  // the HAL found the pool dry: refill once a buffer comes back

// TX: copies (spec §5.4): a pbuf may point into DTCM or AXI. A buffer is reused only once the HAL has
// handed it back (HAL_ETH_TxFreeCallback, from HAL_ETH_ReleaseTxPacket).
static uint8_t s_eth_tx_bufs[TX_BUFS][BUF_LEN] __attribute__((section(".sram1_bss"), aligned(32)));
static uint32_t s_tx_busy;  // a bit per buffer; touched only under the lwIP core lock
static ETH_TxPacketConfigTypeDef s_txc;

// CubeMX's descriptors (eth.c), placed in SRAM1 by app.ld.
extern ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT], DMATxDscrTab[ETH_TX_DESC_CNT];

static struct netif *s_netif;
static ip101_t s_phy;
static bool s_phy_found;
static SemaphoreHandle_t s_rx_sem;
static StaticSemaphore_t s_rx_sem_mem;
static StaticTask_t s_rx_tcb;
static StackType_t s_rx_stack[ETH_RX_STACK_WORDS];
static eth_status_t s_st = {.phy_addr = -1};
static volatile int s_power_req;  // 1 down, 2 up: done by eth_rx, the only task on MDIO

// --- RX pool -----------------------------------------------------------------------------------------

static void rx_free(struct pbuf *p)
{
    rx_buf_t *b = (rx_buf_t *)p;  // pc is the first member
    UBaseType_t s = taskENTER_CRITICAL_FROM_ISR();
    s_rx_free[s_rx_nfree++] = (uint8_t)(b - s_eth_rx_bufs);
    bool wake = s_rx_starved;
    s_rx_starved = false;
    taskEXIT_CRITICAL_FROM_ISR(s);
    if (wake)
        xSemaphoreGive(s_rx_sem);  // eth_rx rebuilds the descriptors the HAL left empty
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    rx_buf_t *b = NULL;
    UBaseType_t s = taskENTER_CRITICAL_FROM_ISR();
    if (s_rx_nfree)
        b = &s_eth_rx_bufs[s_rx_free[--s_rx_nfree]];
    else
        s_rx_starved = true;
    taskEXIT_CRITICAL_FROM_ISR(s);
    if (!b) {
        s_st.rx_starved++;
        *buff = NULL;
        return;
    }
    b->pc.custom_free_function = rx_free;
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, &b->pc, b->buf, BUF_LEN);
    *buff = b->buf;
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
    // As ST's template; no cache invalidation: SRAM1 is non-cacheable (MPU region 2).
    struct pbuf **start = (struct pbuf **)pStart, **end = (struct pbuf **)pEnd;
    struct pbuf *p = (struct pbuf *)(buff - offsetof(rx_buf_t, buf));
    __DMB();
    p->next = NULL;
    p->tot_len = 0;
    p->len = Length;
    if (!*start)
        *start = p;
    else
        (*end)->next = p;
    *end = p;
    for (p = *start; p; p = p->next)
        p->tot_len = (u16_t)(p->tot_len + Length);
}

void HAL_ETH_TxFreeCallback(uint32_t *buff)
{
    uint32_t i = (uint32_t)((uint8_t *)buff - &s_eth_tx_bufs[0][0]) / BUF_LEN;
    if (i < TX_BUFS)
        s_tx_busy &= ~(1u << i);
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *h)
{
    (void)h;
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(s_rx_sem, &woke);
    portYIELD_FROM_ISR(woke);
}

void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *h)
{
    // A buffer-unavailable stop: eth_rx refills the descriptors, which restarts the RX DMA.
    if (HAL_ETH_GetDMAError(h) & ETH_DMACSR_RBU) {
        BaseType_t woke = pdFALSE;
        xSemaphoreGiveFromISR(s_rx_sem, &woke);
        portYIELD_FROM_ISR(woke);
    }
}

// --- TX ----------------------------------------------------------------------------------------------

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    if (p->tot_len > BUF_LEN)
        return ERR_BUF;
    int slot = -1;
    for (uint32_t t = 0; slot < 0; t++) {
        HAL_ETH_ReleaseTxPacket(&heth);
        for (int i = 0; i < (int)TX_BUFS && slot < 0; i++)
            if (!(s_tx_busy & (1u << i)))
                slot = i;
        if (slot >= 0)
            break;
        if (t >= TX_WAIT_MS) {
            s_st.tx_busy_drops++;
            return ERR_MEM;  // transient (spec §5.6): TCP sends again
        }
        vTaskDelay(1);
    }
    pbuf_copy_partial(p, s_eth_tx_bufs[slot], p->tot_len, 0);
    ETH_BufferTypeDef b = {.buffer = s_eth_tx_bufs[slot], .len = p->tot_len, .next = NULL};
    s_txc.Length = p->tot_len;
    s_txc.TxBuffer = &b;
    s_txc.pData = s_eth_tx_bufs[slot];
    s_tx_busy |= 1u << slot;
    __DMB();
    if (HAL_ETH_Transmit_IT(&heth, &s_txc) != HAL_OK) {
        s_tx_busy &= ~(1u << slot);
        return ERR_IF;
    }
    return ERR_OK;
}

// --- PHY and link ------------------------------------------------------------------------------------

static int mdio_read(void *ctx, uint32_t addr, uint32_t reg, uint32_t *val)
{
    (void)ctx;
    return HAL_ETH_ReadPHYRegister(&heth, addr, reg, val) == HAL_OK ? 0 : -1;
}

static int mdio_write(void *ctx, uint32_t addr, uint32_t reg, uint32_t val)
{
    (void)ctx;
    return HAL_ETH_WritePHYRegister(&heth, addr, reg, val) == HAL_OK ? 0 : -1;
}

// In eth_rx. The ETH start and stop, and the netif's link, change under the core lock, so that they
// never run beside low_level_output.
// Starts the MAC at the speed and duplex of `ls`; true when it runs.
static bool mac_start(ip101_status_t ls)
{
    ETH_MACConfigTypeDef mc;
    HAL_ETH_GetMACConfig(&heth, &mc);
    mc.Speed = ls == IP101_100_FULL || ls == IP101_100_HALF ? ETH_SPEED_100M : ETH_SPEED_10M;
    mc.DuplexMode = ls == IP101_100_FULL || ls == IP101_10_FULL ? ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;
    HAL_ETH_SetMACConfig(&heth, &mc);
    if (HAL_ETH_Start_IT(&heth) != HAL_OK)
        return false;
    s_st.speed = mc.Speed == ETH_SPEED_100M ? 100 : 10;
    s_st.full_duplex = mc.DuplexMode == ETH_FULLDUPLEX_MODE;
    return true;
}

// In eth_rx. The ETH start and stop, and the netif's link, change under the core lock, so that they
// never run beside low_level_output. s_applied is the state the MAC runs at (LINK_DOWN: stopped).
static void link_poll(void)
{
    static ip101_status_t s_applied = IP101_LINK_DOWN;
    ip101_status_t ls = ip101_link(&s_phy);
    ip101_action_t act = ip101_action(s_applied, ls);
    if (act == IP101_KEEP)
        return;
    LOCK_TCPIP_CORE();
    if (act == IP101_STOP || act == IP101_RECONFIG) {
        HAL_ETH_Stop_IT(&heth);
        s_applied = IP101_LINK_DOWN;
    }
    if (act == IP101_START || act == IP101_RECONFIG) {
        if (mac_start(ls))
            s_applied = ls;
    }
    bool up = s_applied != IP101_LINK_DOWN;
    if (up != s_st.up) {  // a reconfiguration that worked keeps the netif's link up
        if (up)
            netif_set_link_up(s_netif);
        else
            netif_set_link_down(s_netif);
    }
    s_st.up = up;
    if (!up)
        s_st.speed = 0;
    UNLOCK_TCPIP_CORE();
    s_st.link_changes++;
    uint32_t lp = 0;
    if (up && mdio_read(NULL, s_phy.addr, PHY_ANLPAR, &lp))
        lp = 0xFFFFFFFFu;
    if (up)
        blog("net: link %s, %d Mbit %s duplex (partner %04lx)\r\n", act == IP101_RECONFIG ? "now" : "up", s_st.speed,
             s_st.full_duplex ? "full" : "half", (unsigned long)lp);
    else
        blog("net: link down\r\n");
}

static void eth_rx_task(void *arg)
{
    (void)arg;
    TickType_t last_poll = xTaskGetTickCount();
    link_poll();
    int hb = wdog_hb_add("eth_rx", 10000);  // Plan 11a task 0b
    for (;;) {
        wdog_hb_beat(hb);
        xSemaphoreTake(s_rx_sem, pdMS_TO_TICKS(LINK_POLL_MS));
        int req = s_power_req;
        if (req) {
            s_power_req = 0;
            ip101_power(&s_phy, req == 1);
        }
        if (req || xTaskGetTickCount() - last_poll >= pdMS_TO_TICKS(LINK_POLL_MS)) {
            last_poll = xTaskGetTickCount();
            link_poll();
        }
        if (!s_st.up)
            continue;
        int n = 0;
        for (; n < RX_PER_WAKE; n++) {
            struct pbuf *p = NULL;
            // Also when the pool was dry: a ReadData without a frame rebuilds the empty descriptors.
            if (HAL_ETH_ReadData(&heth, (void **)&p) != HAL_OK || !p)
                break;
            if (s_netif->input(p, s_netif) != ERR_OK)
                pbuf_free(p);
        }
        if (n == RX_PER_WAKE) {
            vTaskDelay(1);  // more may be waiting: a real block first, then look again
            xSemaphoreGive(s_rx_sem);
        }
    }
}

// --- netif -------------------------------------------------------------------------------------------

err_t eth_drv_netif_init(struct netif *netif)
{
    static uint8_t mac[6];
    memcpy(mac, netif->state, 6);
    s_netif = netif;
    s_rx_nfree = 0;
    for (unsigned i = 0; i < RX_BUFS; i++)
        s_rx_free[s_rx_nfree++] = (uint8_t)i;
    s_rx_sem = xSemaphoreCreateBinaryStatic(&s_rx_sem_mem);

    // The PHY first: it drives REF_CLK, which the ETH's DMA reset (in HAL_ETH_Init) waits for.
    HAL_GPIO_WritePin(ETH_RESET_GPIO_Port, ETH_RESET_Pin, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(10));
    HAL_GPIO_WritePin(ETH_RESET_GPIO_Port, ETH_RESET_Pin, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(50));

    heth.Instance = ETH;
    heth.Init.MACAddr = mac;
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    heth.Init.TxDesc = DMATxDscrTab;
    heth.Init.RxDesc = DMARxDscrTab;
    heth.Init.RxBuffLen = BUF_LEN;
    if (HAL_ETH_Init(&heth) != HAL_OK) {
        blog("net: ETH init FAILED (%08lx): no REF_CLK from the PHY?\r\n", (unsigned long)heth.ErrorCode);
        return ERR_IF;
    }
    // MDIO works now (HAL_ETH_Init enabled the clocks and set the MDC divider).
    ip101_io_t io = {.read = mdio_read, .write = mdio_write};
    ip101_status_t ps = ip101_init(&s_phy, &io);
    if (ps != IP101_OK) {
        blog("net: no PHY on MDIO (%d)\r\n", ps);
        return ERR_IF;
    }
    s_phy_found = true;
    s_st.phy_addr = (int)s_phy.addr;
    s_st.phy_id = s_phy.id;
    blog("net: PHY at %lu, id %08lx\r\n", (unsigned long)s_phy.addr, (unsigned long)s_phy.id);

    // Every multicast frame reaches lwIP, which keeps only its IGMP groups (the bridge, spec §7.1).
    ETH_MACFilterConfigTypeDef f;
    HAL_ETH_GetMACFilterConfig(&heth, &f);
    f.PassAllMulticast = ENABLE;
    HAL_ETH_SetMACFilterConfig(&heth, &f);

    // Checksums in software (lwipopts.h): CRC and padding only.
    memset(&s_txc, 0, sizeof s_txc);
    s_txc.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
    s_txc.CRCPadCtrl = ETH_CRC_PAD_INSERT;

    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, mac, 6);
    netif->mtu = 1500;
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    netif->name[0] = 'e';
    netif->name[1] = 't';
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    xTaskCreateStatic(eth_rx_task, "eth_rx", ETH_RX_STACK_WORDS, NULL, ETH_RX_PRIO, s_rx_stack, &s_rx_tcb);
    return ERR_OK;
}

void eth_drv_status(eth_status_t *out)
{
    *out = s_st;
    out->rx_in_use = RX_BUFS - s_rx_nfree;
}

int eth_drv_phy_power(bool down)
{
    if (!s_phy_found)
        return -1;
    s_power_req = down ? 1 : 2;
    xSemaphoreGive(s_rx_sem);
    return 0;
}
