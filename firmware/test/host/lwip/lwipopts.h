// lwIP on the host, for test_dhcp_dns: the DHCP client and DNS of the app's lwIP, no OS, no TCP.
#pragma once
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 0
#define LWIP_UDP 1
#define LWIP_RAW 0
#define LWIP_DHCP 1
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_DNS 1
#define DNS_MAX_SERVERS 2  // as app/lwipopts.h
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_NETIF_HOSTNAME 1
#define MEM_ALIGNMENT 8  // 64-bit host
#define MEM_SIZE 16000
#define MEMP_NUM_SYS_TIMEOUT 8
#define CHECKSUM_CHECK_IP 0  // the test's packets carry no checksums
#define CHECKSUM_CHECK_UDP 0
#define LWIP_STATS 0
struct netif;
struct ip4_addr;
void test_dhcp_dns(struct netif *netif, int n, const struct ip4_addr *servers);
#include <stdlib.h>
#define LWIP_RAND() ((u32_t)rand())
#define LWIP_HOOK_DHCP_DNS(netif, n, servers) test_dhcp_dns(netif, n, servers)
