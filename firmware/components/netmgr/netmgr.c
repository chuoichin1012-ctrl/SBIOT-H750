#include "netmgr.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "blog.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/memp.h"
#include "lwip/netif.h"
#include "lwip/stats.h"
#include "lwip/tcpip.h"
#include "semphr.h"
#include "task.h"

#define TCPIP_STACK_WORDS (TCPIP_THREAD_STACKSIZE / sizeof(StackType_t))

// lwIP's MEM heap (spec §5.6) in SRAM1, as arch/cc.h says; 64 bytes over MEM_SIZE for mem.c's two
// struct mem and its alignment.
uint8_t lwip_ram_heap[MEM_SIZE + 64] __attribute__((section(".sram1_bss"), aligned(32)));

static net_cfg_t s_cfg;
static uint8_t s_mac[6];
static struct netif s_netif;
static bool s_added;
static int s_result;
static SemaphoreHandle_t s_done;
static StaticSemaphore_t s_done_mem;

// The patched sys_thread_new (third_party/lwip README): lwIP's one thread, tcpip, on a static stack
// in DTCM (spec §5.3, §6.1).
void *netmgr_static_thread(void (*fn)(void *), const char *name, unsigned words, void *arg, int prio)
{
    static StaticTask_t tcb;
    static StackType_t stack[TCPIP_STACK_WORDS];
    static bool used;
    if (used || words > TCPIP_STACK_WORDS)
        return NULL;  // lwIP asserts: there is only one
    used = true;
    return xTaskCreateStatic(fn, name, words, arg, (UBaseType_t)prio, stack, &tcb);
}

void netmgr_assert(const char *msg, const char *file, int line)
{
    blog("net: lwIP assert \"%s\" at %s:%d\r\n", msg, file, line);
    configASSERT(0);
}

int netmgr_dns_from_lease(struct netif *netif)
{
    (void)netif;
    return !s_cfg.dns_manual;  // spec §7.1: a manual DNS stays, renewals included
}

static void set_dns(int i, uint32_t a)
{
    ip_addr_t x;
    ip_addr_set_ip4_u32_val(x, lwip_htonl(a));
    dns_setserver((u8_t)i, &x);
}

// In the tcpip thread (lwIP's core lock is held). Order (Codex r2): netif_add, the static address and
// manual DNS, netif_set_up, then dhcp_start.
static void tcpip_ready(void *arg)
{
    (void)arg;
    ip4_addr_t ip, mask, gw;
    ip4_addr_set_u32(&ip, lwip_htonl(s_cfg.ip));
    ip4_addr_set_u32(&mask, lwip_htonl(s_cfg.mask));
    ip4_addr_set_u32(&gw, lwip_htonl(s_cfg.gw));
    if (!netif_add(&s_netif, &ip, &mask, &gw, s_mac, eth_drv_netif_init, tcpip_input)) {
        s_result = -1;
        xSemaphoreGive(s_done);
        return;
    }
    s_added = true;
    netif_set_hostname(&s_netif, s_cfg.hostname);
    if (s_cfg.dns_manual) {
        set_dns(0, s_cfg.dns1);
        set_dns(1, s_cfg.dns2);
    }
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);
    if (s_cfg.dhcp && dhcp_start(&s_netif) != ERR_OK)
        s_result = -2;
    xSemaphoreGive(s_done);
}

static const char *ip_text(uint32_t a, char *buf)
{
    snprintf(buf, 16, "%lu.%lu.%lu.%lu", (unsigned long)(a >> 24), (unsigned long)(a >> 16 & 255),
             (unsigned long)(a >> 8 & 255), (unsigned long)(a & 255));
    return buf;
}

int netmgr_init(const net_cfg_t *cfg, const uint8_t mac[6])
{
    s_cfg = *cfg;
    memcpy(s_mac, mac, 6);
    char a[16], b[16], c[16], d1[16], d2[16];
    if (cfg->static_incomplete)
        blog("net: static address incomplete, DHCP instead\r\n");
    if (cfg->dhcp)
        blog("net: DHCP, hostname %s, dns %s\r\n", cfg->hostname, cfg->dns_manual ? "manual" : "from the lease");
    else
        blog("net: static %s mask %s gw %s, hostname %s\r\n", ip_text(cfg->ip, a), ip_text(cfg->mask, b),
             ip_text(cfg->gw, c), cfg->hostname);
    if (cfg->dns_manual)
        blog("net: dns %s %s\r\n", ip_text(cfg->dns1, d1), ip_text(cfg->dns2, d2));
    blog("net: mac %02x:%02x:%02x:%02x:%02x:%02x\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    s_done = xSemaphoreCreateBinaryStatic(&s_done_mem);
    tcpip_init(tcpip_ready, NULL);
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(5000)) != pdTRUE)
        s_result = -3;
    if (s_result)
        blog("net: network DOWN (%d)\r\n", s_result);
    return s_result;
}

static void pool(net_pool_t *o, const struct stats_mem *m)
{
    o->used = m ? m->used : 0;
    o->max = m ? m->max : 0;
    o->err = m ? m->err : 0;
    o->size = m ? m->avail : 0;
}

void netmgr_status(net_status_t *out)
{
    memset(out, 0, sizeof *out);
    eth_drv_status(&out->eth);
    memcpy(out->mac, s_mac, 6);
    out->dhcp = s_cfg.dhcp;
    LOCK_TCPIP_CORE();
    out->added = s_added;
    if (s_added) {
        out->ip = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(&s_netif)));
        out->mask = lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(&s_netif)));
        out->gw = lwip_ntohl(ip4_addr_get_u32(netif_ip4_gw(&s_netif)));
        out->bound = out->ip != 0;
    }
    out->dns1 = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(dns_getserver(0))));
    out->dns2 = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(dns_getserver(1))));
    pool(&out->heap, &lwip_stats.mem);
    pool(&out->netconn, lwip_stats.memp[MEMP_NETCONN]);
    pool(&out->tcp_pcb, lwip_stats.memp[MEMP_TCP_PCB]);
    pool(&out->tcp_seg, lwip_stats.memp[MEMP_TCP_SEG]);
    pool(&out->pbuf_pool, lwip_stats.memp[MEMP_PBUF_POOL]);
    pool(&out->udp_pcb, lwip_stats.memp[MEMP_UDP_PCB]);
    UNLOCK_TCPIP_CORE();
}
