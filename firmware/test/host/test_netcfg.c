// Host tests for the Ethernet settings (spec §7.1): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "inet.h"
#include "netcfg.h"
#include "sha256.h"

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

static net_cfg_t from(const char *network, const char *system)
{
    cJSON *n = network ? cJSON_Parse(network) : NULL;
    cJSON *s = system ? cJSON_Parse(system) : NULL;
    net_cfg_t c;
    memset(&c, 0xA5, sizeof c);  // every field must be written
    netcfg_from_json(n, s, &c);
    cJSON_Delete(n);
    cJSON_Delete(s);
    return c;
}

#define STATIC_OK "\"ip\":\"192.168.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"192.168.0.1\""
#define DNS "\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\""

static void test_modes(void)
{
    net_cfg_t c = from("{\"ipMode\":1," STATIC_OK ",\"dnsMode\":0," DNS "}", "{\"hostname\":\"gw-a\"}");
    CHECK(!c.dhcp && !c.static_incomplete && c.ip == 0xC0A80007u && c.mask == 0xFFFFFF00u && c.gw == 0xC0A80001u,
          "static complete");
    CHECK(c.dns_manual && c.dns1 == 0x08080808u && c.dns2 == 0x01010101u, "static: the manual DNS, whatever dnsMode says");
    CHECK(strcmp(c.hostname, "gw-a") == 0, "hostname");

    c = from("{\"ipMode\":1,\"ip\":\"192.168.0.7\",\"netmask\":\"255.0.255.0\",\"gateway\":\"192.168.0.1\",\"dnsMode\":0," DNS "}", NULL);
    CHECK(c.dhcp && c.static_incomplete && c.ip == 0 && c.mask == 0 && c.gw == 0 && !c.dns_manual,
          "a non-contiguous mask: DHCP, DNS by dnsMode (auto)");
    c = from("{\"ipMode\":1,\"ip\":\"\",\"netmask\":\"255.255.255.0\",\"gateway\":\"192.168.0.1\",\"dnsMode\":1," DNS "}", NULL);
    CHECK(c.dhcp && c.static_incomplete && c.dns_manual && c.dns1 == 0x08080808u, "an empty ip: DHCP, DNS by dnsMode (manual)");
    c = from("{\"ipMode\":1,\"ip\":\"0.0.0.0\",\"netmask\":\"255.255.255.0\",\"gateway\":\"192.168.0.1\"}", NULL);
    CHECK(c.dhcp && c.static_incomplete, "ip 0.0.0.0 is not an address");
    c = from("{\"ipMode\":1,\"ip\":\"192.168.0.7\",\"netmask\":\"0.0.0.0\",\"gateway\":\"192.168.0.1\"}", NULL);
    CHECK(c.dhcp && c.static_incomplete, "mask 0.0.0.0");
    c = from("{\"ipMode\":1,\"ip\":\"192.168.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"0.0.0.0\"}", NULL);
    CHECK(!c.dhcp && c.gw == 0, "gateway 0.0.0.0: static, no gateway");

    c = from("{\"ipMode\":0," STATIC_OK ",\"dnsMode\":0," DNS "}", NULL);
    CHECK(c.dhcp && !c.static_incomplete && c.ip == 0 && !c.dns_manual && c.dns1 == 0 && c.dns2 == 0, "DHCP, auto DNS");
    c = from("{\"ipMode\":0,\"dnsMode\":1,\"dns1\":\"9.9.9.9\",\"dns2\":\"bad\"}", NULL);
    CHECK(c.dhcp && c.dns_manual && c.dns1 == 0x09090909u && c.dns2 == 0, "DHCP, manual DNS; a bad dns2 is none");
}

static void test_defaults(void)
{
    net_cfg_t c = from(NULL, NULL);
    CHECK(c.dhcp && !c.static_incomplete && !c.dns_manual && strcmp(c.hostname, NETCFG_HOSTNAME_DEFAULT) == 0,
          "no objects: DHCP, auto DNS, the default hostname");
    c = from("{}", "{\"hostname\":\"\"}");
    CHECK(c.dhcp && strcmp(c.hostname, NETCFG_HOSTNAME_DEFAULT) == 0, "empty hostname: the default");
    c = from("{}", "{\"hostname\":\"abcdefghijklmnopqrstuvwxyz\"}");
    CHECK(strcmp(c.hostname, "abcdefghijklmnop") == 0, "a hostname too long is cut at 16");
    c = from("{\"ipMode\":\"1\"," STATIC_OK "}", "{\"hostname\":7}");
    CHECK(c.dhcp && !c.static_incomplete && strcmp(c.hostname, NETCFG_HOSTNAME_DEFAULT) == 0,
          "wrong types read as missing");
}

