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
#include "inet.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip4.h"
#include "lwip/raw.h"
#include "lwip/timeouts.h"
#include "lwip/priv/tcp_priv.h"  // tcp_active_pcbs: the aborts on a route change (Plan 10 ruling 6)
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

static void set_dns(int i, uint32_t a)
{
    ip_addr_t x;
    ip_addr_set_ip4_u32_val(x, lwip_htonl(a));
    dns_setserver((u8_t)i, &x);
}

// ---- the links (Plan 10): the default route and the DNS follow network.priority --------------------------
// Everything below runs in the tcpip thread: DHCP, the ETH netif's callbacks, PPP's status callback.

static net_dns_t s_dns;           // each link's servers, from their own sources (ruling 5)
static struct netif *s_cell;      // the PPP netif, once PPP is up
static bool s_cell_up;
static net_route_t s_route = NET_ROUTE_NONE;

static bool eth_ok(void) { return s_added && netif_is_link_up(&s_netif) && ip4_addr_get_u32(netif_ip4_addr(&s_netif)) != 0; }

static void dns_apply(void)
{
    uint32_t d[2];
    netcfg_dns(&s_dns, s_route, d);
    set_dns(0, d[0]);
    set_dns(1, d[1]);
}

/** Ruling 6: every active TCP connection whose local address is not on the new default netif is aborted; its
 *  socket fails, and its service dials again over the new link. */
static void abort_others(struct netif *keep)
{
    const ip4_addr_t *a = netif_ip4_addr(keep);
    int n = 0;
    for (struct tcp_pcb *pcb = tcp_active_pcbs, *next; pcb; pcb = next) {
        next = pcb->next;
        if (!ip4_addr_eq(ip_2_ip4(&pcb->local_ip), a)) {
            tcp_abort(pcb);
            n++;
        }
    }
    if (n)
        blog("net: %d connections on the old link closed\r\n", n);
}

// ---- the Internet check per link (ruling 14): ICMP echo to 8.8.8.8 and 1.1.1.1 every 30 s, on each link --------

#define PROBE_MS 30000
#define PROBE_WAIT_MS 2000
#define PROBE_ID INET_PROBE_ID
static struct raw_pcb *s_probe;
static inet_t s_inet[2];          // 0 Ethernet, 1 the cell
static bool s_probed[2], s_replied[2];
static uint16_t s_probe_round;
static const uint32_t k_targets[2] = {INET_TARGET0, INET_TARGET1};

static u8_t probe_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    (void)arg;
    (void)pcb;
    (void)addr;
    // Only a reply for this round, from the target its sequence names, on the link it names, with a good checksum
    // counts (inet_reply_ok, Codex code review): a forged packet from the LAN cannot move the route.
    struct ip_hdr ih;
    uint8_t icmp[64];
    if (pbuf_copy_partial(p, &ih, sizeof ih, 0) != sizeof ih)
        return 0;
    u16_t hl = (u16_t)IPH_HL_BYTES(&ih), n = (u16_t)(p->tot_len - hl);
    if (n > sizeof icmp || pbuf_copy_partial(p, icmp, n, hl) != n || icmp[0] != ICMP_ER)
        return 0;  // not an echo reply of ours: lwIP goes on with it
    struct netif *in = ip_current_input_netif();
    int in_link = in == &s_netif ? 0 : (s_cell && in == s_cell) ? 1 : -1, link;
    uint32_t src = lwip_ntohl(ip4_addr_get_u32(ip4_current_src_addr()));
    if (!inet_reply_ok(icmp, n, src, in_link, s_probe_round, &link))
        return 0;
    s_replied[link] = true;
    pbuf_free(p);
    return 1;
}

static void probe_send(int link, struct netif *nif, int target)
{
    struct pbuf *p = pbuf_alloc(PBUF_IP, (u16_t)(sizeof(struct icmp_echo_hdr) + 8), PBUF_RAM);
    if (!p)
        return;
    struct icmp_echo_hdr *e = p->payload;
    memset(e, 0, p->len);
    e->type = ICMP_ECHO;
    e->id = lwip_htons(PROBE_ID);
    e->seqno = lwip_htons((u16_t)((s_probe_round & 0x3FFF) << 2 | link << 1 | target));
    e->chksum = inet_chksum(e, p->len);
    ip_addr_t dst;
    ip_addr_set_ip4_u32_val(dst, lwip_htonl(k_targets[target]));
    raw_sendto_if_src(s_probe, p, &dst, nif, netif_ip_addr4(nif));  // through this link, from its own address
    pbuf_free(p);
}

static void route_check(void);

