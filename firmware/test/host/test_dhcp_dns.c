// Host test of the DNS servers under DHCP (spec §7.1, patch 2 of third_party/patches/lwip-2.2.1-sbiot.patch):
// lwIP's own DHCP client, fed with DHCP replies built here. make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/dns.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/prot/dhcp.h"
#include "netif/ethernet.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static int s_manual;   // the config's dnsMode is manual
static int s_sent;     // frames lwIP sent

int test_dns_from_lease(struct netif *netif)
{
    (void)netif;
    return !s_manual;  // what netmgr answers
}

u32_t sys_now(void)
{
    return 0;
}

static const uint8_t k_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t k_server_mac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
#define SERVER 0xC0A80101u  // 192.168.1.1
#define LEASED 0xC0A80132u  // 192.168.1.50

static err_t linkoutput(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    (void)p;
    s_sent++;
    return ERR_OK;
}

static err_t testif_init(struct netif *netif)
{
    netif->name[0] = 't';
    netif->name[1] = 'e';
    netif->output = etharp_output;
    netif->linkoutput = linkoutput;
    netif->mtu = 1500;
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, k_mac, 6);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    return ERR_OK;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

typedef enum { GOOD, BAD_AFTER_DNS } shape_t;

// An Ethernet frame holding a DHCP reply from SERVER: OFFER or ACK of LEASED, with two DNS servers.
static size_t reply(uint8_t *f, uint8_t type, uint32_t xid, uint32_t dns1, uint32_t dns2, shape_t shape)
{
    memset(f, 0, 600);
    memcpy(f, k_mac, 6);
    memcpy(f + 6, k_server_mac, 6);
    f[12] = 0x08;  // IPv4
    uint8_t *ip = f + 14, *udp = ip + 20, *b = udp + 8;
    b[0] = 2;  // BOOTREPLY
    b[1] = 1;
    b[2] = 6;
    put32(b + 4, xid);
    put32(b + 16, LEASED);  // yiaddr
    put32(b + 20, SERVER);  // siaddr
    memcpy(b + 28, k_mac, 6);
    uint8_t *o = b + 236;
    put32(o, 0x63825363u);  // magic cookie
    o += 4;
    *o++ = 53, *o++ = 1, *o++ = type;
    *o++ = 54, *o++ = 4, put32(o, SERVER), o += 4;
    *o++ = 51, *o++ = 4, put32(o, 3600), o += 4;
    *o++ = 1, *o++ = 4, put32(o, 0xFFFFFF00u), o += 4;
    *o++ = 3, *o++ = 4, put32(o, SERVER), o += 4;
    *o++ = 6, *o++ = 8, put32(o, dns1), put32(o + 4, dns2), o += 8;
    if (shape == BAD_AFTER_DNS) {
        *o++ = 58, *o++ = 200;  // an option running past the end of the packet, and no end marker
    } else {
        *o++ = 255;
    }
    size_t blen = (size_t)(o - b), ulen = 8 + blen, iplen = 20 + ulen;
    ip[0] = 0x45;
    ip[2] = (uint8_t)(iplen >> 8), ip[3] = (uint8_t)iplen;
    ip[8] = 64;
    ip[9] = 17;  // UDP
    put32(ip + 12, SERVER);
    put32(ip + 16, 0xFFFFFFFFu);
    udp[1] = 67, udp[3] = 68;
    udp[4] = (uint8_t)(ulen >> 8), udp[5] = (uint8_t)ulen;
    return 14 + iplen;
}

static void feed(struct netif *nif, const uint8_t *f, size_t len)
{
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    pbuf_take(p, f, (u16_t)len);
    if (nif->input(p, nif) != ERR_OK)
        pbuf_free(p);
}

static uint32_t xid_of(struct netif *nif)
{
    return netif_dhcp_data(nif)->xid;
}

static uint32_t dns_now(int i)
{
    const ip_addr_t *a = dns_getserver((u8_t)i);
    return lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(a)));
}

static uint8_t state_of(struct netif *nif)
{
    return netif_dhcp_data(nif)->state;
}

