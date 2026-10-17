// Host tests for disc_core (Plan 12 task 2): make -C stm32/firmware/test/host; argv[1] is disc_vectors.json,
// the sample packets the C# tests read too.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "disc_core.h"

static int failures;
#define CHECK(cond, ...)                \
    do {                                \
        if (!(cond)) {                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);        \
            printf("\n");               \
            failures++;                 \
        }                               \
    } while (0)

static const char *KIND[] = {"ignore", "search", "set", "restart", "factory", "bad"};

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    if (s && fread(s, 1, (size_t)n, f) == (size_t)n)
        s[n] = '\0';
    fclose(f);
    return s;
}

static const char *str(const cJSON *o, const char *k) { return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, k)); }
static int num(const cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k)->valueint; }

static void net_of(const cJSON *n, disc_net_t *out) { disc_net_from(n, out); }

static void test_requests(const cJSON *v)
{
    const char *mac = str(v, "mac");
    const cJSON *r;
    cJSON_ArrayForEach(r, cJSON_GetObjectItem(v, "requests")) {
        const char *text = str(r, "text"), *want = str(r, "expect");
        disc_req_t q;
        memset(&q, 0x5a, sizeof q);
        disc_kind_t k = disc_parse(text, strlen(text), mac, &q);
        CHECK(!strcmp(KIND[k], want), "%s: %s, not %s", str(r, "name"), KIND[k], want);
        if (k != DISC_IGNORE)
            CHECK(strlen(q.id) == 8, "%s: id copied", str(r, "name"));
        if (k == DISC_SET)
            CHECK(q.net.ip_mode == 1 && !strcmp(q.net.ip, "10.10.0.7") && !strcmp(q.net.gateway, "10.10.0.1") &&
                      q.net.dns_mode == 1 && !strcmp(q.net.dns2, "1.1.1.1"), "%s: the seven fields", str(r, "name"));
    }
}

static void test_size_limit(void)
{
    const char *mac = "02:95:b8:e9:a7:9f", *head = "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"search\",\"id\":\"7f3a91c2\"";
    char buf[DISC_MAX + 2];
    disc_req_t q;
    for (size_t len = DISC_MAX; len <= DISC_MAX + 1; len++) {  // spaces pad a valid packet to len bytes
        size_t h = strlen(head);
        memcpy(buf, head, h);
        memset(buf + h, ' ', len - h - 1);
        buf[len - 1] = '}';
        disc_kind_t k = disc_parse(buf, len, mac, &q);
        CHECK(k == (len == DISC_MAX ? DISC_SEARCH : DISC_IGNORE), "%zu bytes: %s", len, KIND[k]);
    }
    char nest[DISC_MAX];  // a valid search with 17 nested lists in it: past CJSON_NESTING_LIMIT (16)
    int n = snprintf(nest, sizeof nest, "%s,\"x\":", head);
    for (int i = 0; i < 17; i++)
        nest[n++] = '[';
    for (int i = 0; i < 17; i++)
        nest[n++] = ']';
    nest[n++] = '}';
    CHECK(disc_parse(nest, (size_t)n, mac, &q) == DISC_IGNORE, "nested deeper than 16: ignored");
}

static void test_found(const cJSON *v)
{
    const cJSON *f = cJSON_GetObjectItem(v, "found"), *in = cJSON_GetObjectItem(f, "info");
    disc_info_t info = {.mac = str(in, "mac"), .sn = str(in, "sn"), .model = str(in, "model"), .fw = str(in, "fw"),
                        .hostname = str(in, "hostname"), .web = (unsigned)num(in, "web"), .boot = (uint32_t)num(in, "boot"),
                        .eth_ip = 0xC0A80007u, .eth_mask = 0xFFFFFF00u, .eth_gw = 0xC0A80001u};  // the vector's eth
    net_of(cJSON_GetObjectItem(in, "net"), &info.saved);
    char buf[DISC_MAX + 1];
    int n = disc_found(&info, str(f, "id"), buf, sizeof buf);
    CHECK(n > 0 && !strcmp(buf, str(f, "text")), "found byte for byte:\n%s\n%s", n > 0 ? buf : "(none)", str(f, "text"));
    // The longest reply still fits a datagram: a 16-byte UTF-8 hostname, every address 15 characters.
    disc_info_t big = info;
    big.hostname = "nhà-máy-số-1";  // 16 bytes in UTF-8
    big.web = 65535;
    big.boot = 4294967295u;
    big.eth_ip = big.eth_mask = big.eth_gw = 0xFFFFFFFFu;
    disc_net_t w = {.ip_mode = 1, .dns_mode = 1};
    strcpy(w.ip, "255.255.255.255");
    strcpy(w.netmask, w.ip);
    strcpy(w.gateway, w.ip);
    strcpy(w.dns1, w.ip);
    strcpy(w.dns2, w.ip);
    big.saved = w;
    n = disc_found(&big, "7f3a91c2", buf, sizeof buf);
    CHECK(n > 0 && n <= DISC_MAX, "the longest found fits (%d)", n);
    CHECK(disc_found(&info, "7f3a91c2", buf, 64) == -1, "too small a buffer: -1");
}

static void test_acks(const cJSON *v)
{
    const cJSON *a;
    cJSON_ArrayForEach(a, cJSON_GetObjectItem(v, "acks")) {
        char buf[128];
        int n = disc_ack(str(a, "id"), str(a, "mac"), str(a, "err"), buf, sizeof buf);
        CHECK(n > 0 && !strcmp(buf, str(a, "text")), "ack %s:\n%s", str(a, "err") ? str(a, "err") : "ok", n > 0 ? buf : "(none)");
    }
}

static void test_net_json(void)
{
    disc_net_t n = {.ip_mode = 1, .dns_mode = 0};
    strcpy(n.ip, "10.10.0.7");
    strcpy(n.netmask, "255.255.255.0");
    strcpy(n.gateway, "10.10.0.1");
    strcpy(n.dns1, "8.8.8.8");
    strcpy(n.dns2, "1.1.1.1");
    cJSON *j = disc_net_json(&n);
    char *t = cJSON_PrintUnformatted(j);
    CHECK(t && !strcmp(t, "{\"ipMode\":1,\"ip\":\"10.10.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":0,"
                          "\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}"), "the seven fields for store_patch: %s", t);
    disc_net_t back;
    disc_net_from(j, &back);
    CHECK(back.ip_mode == 1 && back.dns_mode == 0 && !strcmp(back.ip, n.ip) && !strcmp(back.netmask, n.netmask) &&
              !strcmp(back.gateway, n.gateway) && !strcmp(back.dns1, n.dns1) && !strcmp(back.dns2, n.dns2), "and read back");
    free(t);
    cJSON_Delete(j);
}

int main(int argc, char **argv)
{
    char *text = argc > 1 ? slurp(argv[1]) : NULL;
    cJSON *v = text ? cJSON_Parse(text) : NULL;
    if (!v) {
        printf("test_disc_core: cannot read the vectors (%s)\n", argc > 1 ? argv[1] : "no path");
        return 1;
    }
    test_requests(v);
    test_size_limit();
    test_found(v);
    test_acks(v);
    test_net_json();
    cJSON_Delete(v);
    free(text);
    printf("test_disc_core: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