static void test_ip4(void)
{
    static const struct {
        const char *s;
        int ok;
        uint32_t v;
    } k[] = {
        {"1.2.3.4", 1, 0x01020304u},     {"255.255.255.255", 1, 0xFFFFFFFFu}, {"0.0.0.0", 1, 0},
        {"010.1.1.1", 0, 0}, /* a leading 0 reads octal elsewhere */            {"256.1.1.1", 0, 0},                 {"1.2.3", 0, 0},
        {"1.2.3.4.5", 0, 0},             {" 1.2.3.4", 0, 0},                  {"1.2.3.4 ", 0, 0},
        {"1.2.3.4x", 0, 0},              {"+1.2.3.4", 0, 0},                  {"1..2.3", 0, 0},
        {"", 0, 0},                      {"1.2.3.", 0, 0},                    {"1.2.3.0004", 0, 0},
        {"99999999999.1.1.1", 0, 0},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        uint32_t v = 0xDEADBEEFu;
        bool ok = netcfg_ip4(k[i].s, &v);
        CHECK(ok == k[i].ok && (!ok || v == k[i].v), "ip4 \"%s\": %d %08x", k[i].s, ok, (unsigned)v);
    }
    CHECK(!netcfg_ip4(NULL, &(uint32_t){0}), "NULL");
}

static void test_mac(void)
{
    const uint8_t uid[12] = {0x30, 0x00, 0x47, 0x00, 0x13, 0x51, 0x33, 0x34, 0x37, 0x38, 0x36, 0x31};
    uint8_t mac[6], h[32];
    sha256_t sh;
    sha256_init(&sh);
    sha256_update(&sh, uid, sizeof uid);
    sha256_final(&sh, h);
    netcfg_mac(uid, mac);
    CHECK(mac[0] == 0x02 && memcmp(mac + 1, h, 5) == 0, "02 then SHA-256(UID)[0..4]");
    CHECK((mac[0] & 1) == 0 && (mac[0] & 2) == 2, "unicast, locally administered");
    uint8_t uid2[12], mac2[6];
    memcpy(uid2, uid, 12);
    uid2[11] ^= 1;
    netcfg_mac(uid2, mac2);
    CHECK(memcmp(mac, mac2, 6) != 0, "another UID, another MAC");
}


static void test_cell_fields(void)
{
    net_cfg_t c = from("{\"priority\":2,\"apn\":\"m-wap\",\"apnUser\":\"u\",\"apnPassword\":\"p\",\"apnAuth\":3,"
                       "\"cellDnsMode\":1,\"cellDns1\":\"8.8.4.4\",\"cellDns2\":\"9.9.9.9\"}", NULL);
    CHECK(c.priority == 2 && !strcmp(c.apn, "m-wap") && !strcmp(c.apn_user, "u") && !strcmp(c.apn_pass, "p") && c.apn_auth == 3,
          "priority and the APN");
    CHECK(c.cell_dns_manual && c.cell_dns1 == 0x08080404u && c.cell_dns2 == 0x09090909u, "the manual cell DNS");
    c = from("{\"cellDnsMode\":0,\"cellDns1\":\"8.8.4.4\"}", NULL);
    CHECK(c.priority == 0 && !strcmp(c.apn, "internet") && c.apn_auth == 0 && !c.cell_dns_manual && c.cell_dns1 == 0,
          "defaults: priority 0, APN internet, automatic DNS");
}

static void test_pick(void)
{
    CHECK(netcfg_pick(0, true, true) == NET_ROUTE_ETH && netcfg_pick(0, false, true) == NET_ROUTE_NONE, "0: Ethernet only");
    CHECK(netcfg_pick(1, true, true) == NET_ROUTE_ETH && netcfg_pick(1, false, true) == NET_ROUTE_CELL &&
              netcfg_pick(1, false, false) == NET_ROUTE_NONE,
          "1: Ethernet first");
    CHECK(netcfg_pick(2, true, true) == NET_ROUTE_CELL && netcfg_pick(2, true, false) == NET_ROUTE_ETH &&
              netcfg_pick(2, false, false) == NET_ROUTE_NONE,
          "2: LTE first");
}

static void test_inet(void)
{
    inet_t s;
    inet_init(&s);
    CHECK(s.ok, "Internet at the start");
    inet_result(&s, false);
    inet_result(&s, false);
    CHECK(s.ok, "still Internet after 2 misses");
    inet_result(&s, false);
    CHECK(!s.ok, "no Internet at the 3rd");
    inet_result(&s, false);
    CHECK(!s.ok, "and after more");
    inet_result(&s, true);
    CHECK(s.ok && s.misses == 0, "back after one reply");
    inet_result(&s, false);
    inet_result(&s, true);
    inet_result(&s, false);
    inet_result(&s, false);
    CHECK(s.ok, "misses count in a row only");
}