static void probe_judge(void *arg)
{
    (void)arg;
    for (int l = 0; l < 2; l++) {
        bool was = s_inet[l].ok;
        if (s_probed[l])
            inet_result(&s_inet[l], s_replied[l]);
        else
            inet_init(&s_inet[l]);  // no address: taken to have Internet again once it gets one
        if (was != s_inet[l].ok)
            blog("net: %s %s Internet\r\n", l ? "LTE" : "Ethernet", s_inet[l].ok ? "has" : "has no");
    }
    route_check();
}

static void probe_round(void *arg)
{
    (void)arg;
    s_probe_round++;
    s_probed[0] = eth_ok();
    s_probed[1] = s_cell_up && s_cell;
    s_replied[0] = s_replied[1] = false;
    for (int t = 0; t < 2; t++) {
        if (s_probed[0])
            probe_send(0, &s_netif, t);
        if (s_probed[1])
            probe_send(1, s_cell, t);
    }
    sys_timeout(PROBE_WAIT_MS, probe_judge, NULL);
    sys_timeout(PROBE_MS, probe_round, NULL);
}

static void probe_start(void)
{
    inet_init(&s_inet[0]);
    inet_init(&s_inet[1]);
    s_probe = raw_new(IP_PROTO_ICMP);
    if (!s_probe)
        return;
    raw_recv(s_probe, probe_recv, NULL);
    sys_timeout(PROBE_MS, probe_round, NULL);
}

void netmgr_internet(bool *eth, bool *cell)
{
    LOCK_TCPIP_CORE();
    *eth = eth_ok() && s_inet[0].ok;
    *cell = s_cell_up && s_inet[1].ok;
    UNLOCK_TCPIP_CORE();
}

static void route_check(void)
{
    net_route_t r = netcfg_pick_inet(s_cfg.priority, eth_ok(), s_inet[0].ok, s_cell_up, s_inet[1].ok);
    struct netif *want = r == NET_ROUTE_ETH ? &s_netif : r == NET_ROUTE_CELL ? s_cell : NULL;
    if (r != s_route) {
        s_route = r;
        blog("net: default route via %s\r\n", r == NET_ROUTE_ETH ? "Ethernet" : r == NET_ROUTE_CELL ? "LTE" : "none");
        if (want) {
            netif_set_default(want);
            abort_others(want);
        }
    }
    dns_apply();  // after every event, the route unchanged included (Codex plan r1, r2)
}

static void eth_changed(struct netif *netif)
{
    (void)netif;
    route_check();
}

// The patched dhcp.c: each accepted ACK's whole DNS list.
void netmgr_dhcp_dns(struct netif *netif, int n, const struct ip4_addr *servers)
{
    (void)netif;
    for (int i = 0; i < 2; i++)
        s_dns.eth_lease[i] = i < n ? lwip_ntohl(ip4_addr_get_u32(&servers[i])) : 0;
    dns_apply();
}

void netmgr_cell(struct netif *nif, bool up, const uint32_t dns[2])
{
    s_cell = nif;
    s_cell_up = up;
    s_dns.cell_peer[0] = up ? dns[0] : 0;
    s_dns.cell_peer[1] = up ? dns[1] : 0;
    route_check();
}

const char *netmgr_route_name(void)
{
    return s_route == NET_ROUTE_ETH ? "ETH" : s_route == NET_ROUTE_CELL ? "LTE" : "NONE";
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
    s_dns.eth_manual = s_cfg.dns_manual;
    s_dns.eth_cfg[0] = s_cfg.dns1, s_dns.eth_cfg[1] = s_cfg.dns2;
    s_dns.cell_manual = s_cfg.cell_dns_manual;
    s_dns.cell_cfg[0] = s_cfg.cell_dns1, s_dns.cell_cfg[1] = s_cfg.cell_dns2;
    netif_set_status_callback(&s_netif, eth_changed);  // an address (DHCP, static)
    netif_set_link_callback(&s_netif, eth_changed);    // the link (eth_drv's poll)
    netif_set_default(&s_netif);
    probe_start();
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

bool netmgr_online(void)
{
    eth_status_t e;
    eth_drv_status(&e);
    LOCK_TCPIP_CORE();
    bool bound = s_added && ip4_addr_get_u32(netif_ip4_addr(&s_netif)) != 0, cell = s_cell_up;
    UNLOCK_TCPIP_CORE();
    return (e.up && bound) || cell;  // ruling 7: either link with an address
}

int netmgr_netconn_free(void)
{
    const struct stats_mem *m = lwip_stats.memp[MEMP_NETCONN];
    return m ? (int)m->avail - (int)m->used : 0;
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
