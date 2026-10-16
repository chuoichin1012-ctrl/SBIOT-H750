#include "netcfg.h"
#include <stdio.h>
#include <string.h>
#include "sha256.h"

static int num(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valueint : 0;
}

static const char *str(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}

bool netcfg_ip4(const char *s, uint32_t *out)
{
    if (!s)
        return false;
    uint32_t v = 0;
    for (int part = 0; part < 4; part++) {
        if (part && *s++ != '.')
            return false;
        if (*s < '0' || *s > '9')
            return false;
        if (s[0] == '0' && s[1] >= '0' && s[1] <= '9')
            return false;  // a leading 0: octal to inet_aton, decimal to people
        uint32_t n = 0;
        for (int d = 0; *s >= '0' && *s <= '9'; d++, s++) {
            n = n * 10 + (uint32_t)(*s - '0');
            if (d >= 3 || n > 255)
                return false;
        }
        v = v << 8 | n;
    }
    if (*s)
        return false;
    *out = v;
    return true;
}

static bool mask_ok(uint32_t m)
{
    return m != 0 && (~m & (~m + 1)) == 0;  // ones, then only zeros
}

static uint32_t ip_or_zero(const char *s)
{
    uint32_t v;
    return netcfg_ip4(s, &v) ? v : 0;
}

void netcfg_from_json(const cJSON *network, const cJSON *system, net_cfg_t *out)
{
    memset(out, 0, sizeof *out);
    out->dhcp = true;
    bool manual_dns = num(network, "dnsMode") == 1;
    if (num(network, "ipMode") == 1) {
        uint32_t ip, mask, gw;
        if (netcfg_ip4(str(network, "ip"), &ip) && ip != 0 && netcfg_ip4(str(network, "netmask"), &mask) && mask_ok(mask) &&
            netcfg_ip4(str(network, "gateway"), &gw)) {
            out->dhcp = false;
            out->ip = ip;
            out->mask = mask;
            out->gw = gw;
            manual_dns = true;
        } else {
            out->static_incomplete = true;
        }
    }
    out->dns_manual = manual_dns;
    if (manual_dns) {
        out->dns1 = ip_or_zero(str(network, "dns1"));
        out->dns2 = ip_or_zero(str(network, "dns2"));
    }
    const char *h = str(system, "hostname");
    strncpy(out->hostname, *h ? h : NETCFG_HOSTNAME_DEFAULT, sizeof out->hostname - 1);
    // LTE (Plan 10)
    out->priority = num(network, "priority");
    const char *apn = str(network, "apn");
    strncpy(out->apn, *apn ? apn : "internet", sizeof out->apn - 1);
    strncpy(out->apn_user, str(network, "apnUser"), sizeof out->apn_user - 1);
    strncpy(out->apn_pass, str(network, "apnPassword"), sizeof out->apn_pass - 1);
    out->apn_auth = num(network, "apnAuth");
    out->cell_dns_manual = num(network, "cellDnsMode") == 1;
    if (out->cell_dns_manual) {
        out->cell_dns1 = ip_or_zero(str(network, "cellDns1"));
        out->cell_dns2 = ip_or_zero(str(network, "cellDns2"));
    }
}

net_route_t netcfg_pick(int priority, bool eth_ok, bool cell_ok)
{
    if (priority == 2)
        return cell_ok ? NET_ROUTE_CELL : eth_ok ? NET_ROUTE_ETH : NET_ROUTE_NONE;
    if (priority == 1)
        return eth_ok ? NET_ROUTE_ETH : cell_ok ? NET_ROUTE_CELL : NET_ROUTE_NONE;
    return eth_ok ? NET_ROUTE_ETH : NET_ROUTE_NONE;  // 0: Ethernet only
}

net_route_t netcfg_pick_inet(int priority, bool eth_ok, bool eth_inet, bool cell_ok, bool cell_inet)
{
    if (priority != 1 && priority != 2)
        return netcfg_pick(priority, eth_ok, false);  // 0: Ethernet only, whatever the check says
    // A link without Internet counts only while the other one has no address or no Internet either.
    bool eth_use = eth_ok && (eth_inet || !(cell_ok && cell_inet));
    bool cell_use = cell_ok && (cell_inet || !(eth_ok && eth_inet));
    return netcfg_pick(priority, eth_use, cell_use);
}

void netcfg_dns(const net_dns_t *d, net_route_t route, uint32_t out[2])
{
    const uint32_t *p = route == NET_ROUTE_CELL ? (d->cell_manual ? d->cell_cfg : d->cell_peer)
                                                : (d->eth_manual ? d->eth_cfg : d->eth_lease);
    out[0] = p[0];
    out[1] = p[1];
}

void netcfg_mac(const uint8_t uid[12], uint8_t mac[6])
{
    uint8_t h[32];
    sha256_t s;
    sha256_init(&s);
    sha256_update(&s, uid, 12);
    sha256_final(&s, h);
    mac[0] = 0x02;
    memcpy(mac + 1, h, 5);
}

void netcfg_serial(const uint8_t uid[12], char out[NETCFG_SERIAL_LEN + 1])
{
    uint8_t mac[6];
    netcfg_mac(uid, mac);
    snprintf(out, NETCFG_SERIAL_LEN + 1, "SB%02X%02X%02X%02X%02X", mac[1], mac[2], mac[3], mac[4], mac[5]);
}