// DISCOVER, OFFER, REQUEST, ACK with dns1/dns2: the netif bound to LEASED.
static void lease(struct netif *nif, uint32_t dns1, uint32_t dns2)
{
    uint8_t f[600];
    CHECK(dhcp_start(nif) == ERR_OK, "dhcp_start");
    feed(nif, f, reply(f, DHCP_OFFER, xid_of(nif), dns1, dns2, GOOD));
    feed(nif, f, reply(f, DHCP_ACK, xid_of(nif), dns1, dns2, GOOD));
    CHECK(state_of(nif) == DHCP_STATE_BOUND && lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(nif))) == LEASED,
          "bound to the leased address (state %u)", state_of(nif));
}

static void renew(struct netif *nif, uint32_t dns1, uint32_t dns2, shape_t shape)
{
    uint8_t f[600];
    CHECK(dhcp_renew(nif) == ERR_OK, "dhcp_renew");
    feed(nif, f, reply(f, DHCP_ACK, xid_of(nif), dns1, dns2, shape));
}

static void set_dns(uint32_t a, uint32_t b)
{
    ip_addr_t x, y;
    ip_addr_set_ip4_u32_val(x, lwip_htonl(a));
    ip_addr_set_ip4_u32_val(y, lwip_htonl(b));
    dns_setserver(0, &x);
    dns_setserver(1, &y);
}

static struct netif *fresh(struct netif *nif)
{
    netif_add(nif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, NULL, testif_init, ethernet_input);
    netif_set_up(nif);
    netif_set_link_up(nif);
    return nif;
}

static void test_auto(void)
{
    struct netif nif;
    s_manual = 0;
    set_dns(0, 0);
    fresh(&nif);
    lease(&nif, 0x08080808u, 0x01010101u);
    CHECK(dns_now(0) == 0x08080808u && dns_now(1) == 0x01010101u, "auto: the ACK's servers (%08x %08x)",
          (unsigned)dns_now(0), (unsigned)dns_now(1));
    renew(&nif, 0x09090909u, 0x04020202u, GOOD);
    CHECK(state_of(&nif) == DHCP_STATE_BOUND && dns_now(0) == 0x09090909u && dns_now(1) == 0x04020202u,
          "auto: a renewal ACK's servers");
    renew(&nif, 0x05050505u, 0x06060606u, BAD_AFTER_DNS);
    CHECK(state_of(&nif) == DHCP_STATE_RENEWING, "a malformed ACK is not accepted (state %u)", state_of(&nif));
    CHECK(dns_now(0) == 0x09090909u && dns_now(1) == 0x04020202u, "a malformed ACK leaves the servers as they were");
    uint8_t f[600];
    feed(&nif, f, reply(f, DHCP_ACK, xid_of(&nif), 0x07070707u, 0x03030303u, GOOD));
    CHECK(state_of(&nif) == DHCP_STATE_BOUND && dns_now(0) == 0x07070707u, "then a good ACK is taken");
    dhcp_stop(&nif);
    dhcp_cleanup(&nif);
    netif_remove(&nif);
}

static void test_manual(void)
{
    struct netif nif;
    s_manual = 1;
    set_dns(0x0A000001u, 0x0A000002u);  // what netmgr sets before dhcp_start
    fresh(&nif);
    lease(&nif, 0x08080808u, 0x01010101u);
    CHECK(dns_now(0) == 0x0A000001u && dns_now(1) == 0x0A000002u, "manual: the lease keeps the manual DNS (%08x %08x)",
          (unsigned)dns_now(0), (unsigned)dns_now(1));
    renew(&nif, 0x09090909u, 0x04020202u, GOOD);
    CHECK(state_of(&nif) == DHCP_STATE_BOUND && dns_now(0) == 0x0A000001u && dns_now(1) == 0x0A000002u,
          "manual: a renewal keeps it too");
    dhcp_stop(&nif);
    dhcp_cleanup(&nif);
    netif_remove(&nif);
}

int main(void)
{
    lwip_init();
    test_auto();
    test_manual();
    CHECK(s_sent > 0, "lwIP sent its requests");
    printf("test_dhcp_dns: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
