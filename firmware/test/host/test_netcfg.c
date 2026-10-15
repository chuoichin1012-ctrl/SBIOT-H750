// Host tests for the Ethernet settings (spec §7.1): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
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

int main(void)
{
    test_modes();
    test_defaults();
    test_ip4();
    test_mac();
    printf("test_netcfg: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
