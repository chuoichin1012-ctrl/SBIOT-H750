// Host test of the DNS servers under DHCP (spec §7.1, §7.2; patch 2 of third_party/patches/lwip-2.2.1-sbiot.patch,
// Plan 10 ruling 5): lwIP's own DHCP client, fed with DHCP replies built here. Each accepted ACK hands its whole DNS
// list (0, 1 or 2 servers) to the hook once, and lwIP's global servers are never written by DHCP.
// make -C stm32/firmware/test/host
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

static int s_sent;      // frames lwIP sent
static int s_calls;     // hook calls
static int s_last_n;    // the last call's count
static uint32_t s_last[2];

void test_dhcp_dns(struct netif *netif, int n, const ip4_addr_t *servers)
{
    (void)netif;
    s_calls++;
    s_last_n = n;
    s_last[0] = n > 0 ? lwip_ntohl(ip4_addr_get_u32(&servers[0])) : 0;
    s_last[1] = n > 1 ? lwip_ntohl(ip4_addr_get_u32(&servers[1])) : 0;
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

// An Ethernet frame holding a DHCP reply from SERVER: OFFER or ACK of LEASED, with ndns DNS servers (0: no option 6).
static size_t reply(uint8_t *f, uint8_t type, uint32_t xid, int ndns, uint32_t dns1, uint32_t dns2, shape_t shape)
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
    if (ndns == 1)
        *o++ = 6, *o++ = 4, put32(o, dns1), o += 4;
    else if (ndns == 2)
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
    feed(nif, f, reply(f, DHCP_OFFER, xid_of(nif), 2, dns1, dns2, GOOD));
    feed(nif, f, reply(f, DHCP_ACK, xid_of(nif), 2, dns1, dns2, GOOD));
    CHECK(state_of(nif) == DHCP_STATE_BOUND && lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(nif))) == LEASED,
          "bound to the leased address (state %u)", state_of(nif));
}

static void renew(struct netif *nif, int ndns, uint32_t dns1, uint32_t dns2, shape_t shape)
{
    uint8_t f[600];
    CHECK(dhcp_renew(nif) == ERR_OK, "dhcp_renew");
    feed(nif, f, reply(f, DHCP_ACK, xid_of(nif), ndns, dns1, dns2, shape));
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

static void test_hook(void)
{
    struct netif nif;
    set_dns(0x0A000001u, 0x0A000002u);  // what netmgr wrote: DHCP must leave them
    fresh(&nif);
    lease(&nif, 0x08080808u, 0x01010101u);
    CHECK(s_calls == 1 && s_last_n == 2 && s_last[0] == 0x08080808u && s_last[1] == 0x01010101u, "the ACK's two servers, once (%d calls)",
          s_calls);
    CHECK(dns_now(0) == 0x0A000001u && dns_now(1) == 0x0A000002u, "and never lwIP's global servers (%08x %08x)",
          (unsigned)dns_now(0), (unsigned)dns_now(1));
    renew(&nif, 2, 0x09090909u, 0x04020202u, GOOD);
    CHECK(state_of(&nif) == DHCP_STATE_BOUND && s_calls == 2 && s_last_n == 2 && s_last[0] == 0x09090909u,
          "a renewal that keeps the address: the hook again");
    renew(&nif, 1, 0x05050505u, 0, GOOD);
    CHECK(s_calls == 3 && s_last_n == 1 && s_last[0] == 0x05050505u && s_last[1] == 0, "a renewal with one server: the list of one");
    renew(&nif, 0, 0, 0, GOOD);
    CHECK(s_calls == 4 && s_last_n == 0, "a renewal without a DNS option: the hook with none (%d)", s_last_n);
    renew(&nif, 2, 0x06060606u, 0x07070707u, BAD_AFTER_DNS);
    CHECK(state_of(&nif) == DHCP_STATE_RENEWING && s_calls == 4, "a malformed ACK is not accepted, and the hook is not called");
    uint8_t f[600];
    feed(&nif, f, reply(f, DHCP_ACK, xid_of(&nif), 2, 0x07070707u, 0x03030303u, GOOD));
    CHECK(state_of(&nif) == DHCP_STATE_BOUND && s_calls == 5 && s_last[0] == 0x07070707u, "then a good ACK is taken");
    CHECK(dns_now(0) == 0x0A000001u && dns_now(1) == 0x0A000002u, "lwIP's servers untouched throughout");
    dhcp_stop(&nif);
    dhcp_cleanup(&nif);
    netif_remove(&nif);
}

int main(void)
{
    lwip_init();
    test_hook();
    CHECK(s_sent > 0, "lwIP sent its requests");
    printf("test_dhcp_dns: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