static void test_pick_inet(void)
{
    // priority 1: Ethernet first
    CHECK(netcfg_pick_inet(1, true, false, true, true) == NET_ROUTE_CELL, "1: Ethernet without Internet, LTE with: LTE");
    CHECK(netcfg_pick_inet(1, true, false, true, false) == NET_ROUTE_ETH, "1: neither has Internet: by address, Ethernet");
    CHECK(netcfg_pick_inet(1, true, false, false, true) == NET_ROUTE_ETH, "1: LTE without an address: Ethernet");
    CHECK(netcfg_pick_inet(1, true, true, true, true) == NET_ROUTE_ETH, "1: both well: Ethernet");
    CHECK(netcfg_pick_inet(1, false, true, true, false) == NET_ROUTE_CELL, "1: Ethernet without an address: LTE, Internet or not");
    // priority 2, mirrored
    CHECK(netcfg_pick_inet(2, true, true, true, false) == NET_ROUTE_ETH, "2: LTE without Internet, Ethernet with: Ethernet");
    CHECK(netcfg_pick_inet(2, true, false, true, false) == NET_ROUTE_CELL, "2: neither: by address, LTE");
    CHECK(netcfg_pick_inet(2, false, true, true, false) == NET_ROUTE_CELL, "2: Ethernet without an address: LTE");
    // priority 0: Ethernet only
    CHECK(netcfg_pick_inet(0, true, false, true, true) == NET_ROUTE_ETH && netcfg_pick_inet(0, false, true, true, true) == NET_ROUTE_NONE,
          "0: Ethernet only");
}

static net_dns_t s_d;
static uint32_t s_globals[2];
static net_route_t s_route;

static void apply(void) { netcfg_dns(&s_d, s_route, s_globals); }  // what dns_apply does after every event

static void test_dns(void)
{
    // manual and automatic, on each link
    net_dns_t d = {.eth_manual = true, .eth_cfg = {1, 2}, .eth_lease = {3, 4}, .cell_manual = false, .cell_cfg = {5, 6}, .cell_peer = {7, 8}};
    uint32_t o[2];
    netcfg_dns(&d, NET_ROUTE_ETH, o);
    CHECK(o[0] == 1 && o[1] == 2, "Ethernet manual: its config");
    netcfg_dns(&d, NET_ROUTE_CELL, o);
    CHECK(o[0] == 7 && o[1] == 8, "cell automatic: PPP's");
    d.eth_manual = false, d.cell_manual = true;
    netcfg_dns(&d, NET_ROUTE_ETH, o);
    CHECK(o[0] == 3 && o[1] == 4, "Ethernet automatic: the lease's");
    netcfg_dns(&d, NET_ROUTE_CELL, o);
    CHECK(o[0] == 5 && o[1] == 6, "cell manual: its config");
    netcfg_dns(&d, NET_ROUTE_NONE, o);
    CHECK(o[0] == 3 && o[1] == 4, "no route: Ethernet's");
    d.eth_manual = false, d.eth_lease[0] = d.eth_lease[1] = 0;
    netcfg_dns(&d, NET_ROUTE_ETH, o);
    CHECK(o[0] == 0 && o[1] == 0, "an empty automatic pair stays empty");

    // the events in both orders, each link preferred (Codex plan r1, r2)
    for (int pref = 1; pref <= 2; pref++) {
        for (int order = 0; order < 2; order++) {
            memset(&s_d, 0, sizeof s_d);
            s_route = NET_ROUTE_NONE;
            for (int step = 0; step < 2; step++) {
                if ((step == 0) == (order == 0)) {  // PPP up with its DNS
                    s_d.cell_peer[0] = 0x0A0A0A0Au, s_d.cell_peer[1] = 0;
                } else {  // a DHCP ACK
                    s_d.eth_lease[0] = 0x08080808u, s_d.eth_lease[1] = 0x01010101u;
                }
                s_route = netcfg_pick(pref, s_d.eth_lease[0] != 0, s_d.cell_peer[0] != 0);
                apply();
            }
            uint32_t want = pref == 1 ? 0x08080808u : 0x0A0A0A0Au;
            CHECK(s_globals[0] == want, "priority %d, order %d: the chosen link's DNS (%08x)", pref, order, (unsigned)s_globals[0]);
        }
    }
    // PPP up without DNS, Ethernet chosen: Ethernet's stay; the cell pair is empty
    memset(&s_d, 0, sizeof s_d);
    s_d.eth_lease[0] = 0x08080808u;
    s_route = NET_ROUTE_ETH;
    apply();
    s_d.cell_peer[0] = s_d.cell_peer[1] = 0;  // IPCP gave none
    apply();
    CHECK(s_globals[0] == 0x08080808u, "PPP without DNS: Ethernet's kept");
    netcfg_dns(&s_d, NET_ROUTE_CELL, o);
    CHECK(o[0] == 0 && o[1] == 0, "and the cell's pair is empty, not Ethernet's");
    // PPP down while Ethernet is chosen, both on the same server: PPP's cdns() cleared the global, apply puts it back
    s_d.cell_peer[0] = 0x08080808u;
    apply();
    s_globals[0] = 0;  // cdns(): lwIP removed the server PPP had set, the same as Ethernet's
    s_d.cell_peer[0] = 0;
    apply();
    CHECK(s_globals[0] == 0x08080808u, "PPP down with the same DNS as Ethernet: Ethernet's written again");
}

int main(void)
{
    test_modes();
    test_defaults();
    test_ip4();
    test_mac();
    test_cell_fields();
    test_pick();
    test_dns();
    test_inet();
    test_pick_inet();
    printf("test_netcfg: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
