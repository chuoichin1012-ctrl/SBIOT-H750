// The `disc` task (Plan 12): FinderTool's packets on UDP 48600, Ethernet only, every reply broadcast.
// Packet logic is disc_core's; here the socket, the store and the restart (spec §2.3-§2.4).
#include "disc.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/sockets.h"
#include "board_rng.h"
#include "disc_core.h"
#include "netmgr.h"
#include "store.h"
#include "syslog.h"
#include "sysmgr.h"
#include "wdog.h"

#define DISC_STACK_WORDS 1536  // 6 KB: cJSON of a 512-byte packet, store_patch's print, the replies
#define DISC_PRIO 5            // as the web, the other config writer

#define AXI_BSS __attribute__((section(".axi_bss")))  // DTCM is at its budget (Plan 12 ruling)

_Static_assert(LWIP_IP_ACCEPT_UDP_PORT(PP_HTONS(DISC_PORT)), "lwipopts.h takes DISC_PORT while there is no address");

static StaticTask_t s_tcb;
static StackType_t s_stack[DISC_STACK_WORDS] AXI_BSS;
static int s_fd = -1;  // bound in disc_start, by init, before services_start: 48600 is ours before a bridge binds
static char s_hostname[17];
static unsigned s_web;
static char s_mac[18];

static int open_socket(void)
{
    char ifname[8] = "";
    if (!netmgr_eth_ifname(ifname))
        return -1;  // the netif is not there yet: tried again in 1 s
    int fd = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
        return -1;
    int one = 1;
    struct timeval tv = {.tv_sec = 1};
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
    struct sockaddr_in me = {.sin_family = AF_INET, .sin_port = lwip_htons(DISC_PORT)};
    // Bound to the Ethernet netif before bind: replies leave by Ethernet even with LTE as the default route.
    if (lwip_setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one) != 0 ||
        lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) != 0 ||
        lwip_setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof ifr) != 0 ||
        lwip_bind(fd, (struct sockaddr *)&me, sizeof me) != 0) {
        lwip_close(fd);
        return -2;
    }
    return fd;
}

static void reply(int fd, uint16_t port_be, const char *text, int n)
{
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = port_be, .sin_addr.s_addr = 0xFFFFFFFFu};
    if (n > 0)
        lwip_sendto(fd, text, (size_t)n, 0, (struct sockaddr *)&to, sizeof to);
}

static void found(int fd, uint16_t port, const char *id, const net_status_t *st)
{
    disc_info_t in = {.mac = s_mac, .sn = sysmgr_serial(), .model = "SBIOT-H750", .fw = APP_VERSION, .hostname = s_hostname,
                      .web = s_web, .boot = store_boot_count(), .eth_ip = st->ip, .eth_mask = st->mask, .eth_gw = st->gw};
    char *saved = store_get_json("network");  // the saved fields, also after a Web save not yet applied
    cJSON *net = saved ? cJSON_Parse(saved) : NULL;
    disc_net_from(net, &in.saved);
    cJSON_Delete(net);
    free(saved);
    vTaskDelay(pdMS_TO_TICKS(board_rand32() % 201u));  // 0-200 ms: replies of many gateways spread out
    char out[DISC_MAX + 1];
    reply(fd, port, out, disc_found(&in, id, out, sizeof out));
}

static void ack(int fd, uint16_t port, const char *id, const char *err)
{
    char out[128];
    reply(fd, port, out, disc_ack(id, s_mac, err, out, sizeof out));
}

static void handle(int fd, const char *buf, int n, uint16_t port)
{
    net_status_t st;
    netmgr_status(&st);
    // No address on Ethernet (DHCP with no server): replies still go, from 0.0.0.0, so FinderTool can set one
    // (the user, 2026-10-17; lwipopts.h LWIP_IP_ACCEPT_UDP_PORT lets the request in).
    disc_req_t q;
    char err[160];
    switch (disc_parse(buf, (size_t)n, s_mac, &q)) {
    case DISC_IGNORE:
        return;
    case DISC_SEARCH:
        found(fd, port, q.id, &st);
        return;
    case DISC_BAD:
        ack(fd, port, q.id, "bad");
        return;
    case DISC_SET: {
        cJSON *f = disc_net_json(&q.net);
        store_err_t e = f ? store_patch("network", f, err, sizeof err) : STORE_ENOMEM;
        cJSON_Delete(f);
        SLOG_W("disc", "network set by FinderTool: %s %s (%s)", q.net.ip_mode ? "static" : "DHCP", q.net.ip,
               e == STORE_OK ? "saved, restarting" : "not saved");
        ack(fd, port, q.id, e == STORE_OK ? NULL : "store");
        if (e == STORE_OK)
            sysmgr_restart_later(1000);
        return;
    }
    case DISC_RESTART:
        SLOG_W("disc", "restart requested by FinderTool");
        ack(fd, port, q.id, NULL);
        sysmgr_restart_later(1000);
        return;
    case DISC_FACTORY: {
        SLOG_W("disc", "factory reset requested by FinderTool");
        store_err_t e = store_factory_reset();  // restarts the gateway itself in 1.5 s, whatever the result
        ack(fd, port, q.id, e == STORE_OK ? NULL : "store");
        return;
    }
    }
}

static bool s_told;

static int try_open(void)
{
    int fd = open_socket();
    if (fd == -2 && !s_told) {
        SLOG_E("disc", "cannot bind UDP port %u on Ethernet: FinderTool will not find this gateway", DISC_PORT);
        s_told = true;
    }
    return fd;
}

static void disc_task(void *arg)
{
    (void)arg;
    int hb = wdog_hb_add("disc", 30000);
    int fd = s_fd;
    static char buf[DISC_MAX + 1] AXI_BSS;  // one more than allowed: a datagram that fills it is too long
    for (;;) {
        wdog_hb_beat(hb);
        if (fd < 0 && (fd = try_open()) < 0) {  // the Ethernet was not up at boot
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = lwip_recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);  // 1 s at most (SO_RCVTIMEO)
        if (n < 0)
            continue;  // the timeout
        if (n > 0 && n <= DISC_MAX)
            handle(fd, buf, n, from.sin_port);
        vTaskDelay(1);  // after every datagram, even an ignored or empty one: a flood cannot starve the tasks below (Codex plan r1)
    }
}

void disc_start(const cJSON *sys)
{
    const char *h = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(sys, "hostname"));
    snprintf(s_hostname, sizeof s_hostname, "%s", h && *h ? h : "sbiot-gw");
    const cJSON *w = cJSON_GetObjectItemCaseSensitive(sys, "webPort");
    s_web = cJSON_IsNumber(w) ? (unsigned)w->valueint : 80u;
    net_status_t st;
    netmgr_status(&st);
    snprintf(s_mac, sizeof s_mac, "%02x:%02x:%02x:%02x:%02x:%02x", st.mac[0], st.mac[1], st.mac[2], st.mac[3], st.mac[4], st.mac[5]);
    s_fd = try_open();  // here, synchronously: services_start (the bridges) comes after (Codex plan r1)
    xTaskCreateStatic(disc_task, "disc", DISC_STACK_WORDS, NULL, DISC_PRIO, s_stack, &s_tcb);
}
