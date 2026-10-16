// lwIP 2.2.1 settings of the app: the pool table of spec §5.6 (Plan 06a). Regions: the memp pools and
// PBUF_POOL in AXI, the MEM heap in SRAM1 (arch/cc.h); the ETH frame buffers and descriptors are
// eth_drv.c's, in SRAM1.
#pragma once
#include <stdint.h>

// --- OS, threads -----------------------------------------------------------------------------------
#define NO_SYS 0
#define SYS_LIGHTWEIGHT_PROT 1
#define LWIP_TCPIP_CORE_LOCKING 1
#define LWIP_TCPIP_CORE_LOCKING_INPUT 0
#define TCPIP_THREAD_NAME "tcpip"
#define TCPIP_THREAD_STACKSIZE 4096  // spec §6.1, static in DTCM (netmgr_static_thread)
#define TCPIP_THREAD_PRIO 24
#define TCPIP_MBOX_SIZE 32
#define DEFAULT_THREAD_STACKSIZE 1024
#define DEFAULT_THREAD_PRIO 5
#define DEFAULT_TCP_RECVMBOX_SIZE 8
#define DEFAULT_UDP_RECVMBOX_SIZE 8
#define DEFAULT_RAW_RECVMBOX_SIZE 4
#define DEFAULT_ACCEPTMBOX_SIZE 8
// The listen backlog is honoured: past it a SYN waits for the client's retry instead of a pending
// connection being reset when the accept mailbox is full (Plan 08a).
#define TCP_LISTEN_BACKLOG 1
#define LWIP_FREERTOS_CHECK_CORE_LOCKING 0
// lwIP 2.2.1's FreeRTOS port still uses this old name; FreeRTOSConfig.h turns the old names off.
#define portTICK_RATE_MS portTICK_PERIOD_MS
void *netmgr_static_thread(void (*fn)(void *), const char *name, unsigned words, void *arg, int prio);
#define LWIP_FREERTOS_STATIC_THREAD(fn, name, words, arg, prio) netmgr_static_thread((fn), (name), (words), (arg), (prio))

// --- memory: the pool table (spec §5.6) ----------------------------------------------------------
#define MEM_ALIGNMENT 4
#define MEM_SIZE (64 * 1024)
#define MEMP_NUM_NETCONN 64
#define MEMP_NUM_TCP_PCB 56
#define MEMP_NUM_TCP_PCB_LISTEN 8
#define MEMP_NUM_UDP_PCB 12
#define MEMP_NUM_RAW_PCB 2
#define MEMP_NUM_TCP_SEG 128
#define MEMP_NUM_NETBUF 16
#define MEMP_NUM_PBUF 32
#define MEMP_NUM_TCPIP_MSG_API 16
#define MEMP_NUM_TCPIP_MSG_INPKT 32
#define MEMP_NUM_ARP_QUEUE 16
#define MEMP_NUM_IGMP_GROUP 8
#define MEMP_NUM_REASSDATA 4
#define MEMP_NUM_FRAG_PBUF 16
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 4)
#define PBUF_POOL_SIZE 16  // PPPoS input only (Plan 09): ETH RX has its own pool
#define PBUF_POOL_BUFSIZE 512
#define ARP_TABLE_SIZE 16
#define ARP_QUEUEING 1
#define IP_REASSEMBLY 1
#define IP_FRAG 1
#define IP_REASS_MAX_PBUFS 10  // below the 16 ETH RX buffers
#define MEMP_OVERFLOW_CHECK 0
#define MEMP_SANITY_CHECK 0

// --- protocols -------------------------------------------------------------------------------------
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_DHCP 1
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_AUTOIP 0
#define LWIP_IGMP 1  // the bridge's multicast (spec §7.1)
#define LWIP_DNS 1
#define DNS_TABLE_SIZE 4
#define DNS_MAX_SERVERS 2
// DHCP takes the DNS servers of an accepted ACK only when dnsMode is automatic: a manual DNS is never
// overwritten, renewals included (spec §7.1; patch 2 in third_party/patches/lwip-2.2.1-sbiot.patch).
struct netif;
struct ip4_addr;
void netmgr_dhcp_dns(struct netif *netif, int n, const struct ip4_addr *servers);
#define LWIP_HOOK_DHCP_DNS(netif, n, servers) netmgr_dhcp_dns(netif, n, servers)  // patched dhcp.c, Plan 10
#define LWIP_UDP 1
#define LWIP_TCP 1
#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)
#define TCP_QUEUE_OOSEQ 1
#define TCP_OOSEQ_MAX_PBUFS 4
#define LWIP_TCP_KEEPALIVE 1
#define LWIP_RANDOMIZE_INITIAL_LOCAL_PORTS 1
#define SO_REUSE 1
uint32_t board_rand32(void);
#define LWIP_RAND() board_rand32()

// --- checksums in software (TX inserts none in hardware: eth_drv.c) --------------------------------
#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_UDP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_GEN_ICMP 1
#define CHECKSUM_CHECK_IP 1
#define CHECKSUM_CHECK_UDP 1
#define CHECKSUM_CHECK_TCP 1
#define CHECKSUM_CHECK_ICMP 1

// --- netif ------------------------------------------------------------------------------------------
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_SINGLE_NETIF 0
#define LWIP_NETIF_TX_SINGLE_PBUF 0  // eth_drv copies the chain anyway (spec §5.4)
#define PBUF_LINK_HLEN 14
#define ETH_PAD_SIZE 0

// --- APIs (sockets for Mongoose in 06b, netconn) -----------------------------------------------------
#define LWIP_NETCONN 1
#define LWIP_SOCKET 1
#define LWIP_SOCKET_OFFSET 0
#define LWIP_COMPAT_SOCKETS 1
#define LWIP_POSIX_SOCKETS_IO_NAMES 0  // no read/write/close macros over newlib's
#define LWIP_SO_RCVTIMEO 1
#define LWIP_SO_LINGER 1  // a lost bridge client is reset at once, its queued data freed (Plan 08a)
#define LWIP_SO_SNDTIMEO 1
#define LWIP_SO_RCVBUF 0
#define LWIP_TIMEVAL_PRIVATE 0  // newlib's struct timeval
#define LWIP_NETCONN_SEM_PER_THREAD 0
#define LWIP_SOCKET_SELECT 1
#define LWIP_SOCKET_POLL 1

// --- SNTP (sysmgr, spec §10.4): the server by name, the time handed to sysmgr in the lwIP core ---------
#define SNTP_SERVER_DNS 1
#define SNTP_MAX_SERVERS 1
void sysmgr_sntp_set(uint32_t sec, uint32_t us);
#define SNTP_SET_SYSTEM_TIME_US(sec, us) sysmgr_sntp_set((uint32_t)(sec), (uint32_t)(us))

// --- stats: the pool use for netmgr_status (and /api/status.mem, 06b) --------------------------------
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 0
#define MEMP_STATS 1
#define MEM_STATS 1
#define LINK_STATS 1
#define ETHARP_STATS 0
#define IP_STATS 0
#define IPFRAG_STATS 0
#define ICMP_STATS 0
#define IGMP_STATS 0
#define UDP_STATS 0
#define TCP_STATS 0
#define SYS_STATS 0
