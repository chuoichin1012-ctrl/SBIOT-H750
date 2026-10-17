# Plan 12: find gateways on the LAN and set their Ethernet address (SBIOT Finder)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (Native, chosen) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a Windows tool, `SbiotFinder.exe`, finds every SBIOT gateway on the LAN by UDP broadcast, also one in another subnet, shows its MAC, SN, IP and firmware, opens its Web UI, and changes its Ethernet settings, restarts it or factory-resets it, as USR-M100's configuration utility does.

**Architecture:** a `disc` task on the gateway listens on UDP 48600 (Ethernet only) and answers by broadcast. Packet logic is pure C (`disc_core.c`, host-tested); the task (`disc.c`) does the socket, the store and the restart. `store_patch` merges the seven Ethernet fields into `network` under the store lock. The tool is .NET 10: a protocol library (tested on Linux), a WinForms window, published as one self-contained exe. One file of sample packets (`disc_vectors.json`) is read by the C and the C# tests, so both sides agree byte for byte.

**Tech Stack:** C11, FreeRTOS, lwIP sockets (`SO_BINDTODEVICE`), cJSON; host tests (`stm32/firmware/test/host`, make); Python 3 bench (`test/bench`, Windows Python for the broadcast side); TypeScript (`stm32/web`, node --test); .NET 10 SDK 10.0.112 (`/usr/lib/dotnet`), WinForms, xUnit.

**Spec:** `stm32/docs/specs/2026-10-17-lan-discovery-design.md` (approved by the user and Codex, r4); main spec §10.5.

## Global Constraints

- The user, 2026-10-17: own protocol, not USR-compatible; WinForms; **every packet broadcast** to `255.255.255.255`; **no username or password**: a packet in the agreed format is acted on.
- UDP port **48600**, Ethernet netif only; JSON object of at most **512** bytes; `"sbiot":"disc"`, `"v":1`; `id` exactly 8 lowercase hex; `mac` `xx:xx:xx:xx:xx:xx` lowercase hex.
- Commands (`set`, `restart`, `factory`) must carry `mac`; without it they are ignored.
- The operation first, then the `ack` with its real result (`ok`, `err` `bad` or `store`), then the restart (1 s; the factory reset schedules its own 1.5 s).
- `found` carries `eth` (running Ethernet address), `net` (the seven saved fields, read from the store per reply), `web` (running port), `boot` (`store_boot_count`).
- The gateway checks syntax only; the tool checks the rest. Configs are read once at boot (spec §9.1).
- The tool waits 10 s for an `ack`, never resends by itself, then polls `search` every 3 s for up to 120 s until `boot` grows.
- Keep it simple. Run only the affected tests after an edit. Do not run `factory` on the bench board and do not change `network.priority` (the SIM is locked; LTE must not re-attach).

## Review Focus

1. A `set` from the tool while the Web saves `network` (an APN change): neither is lost, and the LTE fields and `priority` survive the tool's `set` (Task 1 lock-trace test; Task 3 bench reads `priority` and `apn` back).
2. A PC with several adapters (Hyper-V/WSL vEthernet, VPN): the broadcast leaves the chosen adapter and the replies come back to it (Task 3 bench binds the LAN address; Task 6 Windows check).
3. A datagram of exactly 512 bytes is handled, 513 is ignored; the longest `found` (16-byte UTF-8 hostname, every address 15 characters, the largest numbers) still fits in 512 (Task 2 tests).
4. Two gateways answer one search: both rows appear; a later answer from the same MAC replaces its row (Task 5 `FoundTable` test).
5. With LTE as the default route (`priority` 2), replies still leave by Ethernet: the socket is bound to the Ethernet netif by `SO_BINDTODEVICE` before `bind`. Not runnable while the SIM is locked: the reviewer checks the code path (Task 3).

---

### Task 1: `store_patch`

**Files:**
- Modify: `stm32/firmware/components/store/include/store.h` (declaration after `store_set_text`)
- Modify: `stm32/firmware/components/store/store.c` (new function after `store_set_text`)
- Test: `stm32/firmware/test/host/test_store.c`

**Interfaces:**
- Produces: `store_err_t store_patch(const char *name, const cJSON *fields, char *err, size_t errlen);`: replaces (or adds) each member of the object `fields` in the small object config `name`, keeps every other member, validates and writes as `store_set_text` does, the store lock held from the read to the write. `STORE_ENOTFOUND` for an unknown name, `edgeDevices`, a list config or a `fields` that is not an object; `STORE_ENOMEM` without writing; otherwise `store_set_text`'s result.

- [ ] **Step 1: Write the failing test.** In `test_store.c`, count lock releases in the fake port, then add the test and call it from `main` (non-`STORE_CUTS` branch, after `test_with_defaults_oom()`):

```c
static int s_releases;  // the lock fell to depth 0 (Plan 12 task 1)

void store_port_lock(void) { s_depth++; }
void store_port_unlock(void)
{
    if (--s_depth == 0)
        s_releases++;
}

// Plan 12 task 1, the lock trace (Codex plan r1): inside store_patch, every cJSON allocation (the read is the parse
// of the cached text) and every flash program (the write) must happen with the lock held, and the lock must not be
// released between the first of them and the last.
static bool s_tracing;
static int s_trace_min = 1 << 30;              // the lowest lock depth seen
static int s_rel_first = -1, s_rel_last = -1;  // s_releases at the first allocation, at the last program

static void *trace_malloc(size_t n)
{
    if (s_tracing) {
        if (s_depth < s_trace_min)
            s_trace_min = s_depth;
        if (s_rel_first < 0)
            s_rel_first = s_releases;
    }
    return malloc(n);
}
```

and at the top of the existing `tracing_program` (the flash program hook):

```c
    if (s_tracing) {
        if (s_depth < s_trace_min)
            s_trace_min = s_depth;
        s_rel_last = s_releases;
    }
```

```c
// Plan 12 task 1: the tool's `set` merges the Ethernet fields; the rest of `network` stays, the lock is held
// from the read to the write (one release), so a Web save from another task waits for it.
static void test_patch(void)
{
    flash_new(43);
    reboot();
    char err[200];
    const char *mine = "{\"priority\":0,\"ipMode\":0,\"ip\":\"192.168.0.7\",\"apn\":\"m3-world\",\"future\":5}";
    CHECK(store_set_text("network", mine, strlen(mine), err, sizeof err) == STORE_OK, "set network: %s", err);
    cJSON *f = cJSON_Parse("{\"ipMode\":1,\"ip\":\"10.10.0.7\",\"netmask\":\"255.255.255.0\"}");
    cJSON_Hooks th = {trace_malloc, free};
    cJSON_InitHooks(&th);
    s_tracing = true;
    store_err_t pe = store_patch("network", f, err, sizeof err);
    s_tracing = false;
    cJSON_InitHooks(NULL);
    CHECK(pe == STORE_OK, "patch: %s", err);
    CHECK(s_trace_min >= 1, "the read and the write ran under the lock (lowest depth %d)", s_trace_min);
    CHECK(s_rel_first >= 0 && s_rel_last == s_rel_first, "no release between the read and the write (%d, %d)", s_rel_first, s_rel_last);
    BALANCED("store_patch");
    for (int pass = 0; pass < 2; pass++) {  // the cache, then the flash after a reboot
        char *t = store_get_json("network");
        cJSON *d = cJSON_Parse(t);
        CHECK(d && cJSON_GetObjectItem(d, "ipMode")->valueint == 1, "ipMode patched (%s)", t);
        CHECK(d && !strcmp(cJSON_GetObjectItem(d, "ip")->valuestring, "10.10.0.7"), "ip patched");
        CHECK(d && cJSON_GetObjectItem(d, "priority")->valueint == 0, "priority kept");
        CHECK(d && !strcmp(cJSON_GetObjectItem(d, "apn")->valuestring, "m3-world"), "apn kept");
        CHECK(d && cJSON_GetObjectItem(d, "future") && cJSON_GetObjectItem(d, "future")->valueint == 5, "an unknown key kept");
        cJSON_Delete(d);
        free(t);
        reboot();
    }
    CHECK(store_patch("nonsense", f, err, sizeof err) == STORE_ENOTFOUND, "unknown name");
    CHECK(store_patch("edgeDevices", f, err, sizeof err) == STORE_ENOTFOUND, "not edgeDevices");
    cJSON *arr = cJSON_CreateArray();
    CHECK(store_patch("network", arr, err, sizeof err) == STORE_ENOTFOUND, "fields must be an object");
    cJSON_Delete(arr);
    // Out of memory at each allocation in turn: STORE_ENOMEM and nothing written, or a whole save.
    cJSON *g = cJSON_Parse("{\"ip\":\"10.10.0.9\"}");
    char *cur = store_get_json("network");  // what a save must give: today's document with only ip changed
    cJSON *want = cJSON_Parse(cur);
    cJSON_ReplaceItemInObjectCaseSensitive(want, "ip", cJSON_CreateString("10.10.0.9"));
    free(cur);
    cJSON_Hooks hooks = {fail_malloc, free};
    cJSON_InitHooks(&hooks);
    for (int n = 0; n < 2000; n++) {
        char *was = store_get_json("network");
        s_alloc_fail_at = n;
        store_err_t e = store_patch("network", g, err, sizeof err);
        bool hit = s_alloc_fail_at < 0;
        s_alloc_fail_at = -1;
        char *now = store_get_json("network");
        CHECK(e == STORE_OK || (e == STORE_ENOMEM && now && was && !strcmp(now, was)),
              "allocation %d failed: STORE_ENOMEM with nothing written, or saved (%d)", n, e);
        if (e == STORE_OK) {  // a save is the whole document: the field patched, every other member as it was
            cJSON *d = now ? cJSON_Parse(now) : NULL;  // s_alloc_fail_at is -1 again: no failure here
            CHECK(d && cJSON_Compare(d, want, true), "allocation %d: saved whole (%s)", n, now);
            cJSON_Delete(d);
        }
        free(was);
        free(now);
        BALANCED("store_patch out of memory");
        if (!hit)
            break;
    }
    cJSON_InitHooks(NULL);
    cJSON_Delete(want);
    cJSON_Delete(g);
    cJSON_Delete(f);
}
```

- [ ] **Step 2: Run it to see it fail.** Run: `set -o pipefail; make -C stm32/firmware/test/host build/test_store 2>&1 | tail -5`. Expected: compile error, `implicit declaration of function 'store_patch'`.

- [ ] **Step 3: Implement.** `store.h`, after `store_set_text`:

```c
/** Replaces (or adds) each member of the object `fields` in the object config `name`, keeping every other member
 *  (unknown keys too), then saves it as store_set_text does. The store lock is held from the read to the write, so
 *  a save from another task cannot fall between them (Plan 12, SBIOT Finder's `set`). STORE_ENOMEM writes nothing. */
store_err_t store_patch(const char *name, const cJSON *fields, char *err, size_t errlen);
```

`store.c`, after `store_set_text`:

```c
store_err_t store_patch(const char *name, const cJSON *fields, char *err, size_t errlen)
{
    err[0] = '\0';
    LOCK();  // held to the end, from the read to the write: store_set_text below nests (a recursive mutex on the board)
    if (!store_known(name) || !strcmp(name, EDGE) || cfg_is_list(&s_env, name) || !cJSON_IsObject(fields)) {
        UNLOCK();
        snprintf(err, errlen, "Unknown settings \"%s\".", name ? name : "");
        return STORE_ENOTFOUND;
    }
    int i = cache_find(name);
    cJSON *doc = i >= 0 ? cJSON_ParseWithLength(s_pool + s_cache[i].off, s_cache[i].len) : NULL;
    bool ok = doc != NULL;
    for (const cJSON *f = fields->child; ok && f; f = f->next) {
        cJSON *v = cJSON_Duplicate(f, true);
        ok = v && (cJSON_GetObjectItemCaseSensitive(doc, f->string) ? cJSON_ReplaceItemInObjectCaseSensitive(doc, f->string, v)
                                                                   : cJSON_AddItemToObject(doc, f->string, v));
        if (!ok)
            cJSON_Delete(v);
    }
    char *text = ok ? cJSON_PrintUnformatted(doc) : NULL;
    cJSON_Delete(doc);
    store_err_t e;
    if (!text) {
        snprintf(err, errlen, "The gateway does not have enough free memory to save these settings.");
        e = STORE_ENOMEM;
    } else {
        e = store_set_text(name, text, strlen(text), err, errlen);
    }
    UNLOCK();
    free(text);
    return e;
}
```

Note: `cJSON_ReplaceItemInObjectCaseSensitive` frees the old item and takes `v` on success; on failure `v` is still ours (deleted above).

- [ ] **Step 4: Run it to see it pass.** Run: `make -C stm32/firmware/test/host build/test_store build/test_store_small && (cd stm32/firmware/test/host && build/test_store && build/test_store_small)`. Expected: `test_store: ok` twice.

- [ ] **Step 5: Commit.**

```bash
git add stm32/firmware/components/store stm32/firmware/test/host/test_store.c
git commit -m "stm32 store: store_patch merges fields under one lock hold (Plan 12 task 1)"
```

### Task 2: `disc_core`, the packet logic, and the shared sample packets

**Files:**
- Create: `stm32/firmware/components/services/include/disc_core.h`
- Create: `stm32/firmware/components/services/disc_core.c`
- Create: `stm32/firmware/test/host/disc_vectors.json`
- Create: `stm32/firmware/test/host/test_disc_core.c`
- Modify: `stm32/firmware/test/host/Makefile` (target `test_disc_core`, added to `all` and run with the vectors file)

**Interfaces:**
- Consumes: `bool netcfg_ip4(const char *s, uint32_t *out);` (`components/netmgr/netcfg.c`, strict dotted quad).
- Produces (`disc_core.h`):

```c
#define DISC_PORT 48600
#define DISC_MAX 512  // a datagram's limit; the task reads DISC_MAX + 1 to see one too long

typedef enum { DISC_IGNORE, DISC_SEARCH, DISC_SET, DISC_RESTART, DISC_FACTORY, DISC_BAD } disc_kind_t;

typedef struct {
    int ip_mode, dns_mode;                             // 0 or 1
    char ip[16], netmask[16], gateway[16], dns1[16], dns2[16];
} disc_net_t;

typedef struct {
    char id[9];
    disc_net_t net;  // DISC_SET only
} disc_req_t;

/** What the gateway with this MAC ("02:95:b8:e9:a7:9f") does with one datagram (spec §2.1-§2.3). DISC_BAD: a
 *  `set` for this gateway whose `net` is wrong (answer `ack` err "bad"); out->id is filled for every kind but
 *  DISC_IGNORE. */
disc_kind_t disc_parse(const char *buf, size_t len, const char *my_mac, disc_req_t *out);

typedef struct {
    const char *mac, *sn, *model, *fw, *hostname;
    unsigned web;
    uint32_t boot;
    uint32_t eth_ip, eth_mask, eth_gw;  // running, host order
    disc_net_t saved;                   // the seven saved fields
} disc_info_t;

/** The `found` reply; its length, or -1 when it does not fit cap. */
int disc_found(const disc_info_t *in, const char *id, char *buf, size_t cap);
/** The `ack`: err NULL for ok, else "bad" or "store". Its length, or -1. */
int disc_ack(const char *id, const char *mac, const char *err, char *buf, size_t cap);
/** The seven fields of a `network` config object (a missing field reads 0 or ""). */
void disc_net_from(const cJSON *network, disc_net_t *out);
/** The seven fields as an object for store_patch; NULL without memory. */
cJSON *disc_net_json(const disc_net_t *n);
```

- [ ] **Step 1: Write the sample packets.** `disc_vectors.json` (the C test and the C# tests read it; `built: true` entries are what the tool's builder must produce byte for byte):

```json
{
  "mac": "02:95:b8:e9:a7:9f",
  "requests": [
    {"name": "search", "built": true, "expect": "search", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"search\",\"id\":\"7f3a91c2\"}"},
    {"name": "search for this MAC", "built": true, "expect": "search", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"search\",\"id\":\"7f3a91c2\",\"mac\":\"02:95:b8:e9:a7:9f\"}"},
    {"name": "search for another MAC", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"search\",\"id\":\"7f3a91c2\",\"mac\":\"02:00:00:00:00:01\"}"},
    {"name": "MAC in capitals", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"search\",\"id\":\"7f3a91c2\",\"mac\":\"02:95:B8:E9:A7:9F\"}"},
    {"name": "set", "built": true, "expect": "set", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"net\":{\"ipMode\":1,\"ip\":\"10.10.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":1,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}}"},
    {"name": "set without mac", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"net\":{\"ipMode\":1,\"ip\":\"10.10.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":1,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}}"},
    {"name": "set for another MAC", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"mac\":\"02:00:00:00:00:01\",\"net\":{\"ipMode\":1,\"ip\":\"10.10.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":1,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}}"},
    {"name": "set without dns2", "expect": "bad", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"net\":{\"ipMode\":1,\"ip\":\"10.10.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":1,\"dns1\":\"8.8.8.8\"}}"},
    {"name": "set with ip 256", "expect": "bad", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"net\":{\"ipMode\":1,\"ip\":\"10.10.0.256\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":1,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}}"},
    {"name": "set with a leading zero", "expect": "bad", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"net\":{\"ipMode\":1,\"ip\":\"10.10.0.07\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":1,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}}"},
    {"name": "set with ipMode 2", "expect": "bad", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"net\":{\"ipMode\":2,\"ip\":\"10.10.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":1,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}}"},
    {"name": "set with ipMode as text", "expect": "bad", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"net\":{\"ipMode\":\"1\",\"ip\":\"10.10.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":1,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}}"},
    {"name": "set with dnsMode 0.5", "expect": "bad", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"net\":{\"ipMode\":1,\"ip\":\"10.10.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":0.5,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}}"},
    {"name": "set with net a list", "expect": "bad", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"net\":[]}"},
    {"name": "set with a key twice in net", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"set\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"net\":{\"ipMode\":1,\"ip\":\"10.10.0.7\",\"ip\":\"10.10.0.8\",\"netmask\":\"255.255.255.0\",\"gateway\":\"10.10.0.1\",\"dnsMode\":1,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}}"},
    {"name": "mac twice", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"restart\",\"id\":\"5b20e7d1\",\"mac\":\"02:00:00:00:00:01\",\"mac\":\"02:95:b8:e9:a7:9f\"}"},
    {"name": "restart", "built": true, "expect": "restart", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"restart\",\"id\":\"0a1b2c3d\",\"mac\":\"02:95:b8:e9:a7:9f\"}"},
    {"name": "factory", "built": true, "expect": "factory", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"factory\",\"id\":\"0a1b2c3d\",\"mac\":\"02:95:b8:e9:a7:9f\"}"},
    {"name": "restart without mac", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"restart\",\"id\":\"0a1b2c3d\"}"},
    {"name": "version 2", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":2,\"op\":\"search\",\"id\":\"7f3a91c2\"}"},
    {"name": "no signature", "expect": "ignore", "text": "{\"v\":1,\"op\":\"search\",\"id\":\"7f3a91c2\"}"},
    {"name": "another signature", "expect": "ignore", "text": "{\"sbiot\":\"usr\",\"v\":1,\"op\":\"search\",\"id\":\"7f3a91c2\"}"},
    {"name": "a found from another gateway", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"found\",\"id\":\"7f3a91c2\",\"mac\":\"02:95:b8:e9:a7:9f\"}"},
    {"name": "an ack", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"ack\",\"id\":\"7f3a91c2\",\"mac\":\"02:95:b8:e9:a7:9f\",\"ok\":true}"},
    {"name": "an unknown op", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"reboot\",\"id\":\"7f3a91c2\",\"mac\":\"02:95:b8:e9:a7:9f\"}"},
    {"name": "id of 7", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"search\",\"id\":\"7f3a91c\"}"},
    {"name": "id in capitals", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"search\",\"id\":\"7F3A91C2\"}"},
    {"name": "no id", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"search\"}"},
    {"name": "a list", "expect": "ignore", "text": "[{\"sbiot\":\"disc\",\"v\":1,\"op\":\"search\",\"id\":\"7f3a91c2\"}]"},
    {"name": "not JSON", "expect": "ignore", "text": "www.usr.cn"},
    {"name": "garbage after the object", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"factory\",\"id\":\"0a1b2c3d\",\"mac\":\"02:95:b8:e9:a7:9f\"}x"},
    {"name": "two objects", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"search\",\"id\":\"7f3a91c2\"}{\"sbiot\":\"disc\",\"v\":1,\"op\":\"factory\",\"id\":\"0a1b2c3d\",\"mac\":\"02:95:b8:e9:a7:9f\"}"},
    {"name": "a number with a leading zero", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":01,\"op\":\"search\",\"id\":\"7f3a91c2\"}"},
    {"name": "an escaped NUL in mac", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"restart\",\"id\":\"0a1b2c3d\",\"mac\":\"02:95:b8:e9:a7:9f\\u0000x\"}"},
    {"name": "an escaped NUL in op", "expect": "ignore", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"restart\\u0000x\",\"id\":\"0a1b2c3d\",\"mac\":\"02:95:b8:e9:a7:9f\"}"}
  ],
  "found": {
    "info": {"mac": "02:95:b8:e9:a7:9f", "sn": "SB2610000001", "model": "SBIOT-H750", "fw": "0.7.0", "hostname": "sbiot-gw", "web": 80, "boot": 197,
             "eth": {"ip": "192.168.0.7", "netmask": "255.255.255.0", "gateway": "192.168.0.1"},
             "net": {"ipMode": 1, "ip": "192.168.0.7", "netmask": "255.255.255.0", "gateway": "192.168.0.1", "dnsMode": 1, "dns1": "8.8.8.8", "dns2": "1.1.1.1"}},
    "id": "7f3a91c2",
    "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"found\",\"id\":\"7f3a91c2\",\"mac\":\"02:95:b8:e9:a7:9f\",\"sn\":\"SB2610000001\",\"model\":\"SBIOT-H750\",\"fw\":\"0.7.0\",\"hostname\":\"sbiot-gw\",\"web\":80,\"boot\":197,\"eth\":{\"ip\":\"192.168.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"192.168.0.1\"},\"net\":{\"ipMode\":1,\"ip\":\"192.168.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"192.168.0.1\",\"dnsMode\":1,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\"}}"
  },
  "acks": [
    {"id": "5b20e7d1", "mac": "02:95:b8:e9:a7:9f", "err": null, "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"ack\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"ok\":true}"},
    {"id": "5b20e7d1", "mac": "02:95:b8:e9:a7:9f", "err": "bad", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"ack\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"ok\":false,\"err\":\"bad\"}"},
    {"id": "5b20e7d1", "mac": "02:95:b8:e9:a7:9f", "err": "store", "text": "{\"sbiot\":\"disc\",\"v\":1,\"op\":\"ack\",\"id\":\"5b20e7d1\",\"mac\":\"02:95:b8:e9:a7:9f\",\"ok\":false,\"err\":\"store\"}"}
  ]
}
```

- [ ] **Step 2: Write the failing test.** `test_disc_core.c`:

```c
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
```

`Makefile`: add `$(BUILD)/test_disc_core` to `all`'s prerequisites, the run line `$(BUILD)/test_disc_core disc_vectors.json` after `$(BUILD)/test_netcfg`, and:

```make
SVC := ../../components/services

$(BUILD)/test_disc_core: test_disc_core.c $(SVC)/disc_core.c ../../components/proto/jsonscan.c $(NET)/netcfg.c $(NET)/inet.c ../../components/ota/sha256.c $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SVC)/include -I$(NET)/include $^ -lm -o $@
```

(`netcfg.c` brings `netcfg_ip4`; it needs `inet.c` and `sha256.c` as `test_netcfg` does; `jsonscan.c` is the strict check. Place the rule after `NET` is defined. The `\u0000` scan may reject a harmless packet holding an escaped backslash before `u0000`; no packet of this protocol has one.)

- [ ] **Step 3: Run it to see it fail.** Run: `set -o pipefail; make -C stm32/firmware/test/host build/test_disc_core 2>&1 | tail -3`. Expected: error, `disc_core.h: No such file or directory`.

- [ ] **Step 4: Implement.** `disc_core.h` with the block from **Interfaces** (plus `#pragma once`, `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`, `"cJSON.h"`, and a header comment pointing at the spec). `disc_core.c`:

```c
// SBIOT Finder's packets on the gateway (Plan 12, stm32/docs/specs/2026-10-17-lan-discovery-design.md §2):
// parse and check, the `found` and `ack` replies. Pure C, host-tested (test_disc_core).
#include "disc_core.h"
#include <stdio.h>
#include <string.h>
#include "jsonscan.h"
#include "netcfg.h"

/** The whole datagram is one strict JSON value (jsonscan: RFC 8259, nothing after it, no `01`), with no NUL in it,
 *  raw or escaped: cJSON stops a string at a NUL, so "mac":"<this MAC>\u0000x" would read as this MAC (Codex plan r1). */
static bool strict(const char *buf, size_t len)
{
    js_span_t root;
    if (memchr(buf, '\0', len) || !js_parse_root(buf, len, &root))
        return false;
    for (size_t i = 0; i + 6 <= len; i++)
        if (!memcmp(buf + i, "\\u0000", 6))
            return false;
    return true;
}

static bool hex_lower(const char *s, size_t n)
{
    if (!s || strlen(s) != n)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return false;
    return true;
}

static bool mac_ok(const char *s)
{
    if (!s || strlen(s) != 17)
        return false;
    for (int i = 0; i < 17; i++) {
        bool hex = (s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f');
        if (i % 3 == 2 ? s[i] != ':' : !hex)
            return false;
    }
    return true;
}

/** No member name twice in this object (cJSON keeps both, so a packet could name two targets or two values). */
static bool unique_keys(const cJSON *o)
{
    for (const cJSON *a = o->child; a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (a->string && b->string && !strcmp(a->string, b->string))
                return false;
    return true;
}

static bool mode(const cJSON *n, const char *k, int *out)
{
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(n, k);
    if (!cJSON_IsNumber(m) || (m->valuedouble != 0 && m->valuedouble != 1))
        return false;
    *out = (int)m->valuedouble;
    return true;
}

static bool addr(const cJSON *n, const char *k, char out[16])
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(n, k));
    uint32_t ip;
    if (!s || !netcfg_ip4(s, &ip))
        return false;
    snprintf(out, 16, "%s", s);  // a valid quad is at most 15 characters
    return true;
}

static bool net_ok(const cJSON *n, disc_net_t *out)
{
    return cJSON_IsObject(n) && mode(n, "ipMode", &out->ip_mode) && mode(n, "dnsMode", &out->dns_mode) &&
           addr(n, "ip", out->ip) && addr(n, "netmask", out->netmask) && addr(n, "gateway", out->gateway) &&
           addr(n, "dns1", out->dns1) && addr(n, "dns2", out->dns2);
}

disc_kind_t disc_parse(const char *buf, size_t len, const char *my_mac, disc_req_t *out)
{
    memset(out, 0, sizeof *out);
    if (len > DISC_MAX || !strict(buf, len))
        return DISC_IGNORE;
    cJSON *p = cJSON_ParseWithLength(buf, len);
    disc_kind_t k = DISC_IGNORE;
    const cJSON *net = cJSON_GetObjectItemCaseSensitive(p, "net");
    const char *sig = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "sbiot"));
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(p, "v");
    const char *op = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "op"));
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "id"));
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(p, "mac");
    const char *mac = cJSON_GetStringValue(m);
    if (!cJSON_IsObject(p) || !unique_keys(p) || (cJSON_IsObject(net) && !unique_keys(net)) || !sig ||
        strcmp(sig, "disc") || !cJSON_IsNumber(v) || v->valuedouble != 1 || !op || !hex_lower(id, 8) ||
        (m && !mac_ok(mac)))
        goto done;
    bool mine = mac && !strcmp(mac, my_mac);
    if (!strcmp(op, "search"))
        k = !m || mine ? DISC_SEARCH : DISC_IGNORE;
    else if (!mine)  // commands must name this gateway (spec §2.3)
        k = DISC_IGNORE;
    else if (!strcmp(op, "set"))
        k = net_ok(net, &out->net) ? DISC_SET : DISC_BAD;
    else if (!strcmp(op, "restart"))
        k = DISC_RESTART;
    else if (!strcmp(op, "factory"))
        k = DISC_FACTORY;
    if (k != DISC_IGNORE)
        memcpy(out->id, id, 9);
done:
    cJSON_Delete(p);
    return k;
}

static void quad(char out[16], uint32_t ip)
{
    snprintf(out, 16, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)(ip >> 16 & 255), (unsigned)(ip >> 8 & 255), (unsigned)(ip & 255));
}

static cJSON *head(const char *op, const char *id, const char *mac)
{
    cJSON *o = cJSON_CreateObject();
    if (o && cJSON_AddStringToObject(o, "sbiot", "disc") && cJSON_AddNumberToObject(o, "v", 1) &&
        cJSON_AddStringToObject(o, "op", op) && cJSON_AddStringToObject(o, "id", id) && cJSON_AddStringToObject(o, "mac", mac))
        return o;
    cJSON_Delete(o);
    return NULL;
}

/** The text of o into buf (without the terminating NUL in the count), or -1; o is deleted. */
static int emit(cJSON *o, bool ok, char *buf, size_t cap)
{
    char *t = ok && o ? cJSON_PrintUnformatted(o) : NULL;
    cJSON_Delete(o);
    int n = -1;
    if (t && strlen(t) < cap) {
        n = (int)strlen(t);
        memcpy(buf, t, (size_t)n + 1);
    }
    cJSON_free(t);
    return n;
}

cJSON *disc_net_json(const disc_net_t *n)
{
    cJSON *o = cJSON_CreateObject();
    if (o && cJSON_AddNumberToObject(o, "ipMode", n->ip_mode) && cJSON_AddStringToObject(o, "ip", n->ip) &&
        cJSON_AddStringToObject(o, "netmask", n->netmask) && cJSON_AddStringToObject(o, "gateway", n->gateway) &&
        cJSON_AddNumberToObject(o, "dnsMode", n->dns_mode) && cJSON_AddStringToObject(o, "dns1", n->dns1) &&
        cJSON_AddStringToObject(o, "dns2", n->dns2))
        return o;
    cJSON_Delete(o);
    return NULL;
}

int disc_found(const disc_info_t *in, const char *id, char *buf, size_t cap)
{
    cJSON *o = head("found", id, in->mac), *eth = cJSON_CreateObject(), *net = disc_net_json(&in->saved);
    char a[16], b[16], c[16];
    quad(a, in->eth_ip);
    quad(b, in->eth_mask);
    quad(c, in->eth_gw);
    bool ok = o && eth && net && cJSON_AddStringToObject(o, "sn", in->sn) && cJSON_AddStringToObject(o, "model", in->model) &&
              cJSON_AddStringToObject(o, "fw", in->fw) && cJSON_AddStringToObject(o, "hostname", in->hostname) &&
              cJSON_AddNumberToObject(o, "web", in->web) && cJSON_AddNumberToObject(o, "boot", in->boot) &&
              cJSON_AddStringToObject(eth, "ip", a) && cJSON_AddStringToObject(eth, "netmask", b) &&
              cJSON_AddStringToObject(eth, "gateway", c) && cJSON_AddItemToObject(o, "eth", eth);
    if (ok)
        eth = NULL;  // o owns it now
    ok = ok && cJSON_AddItemToObject(o, "net", net);
    if (ok)
        net = NULL;
    cJSON_Delete(eth);
    cJSON_Delete(net);
    return emit(o, ok, buf, cap);
}

int disc_ack(const char *id, const char *mac, const char *err, char *buf, size_t cap)
{
    cJSON *o = head("ack", id, mac);
    bool ok = o && cJSON_AddBoolToObject(o, "ok", err == NULL) && (!err || cJSON_AddStringToObject(o, "err", err));
    return emit(o, ok, buf, cap);
}

void disc_net_from(const cJSON *network, disc_net_t *out)
{
    memset(out, 0, sizeof *out);
    out->ip_mode = cJSON_GetObjectItemCaseSensitive(network, "ipMode") ? cJSON_GetObjectItemCaseSensitive(network, "ipMode")->valueint : 0;
    out->dns_mode = cJSON_GetObjectItemCaseSensitive(network, "dnsMode") ? cJSON_GetObjectItemCaseSensitive(network, "dnsMode")->valueint : 0;
    const char *keys[] = {"ip", "netmask", "gateway", "dns1", "dns2"};
    char *dst[] = {out->ip, out->netmask, out->gateway, out->dns1, out->dns2};
    for (int i = 0; i < 5; i++) {
        const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(network, keys[i]));
        snprintf(dst[i], 16, "%s", s ? s : "");
    }
}
```

Notes for the implementer: cJSON prints `"v":1`, `"web":80` and `"boot":197` as integers (whole doubles print without a fraction); a `uint32_t` boot of 4294967295 prints as `4294967295`. Add `disc_core.c` to the app's sources in `stm32/firmware/CMakeLists.txt` line 125 (`${SVC}/disc_core.c`) in this task, so the firmware build checks it compiles for the target.

- [ ] **Step 5: Run it to see it pass.** Run: `make -C stm32/firmware/test/host build/test_disc_core && (cd stm32/firmware/test/host && build/test_disc_core disc_vectors.json)`. Expected: `test_disc_core: ok`. Then `set -o pipefail; cd stm32/firmware && cmake --build --preset app 2>&1 | tail -3`. Expected: exit 0, links with no warnings.

- [ ] **Step 6: Commit.**

```bash
git add stm32/firmware/components/services/disc_core.c stm32/firmware/components/services/include/disc_core.h \
        stm32/firmware/test/host/disc_vectors.json stm32/firmware/test/host/test_disc_core.c stm32/firmware/test/host/Makefile \
        stm32/firmware/CMakeLists.txt
git commit -m "stm32 disc: packet logic and the shared sample packets (Plan 12 task 2)"
```

### Task 3: the `disc` task on the board, and the bench

**Files:**
- Create: `stm32/firmware/components/services/include/disc.h`, `stm32/firmware/components/services/disc.c`
- Modify: `stm32/firmware/components/netmgr/include/netmgr.h`, `netmgr.c` (`netmgr_eth_ifname`)
- Modify: `stm32/firmware/app/app_main.c` (start `disc` after `bench_serial_init()`, before `services_start(sys)`)
- Modify: `stm32/firmware/CMakeLists.txt` (`${SVC}/disc.c`)
- Modify: `stm32/firmware/test/bench/win_peer.py` (mode `disc`)
- Create: `stm32/firmware/test/bench/disc_check.py`
- Modify: `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` (§6.1 task table and §11.4 supervisor list: `disc`, priority 5, 30 s), `stm32/docs/plans/2026-10-14-00-roadmap.md` (row 12)

**Interfaces:**
- Consumes: Task 1 `store_patch`; Task 2 `disc_parse`, `disc_found`, `disc_ack`, `disc_net_from`, `disc_net_json`, `DISC_PORT`, `DISC_MAX`; `netmgr_status(net_status_t *)` (`ip`, `mask`, `gw` running, host order; `bound`; `mac[6]`); `store_get_json`, `store_boot_count`, `store_factory_reset`; `sysmgr_restart_later(uint32_t)`, `sysmgr_serial()`; `wdog_hb_add`, `wdog_hb_beat`; `board_rand32()`.
- Produces: `void disc_start(const cJSON *sys);` (reads `hostname` and `webPort` from the `system` object once); `bool netmgr_eth_ifname(char out[NETIF_NAMESIZE]);` (false while the Ethernet netif is not added).

- [ ] **Step 1: `netmgr_eth_ifname`.** `netmgr.h`:

```c
/** The Ethernet netif's lwIP name ("et0"), for SO_BINDTODEVICE; false while it is not added (Plan 12). */
bool netmgr_eth_ifname(char out[6]);
```

`netmgr.c`:

```c
bool netmgr_eth_ifname(char out[6])
{
    LOCK_TCPIP_CORE();
    bool ok = s_added && netif_index_to_name(netif_get_index(&s_netif), out) != NULL;
    UNLOCK_TCPIP_CORE();
    return ok;
}
```

(`NETIF_NAMESIZE` is 6 in lwIP; if the header uses the macro, use it instead of 6.)

- [ ] **Step 2: The task.** `disc.h`:

```c
// SBIOT Finder's discovery on the gateway (Plan 12; stm32/docs/specs/2026-10-17-lan-discovery-design.md).
#pragma once
#include "cJSON.h"
/** Binds UDP 48600 on the Ethernet netif (now, so before the bridges) and starts the `disc` task. Reads
 *  system.hostname and system.webPort once. Call before services_start. */
void disc_start(const cJSON *sys);
```

`disc.c`:

```c
// The `disc` task (Plan 12): SBIOT Finder's packets on UDP 48600, Ethernet only, every reply broadcast.
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

static StaticTask_t s_tcb;
static StackType_t s_stack[DISC_STACK_WORDS];
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
    if (!st.bound)
        return;  // spec §2.4: no address on Ethernet, no reply
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
        SLOG_W("disc", "network set by SBIOT Finder: %s %s (%s)", q.net.ip_mode ? "static" : "DHCP", q.net.ip,
               e == STORE_OK ? "saved, restarting" : "not saved");
        ack(fd, port, q.id, e == STORE_OK ? NULL : "store");
        if (e == STORE_OK)
            sysmgr_restart_later(1000);
        return;
    }
    case DISC_RESTART:
        SLOG_W("disc", "restart requested by SBIOT Finder");
        ack(fd, port, q.id, NULL);
        sysmgr_restart_later(1000);
        return;
    case DISC_FACTORY: {
        SLOG_W("disc", "factory reset requested by SBIOT Finder");
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
        SLOG_E("disc", "cannot bind UDP port %u on Ethernet: SBIOT Finder will not find this gateway", DISC_PORT);
        s_told = true;
    }
    return fd;
}

static void disc_task(void *arg)
{
    (void)arg;
    int hb = wdog_hb_add("disc", 30000);
    int fd = s_fd;
    static char buf[DISC_MAX + 1];  // one more than allowed: a datagram that fills it is too long
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
```

Implementer: match the includes to what the tree uses (`SLOG_W`/`SLOG_E` from `syslog.h`; `board_rand32` from `board_rng.h`; `APP_VERSION` is defined for the whole `app` target). If DTCM has no room for the 6 KB stack (the link fails or `check_elf.py` complains), put `s_stack` in `.axi_bss` as `bridge.c` does and ledger the ruling.

`app_main.c`: `#include "disc.h"`, and after `bench_serial_init();`:

```c
    disc_start(sys);  // Plan 12: before the bridges, so UDP 48600 is SBIOT Finder's
```

`CMakeLists.txt` line 125: add `${SVC}/disc.c` after `${SVC}/disc_core.c`.

- [ ] **Step 3: Build.** Run: `set -o pipefail; cd stm32/firmware && cmake --build --preset app 2>&1 | tail -5 && python3 tools/check_elf.py build/h750`. Expected: no warnings; `check_elf` passes (layout, RAM and QSPI within their limits).

- [ ] **Step 4: The bench peer.** `win_peer.py`: document the mode in the docstring (`disc  iface, port (48600), secs, send (the packet, hex): broadcast from iface, one event per reply`) and add:

```python
def disc(args):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind((args['iface'], 0))  # the LAN adapter: the broadcast leaves by it, the replies come back to it
    s.settimeout(0.1)
    s.sendto(bytes.fromhex(args['send']), ('255.255.255.255', int(args.get('port', 48600))))
    while time.time() - T0 < float(args.get('secs', 3)):
        try:
            d, a = s.recvfrom(2048)
        except socket.timeout:
            continue
        out(ev='reply', text=d.decode('utf-8', 'replace'), peer=a[0])
```

and `'disc': disc` in the mode table.

- [ ] **Step 5: The bench.** `disc_check.py` (WSL is behind NAT, so the broadcasts are sent from Windows by `win_peer.py`):

```python
"""Plan 12 bench: SBIOT Finder's protocol against the board (spec 2026-10-17-lan-discovery-design.md §4).
Windows sends the broadcasts (WSL is behind NAT): win_peer.py disc. Never sends `factory` (the SIM is locked:
a factory reset sets priority 1 and LTE would attach). Leaves the board at 192.168.0.7.

    python3 disc_check.py
"""
import json
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_check as bc  # noqa: E402  (peer, wait_peer, WIN)
import web_check as w  # noqa: E402

SN = 'SB2610000001'  # the bench board's record (memory: bench-board-state)
failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def new_id():
    return '%08x' % random.getrandbits(32)


def send(pkt, secs=3):
    """Broadcasts pkt from Windows; the JSON replies with its id."""
    text = json.dumps(pkt, separators=(',', ':'))
    ev = bc.wait_peer(bc.peer('disc', iface=bc.WIN, send=text.encode().hex(), secs=secs))
    out = []
    for e in ev:
        if e.get('ev') == 'reply':
            try:
                r = json.loads(e['text'])
            except ValueError:
                continue
            if r.get('id') == pkt['id']:
                out.append(r)
    return out


def search(mac=None):
    p = {'sbiot': 'disc', 'v': 1, 'op': 'search', 'id': new_id()}
    if mac:
        p['mac'] = mac
    return [r for r in send(p) if r.get('op') == 'found']


def command(op, mac, net=None):
    p = {'sbiot': 'disc', 'v': 1, 'op': op, 'id': new_id(), 'mac': mac}
    if net is not None:
        p['net'] = net
    acks = [r for r in send(p, secs=10) if r.get('op') == 'ack']
    return acks[0] if acks else None


def back(mac, boot, limit=120):
    """Searches every 3 s until mac answers with a larger boot: its found, or None."""
    t = time.time()
    while time.time() - t < limit:
        for f in search(mac):
            if f['boot'] > boot:
                return f
        time.sleep(3)
    return None


def restart_and_wait(mac, boot):
    w.req('POST', '/api/restart')
    return back(mac, boot)


def main():
    _, orig = w.get_json('/api/config/network')
    if not orig or orig.get('priority') != 0:
        print('bench: the board must start at network.priority 0 (the SIM is locked); found %s' % (orig or {}).get('priority'))
        return 1
    f = search()
    me = [x for x in f if x.get('sn') == SN]
    check(len(me) == 1, 'search finds the board (SN %s)' % SN)
    if not me:
        return 1
    me = me[0]
    mac, boot = me['mac'], me['boot']
    home = me['net']  # the board's own saved settings: put back as they were
    away = dict(home, ipMode=1, ip='10.10.0.7', netmask='255.255.255.0', gateway='10.10.0.1')
    check(me['eth']['ip'] == '192.168.0.7' and home['ip'] == '192.168.0.7' and me['web'] == 80 and me['model'] == 'SBIOT-H750',
          'found: running and saved addresses, web port, model (%s)' % json.dumps(me))
    check(all(k in me for k in ('fw', 'hostname')), 'found: fw and hostname')
    check(search('02:00:00:00:00:01') == [], 'a search for another MAC: no reply')

    a = command('set', mac, dict(home, ip='10.10.0.256'))
    check(a is not None and a.get('ok') is False and a.get('err') == 'bad', 'set with a wrong address: ack bad (%s)' % a)
    p = {'sbiot': 'disc', 'v': 1, 'op': 'restart', 'id': new_id()}
    check(send(p) == [], 'restart without mac: ignored')
    time.sleep(3)
    f = search(mac)
    check(f and f[0]['boot'] == boot, 'and the board did not restart')

    try:
        a = command('set', mac, away)
        check(a is not None and a.get('ok') is True, 'set 10.10.0.7/24 (another subnet than Windows): ack ok')
        f = back(mac, boot)
        check(f is not None and f['eth']['ip'] == '10.10.0.7' and f['net'] == away,
              'found again at 10.10.0.7 after its restart, from 192.168.0.10')
        boot = f['boot'] if f else boot
    finally:
        a = command('set', mac, home)
        check(a is not None and a.get('ok') is True, 'set back to 192.168.0.7: ack ok')
        f = back(mac, boot)
        check(f is not None and f['eth']['ip'] == '192.168.0.7' and f['net'] == home, 'found again at 192.168.0.7, settings as before')
        boot = f['boot'] if f else boot
    _, net = w.get_json('/api/config/network')
    check(net == orig, 'network is exactly as before the bench: LTE fields, priority and the rest kept')

    # A flood of junk on 48600 for 70 s, longer than syslog's 60 s supervisor deadline (the lowest task, priority 2):
    # if disc starved the tasks below it, the supervisor would restart the board (Codex plan r2).
    flood = bc.subprocess.run([bc.WIN_PY, '-c', (
        'import socket,time\n'
        's=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)\n'
        's.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1)\n'
        's.bind(("%s",0))\n'
        't=time.time();n=0\n'
        'while time.time()-t<70:\n'
        '    s.sendto(b"x"*100,("255.255.255.255",48600));n+=1\n'
        'print(n)\n' % bc.WIN)], cwd='/mnt/c', capture_output=True, text=True)
    sent = int(flood.stdout.strip() or 0) if flood.returncode == 0 else 0
    check(sent > 50000, 'the flood ran: %d datagrams in 70 s (exit %d)' % (sent, flood.returncode))
    f = search(mac)
    check(f and f[0]['boot'] == boot, 'after it the board answers, not restarted (no task starved past its deadline)')

    # A UART set (around the UI) to bind UDP 48600: disc bound first at boot, so the board is still found.
    _, p1 = w.get_json('/api/config/port1')
    try:
        w.req('POST', '/api/config/port1', body=json.dumps(dict(p1, workMode=2, localPort=48600)).encode())
        f = restart_and_wait(mac, boot)
        check(f is not None, 'with UART1 configured as a UDP server on 48600, the board is still found after a restart')
        boot = f['boot'] if f else boot
        _, st = w.get_json('/api/status')
        check('ERROR' in json.dumps(st), "and UART1's bridge reports its bind error (%s)" % json.dumps(st)[:300])
    finally:
        w.req('POST', '/api/config/port1', body=json.dumps(p1).encode())
        f = restart_and_wait(mac, boot)
        boot = f['boot'] if f else boot
    _, now1 = w.get_json('/api/config/port1')
    check(now1 == p1, 'UART1 restored')

    a = command('restart', mac)
    check(a is not None and a.get('ok') is True, 'restart: ack ok')
    f = back(mac, boot)
    check(f is not None, 'and the board comes back with a new boot')
    print('bench: ' + ('FAILED (%d)' % len(failures) if failures else 'ok'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
```

(`w.get_json` returns `(status, body)` and signs in as the other bench scripts do; `bc.peer` copies `win_peer.py` to Windows before each run; `bc.subprocess` is `bridge_check`'s import. Check where `/api/status` shows a bridge's state and tighten the `ERROR` check to that field. `workMode` 2 is `BR_UDP_SERVER`.)

- [ ] **Step 6: Run on the board.** Flash the new app by **web OTA** (the staging trap: an app flashed over SWD is replaced by the staged image), as the earlier plans did (`ota_check.py`'s helper, or the web page). Then: `set -o pipefail; python3 stm32/firmware/test/bench/disc_check.py 2>&1 | tee .superpowers/sdd/2026-10-17-12-lan-discovery/disc_check.log | tail -25`. Expected: every line `ok`, `bench: ok`. Also `python3 stm32/firmware/test/bench/supervisor_check.py` is **not** needed; read `/api/status` once: `hbMissing` 0.

If Windows does not deliver the broadcast replies to a socket bound to `192.168.0.10` (no `reply` events while the board's console shows the request), ledger it and bind the peer to `0.0.0.0` with the broadcast still sent from the LAN adapter (`IP_UNICAST_IF` is not needed for broadcast); apply the same change to the tool in Task 5.

- [ ] **Step 7: Docs.** Main spec §6.1 (task table: `disc`, priority 5, 6 KB, UDP 48600) and §11.4 (supervised: `disc` 30 s); roadmap: a row `| 12 | LAN discovery (SBIOT Finder) | ... | §10.5 |`. Commit:

```bash
git add stm32/firmware stm32/docs
git commit -m "stm32 disc: the disc task on UDP 48600 and its bench (Plan 12 task 3)"
```

### Task 4: the Web UI keeps UDP 48600 free

**Files:**
- Modify: `stm32/web/src/lib/rules.ts`
- Modify: `stm32/web/src/pages/Port.tsx`
- Test: `stm32/web/tests/unit/rules.test.ts`

**Interfaces:**
- Produces: `export const DISC_PORT = 48600;` and `export function discPortClash(port: Obj): string | null;`: the message for a `localPort` of 48600, else null.

- [ ] **Step 1: Write the failing test** (append to `rules.test.ts`, and add `discPortClash` to the import):

```ts
test('the local port 48600 is SBIOT Finder\'s', () => {
  assert.equal(discPortClash({ workMode: 1, localPort: 48600 }), 'UDP port 48600 is reserved for SBIOT Finder: choose another local port.');
  assert.equal(discPortClash({ workMode: 1, localPort: 4000 }), null);
  assert.equal(discPortClash({ workMode: 3, listenPort: 48600, localPort: 0 }), null);
});
```

- [ ] **Step 2: Run it to see it fail.** Run: `set -o pipefail; cd stm32/web && npm run test:unit 2>&1 | tail -5`. Expected: tsc error, `has no exported member 'discPortClash'`.

- [ ] **Step 3: Implement.** `rules.ts`:

```ts
/** SBIOT Finder's UDP port (Plan 12): the gateway's `disc` task holds it, so no UART may bind it locally. */
export const DISC_PORT = 48600;

export function discPortClash(port: Obj): string | null {
  return Number(port.localPort) === DISC_PORT ? `UDP port ${DISC_PORT} is reserved for SBIOT Finder: choose another local port.` : null;
}
```

`Port.tsx`: import `discPortClash`; next to `clash`:

```tsx
  const disc = discPortClash(f.values);
  const errors = { ...f.errors, ...(clash ? { listenPort: clash } : {}), ...(disc ? { localPort: disc } : {}) };
```

and `invalid={!f.valid || !!clash || !!disc || !checked}` on the `SaveBar`.

- [ ] **Step 4: Run it to see it pass.** Run: `set -o pipefail; cd stm32/web && npm run test:unit 2>&1 | tail -5 && npx tsc --noEmit -p . && npm run build:firmware 2>&1 | tail -3`. Expected: all tests pass; the embed rewrites `www_files.c`.

- [ ] **Step 5: Commit.**

```bash
git add stm32/web stm32/firmware/components/webapi/www
git commit -m "stm32 web: UDP 48600 is SBIOT Finder's (Plan 12 task 4)"
```

### Task 5: `Sbiot.Finder.Protocol` and its tests

**Files:**
- Create: `stm32/tools/finder/Sbiot.Finder.slnx` (or `.sln`, what `dotnet new sln` makes), `stm32/tools/finder/.gitignore` (`bin/`, `obj/`, `out/`)
- Create: `stm32/tools/finder/Sbiot.Finder.Protocol/Sbiot.Finder.Protocol.csproj`, `Packets.cs`, `NetSettings.cs`, `FoundTable.cs`, `DiscClient.cs`, `Adapters.cs`
- Create: `stm32/tools/finder/Sbiot.Finder.Tests/Sbiot.Finder.Tests.csproj`, `PacketTests.cs`, `NetSettingsTests.cs`, `FoundTableTests.cs`

**Interfaces:**
- Consumes: `stm32/firmware/test/host/disc_vectors.json` (Task 2), copied to the test output.
- Produces (namespace `Sbiot.Finder.Protocol`):
  - `record NetSettings(int IpMode, string Ip, string Netmask, string Gateway, int DnsMode, string Dns1, string Dns2)` with `string? Validate()` (null when good, else an English message) and `static bool IsQuad(string s)`;
  - `record Found(string Mac, string Sn, string Model, string Fw, string Hostname, int Web, long Boot, string EthIp, string EthNetmask, string EthGateway, NetSettings Net)`;
  - `record Ack(string Mac, bool Ok, string? Err)`;
  - `static class Packets`: `const int Port = 48600`; `string NewId()`; `string Search(string id, string? mac = null)`; `string Set(string id, string mac, NetSettings n)`; `string Command(string op, string id, string mac)`; `Found? ParseFound(string text, string id)`; `Ack? ParseAck(string text, string id, string mac)`;
  - `sealed class FoundTable`: `void Merge(Found f)` (one row per MAC, the later replaces), `IReadOnlyList<Found> Rows` (in first-seen order);
  - `sealed class DiscClient : IDisposable`: `DiscClient(IPAddress local)`; `Task<List<Found>> SearchAsync(TimeSpan wait, string? mac = null)`; `Task<Ack?> CommandAsync(string op, string mac, NetSettings? net, TimeSpan wait)`; `Task<Found?> WaitBackAsync(string mac, long boot, TimeSpan total, TimeSpan every)`;
  - `static class Adapters`: `IReadOnlyList<(string Name, IPAddress Address)> List()`.

- [ ] **Step 1: Scaffold.** From `stm32/tools/finder`:

```bash
dotnet new sln -n Sbiot.Finder
dotnet new classlib -n Sbiot.Finder.Protocol -f net10.0
dotnet new xunit -n Sbiot.Finder.Tests -f net10.0
dotnet sln add Sbiot.Finder.Protocol Sbiot.Finder.Tests
dotnet add Sbiot.Finder.Tests reference Sbiot.Finder.Protocol
rm Sbiot.Finder.Protocol/Class1.cs Sbiot.Finder.Tests/UnitTest1.cs
printf 'bin/\nobj/\nout/\n' > .gitignore
```

In `Sbiot.Finder.Tests.csproj` add:

```xml
  <ItemGroup>
    <None Include="../../../firmware/test/host/disc_vectors.json" Link="disc_vectors.json" CopyToOutputDirectory="PreserveNewest" />
  </ItemGroup>
```

- [ ] **Step 2: Write the failing tests.** `PacketTests.cs`:

```csharp
using System.Text.Json;
using Sbiot.Finder.Protocol;
using Xunit;

public class PacketTests
{
    static readonly JsonElement V = JsonDocument.Parse(File.ReadAllText("disc_vectors.json")).RootElement;
    static JsonElement Req(string name) => V.GetProperty("requests").EnumerateArray().First(r => r.GetProperty("name").GetString() == name);
    static string Text(string name) => Req(name).GetProperty("text").GetString()!;
    const string Mac = "02:95:b8:e9:a7:9f";
    static readonly NetSettings Away = new(1, "10.10.0.7", "255.255.255.0", "10.10.0.1", 1, "8.8.8.8", "1.1.1.1");

    [Fact] public void SearchMatchesTheGateway() => Assert.Equal(Text("search"), Packets.Search("7f3a91c2"));
    [Fact] public void SearchForOneMac() => Assert.Equal(Text("search for this MAC"), Packets.Search("7f3a91c2", Mac));
    [Fact] public void SetMatchesTheGateway() => Assert.Equal(Text("set"), Packets.Set("5b20e7d1", Mac, Away));
    [Fact] public void RestartAndFactory()
    {
        Assert.Equal(Text("restart"), Packets.Command("restart", "0a1b2c3d", Mac));
        Assert.Equal(Text("factory"), Packets.Command("factory", "0a1b2c3d", Mac));
    }

    [Fact] public void EveryBuiltVectorIsCovered() =>
        Assert.Equal(5, V.GetProperty("requests").EnumerateArray().Count(r => r.TryGetProperty("built", out var b) && b.GetBoolean()));

    [Fact] public void ParsesFound()
    {
        var f = V.GetProperty("found");
        var got = Packets.ParseFound(f.GetProperty("text").GetString()!, "7f3a91c2");
        Assert.NotNull(got);
        Assert.Equal(Mac, got!.Mac);
        Assert.Equal("SB2610000001", got.Sn);
        Assert.Equal(197, got.Boot);
        Assert.Equal(80, got.Web);
        Assert.Equal("192.168.0.7", got.EthIp);
        Assert.Equal(new NetSettings(1, "192.168.0.7", "255.255.255.0", "192.168.0.1", 1, "8.8.8.8", "1.1.1.1"), got.Net);
    }

    [Fact] public void FoundForAnotherIdIsNotOurs() =>
        Assert.Null(Packets.ParseFound(V.GetProperty("found").GetProperty("text").GetString()!, "00000000"));

    [Fact] public void OurOwnSearchIsNotAFound() => Assert.Null(Packets.ParseFound(Text("search"), "7f3a91c2"));

    [Fact] public void ParsesAcks()
    {
        foreach (var a in V.GetProperty("acks").EnumerateArray())
        {
            var got = Packets.ParseAck(a.GetProperty("text").GetString()!, "5b20e7d1", Mac);
            Assert.NotNull(got);
            var err = a.GetProperty("err").ValueKind == JsonValueKind.Null ? null : a.GetProperty("err").GetString();
            Assert.Equal(err is null, got!.Ok);
            Assert.Equal(err, got.Err);
        }
    }

    [Fact] public void AckOfAnotherGatewayOrIdIsNotOurs()
    {
        var t = V.GetProperty("acks")[0].GetProperty("text").GetString()!;
        Assert.Null(Packets.ParseAck(t, "5b20e7d1", "02:00:00:00:00:01"));
        Assert.Null(Packets.ParseAck(t, "00000000", Mac));
        Assert.Null(Packets.ParseAck("not json", "5b20e7d1", Mac));
    }

    [Fact] public void IdsAreEightLowercaseHex() =>
        Assert.Matches("^[0-9a-f]{8}$", Packets.NewId());
}
```

`NetSettingsTests.cs`:

```csharp
using Sbiot.Finder.Protocol;
using Xunit;

public class NetSettingsTests
{
    static NetSettings S(string ip = "10.10.0.7", string mask = "255.255.255.0", string gw = "10.10.0.1", int ipMode = 1, int dnsMode = 1,
                         string d1 = "8.8.8.8", string d2 = "1.1.1.1") => new(ipMode, ip, mask, gw, dnsMode, d1, d2);

    [Fact] public void AGoodStaticAddress() => Assert.Null(S().Validate());
    [Theory]
    [InlineData("10.10.0.256")] [InlineData("10.10.0.07")] [InlineData("10.10.0")] [InlineData(" 10.10.0.7")] [InlineData("")]
    [InlineData("10.10.0.\u0667")] [InlineData("10.10.0.7\n")]  // an Arabic-Indic seven; a trailing newline
    public void ABadQuad(string ip) => Assert.NotNull(S(ip: ip).Validate());
    [Theory] [InlineData("255.0.255.0")] [InlineData("0.0.0.0")] [InlineData("255.255.255.255")]
    public void AMaskThatIsNotUsable(string mask) => Assert.NotNull(S(mask: mask).Validate());
    [Fact] public void TheNetworkAddress() => Assert.NotNull(S(ip: "10.10.0.0").Validate());
    [Fact] public void TheBroadcastAddress() => Assert.NotNull(S(ip: "10.10.0.255").Validate());
    [Fact] public void AGatewayOutsideTheSubnet() => Assert.NotNull(S(gw: "10.10.1.1").Validate());
    [Fact] public void TheGatewayIsTheAddress() => Assert.NotNull(S(gw: "10.10.0.7").Validate());
    [Fact] public void AnIsolatedLanHasNoGateway() => Assert.Null(S(gw: "0.0.0.0").Validate());
    [Fact] public void NoDnsServerIsAllowed() => Assert.Null(S(d1: "0.0.0.0", d2: "0.0.0.0").Validate());
    [Fact] public void DhcpChecksSyntaxOnly() => Assert.Null(S(ipMode: 0, dnsMode: 0, gw: "10.10.1.1", d1: "0.0.0.0").Validate());
    [Fact] public void DhcpStillNeedsQuads() => Assert.NotNull(S(ipMode: 0, ip: "x").Validate());
}
```

`FoundTableTests.cs`:

```csharp
using Sbiot.Finder.Protocol;
using Xunit;

public class FoundTableTests
{
    static readonly NetSettings N = new(1, "192.168.0.7", "255.255.255.0", "192.168.0.1", 1, "8.8.8.8", "1.1.1.1");
    static Found F(string mac, long boot) => new(mac, "SB2610000001", "SBIOT-H750", "0.7.0", "sbiot-gw", 80, boot, "192.168.0.7", "255.255.255.0", "192.168.0.1", N);

    [Fact] public void TwoGatewaysTwoRows()
    {
        var t = new FoundTable();
        t.Merge(F("02:00:00:00:00:01", 1));
        t.Merge(F("02:00:00:00:00:02", 1));
        Assert.Equal(2, t.Rows.Count);
    }

    [Fact] public void ALaterReplyReplacesItsRowInPlace()
    {
        var t = new FoundTable();
        t.Merge(F("02:00:00:00:00:01", 1));
        t.Merge(F("02:00:00:00:00:02", 1));
        t.Merge(F("02:00:00:00:00:01", 2));
        Assert.Equal(2, t.Rows.Count);
        Assert.Equal("02:00:00:00:00:01", t.Rows[0].Mac);
        Assert.Equal(2, t.Rows[0].Boot);
    }
}
```

- [ ] **Step 3: Run them to see them fail.** Run: `set -o pipefail; cd stm32/tools/finder && dotnet test 2>&1 | tail -5`. Expected: build errors, `The type or namespace name 'Packets' could not be found`.

- [ ] **Step 4: Implement.** `NetSettings.cs`:

```csharp
using System.Net;
using System.Text.RegularExpressions;

namespace Sbiot.Finder.Protocol;

/// <summary>The seven Ethernet fields of the gateway's `network` config (spec §2.2). The tool checks them; the gateway checks syntax only.</summary>
public sealed record NetSettings(int IpMode, string Ip, string Netmask, string Gateway, int DnsMode, string Dns1, string Dns2)
{
    // ASCII digits only ([0-9], not \d: .NET's \d takes any Unicode digit, which IPAddress.Parse then rejects; Codex plan r2).
    static readonly Regex Quad = new(@"\A(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}\z");

    /// <summary>A strict dotted quad, as the gateway's netcfg_ip4: no sign, space or leading zero.</summary>
    public static bool IsQuad(string s) => s is not null && Quad.IsMatch(s);

    static uint U(string s) => (uint)IPAddress.NetworkToHostOrder(BitConverter.ToInt32(IPAddress.Parse(s).GetAddressBytes()));

    /// <summary>Null when the settings are good, else what to fix.</summary>
    public string? Validate()
    {
        foreach (var (v, what) in new[] { (Ip, "IP address"), (Netmask, "Subnet mask"), (Gateway, "Gateway"), (Dns1, "DNS 1"), (Dns2, "DNS 2") })
            if (!IsQuad(v)) return $"{what}: enter four numbers 0-255, such as 192.168.0.7.";
        if (IpMode == 1)
        {
            uint ip = U(Ip), mask = U(Netmask), gw = U(Gateway);
            uint inv = ~mask;
            if (mask == 0 || mask == 0xFFFFFFFF || (inv & (inv + 1)) != 0) return "Subnet mask: use a mask such as 255.255.255.0.";
            if ((ip & inv) == 0 || (ip & inv) == inv) return "IP address: this is the network's own or broadcast address.";
            // 0.0.0.0 is "no gateway" (an isolated LAN); the gateway accepts it.
            if (gw != 0 && ((gw & mask) != (ip & mask) || gw == ip || (gw & inv) == inv))
                return "Gateway: it must be another address in the same subnet as the IP address, or 0.0.0.0 for none.";
        }
        return null;  // DNS: syntax only; 0.0.0.0 is "none", as the gateway reads it
    }
}
```

`Packets.cs`:

```csharp
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Sbiot.Finder.Protocol;

public sealed record Found(string Mac, string Sn, string Model, string Fw, string Hostname, int Web, long Boot,
                           string EthIp, string EthNetmask, string EthGateway, NetSettings Net);

public sealed record Ack(string Mac, bool Ok, string? Err);

/// <summary>SBIOT Finder's packets (spec §2): built byte for byte as disc_vectors.json has them.</summary>
public static class Packets
{
    public const int Port = 48600;

    public static string NewId() => Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(4));

    static string Write(Action<Utf8JsonWriter> body)
    {
        using var ms = new MemoryStream();
        using (var w = new Utf8JsonWriter(ms))
        {
            w.WriteStartObject();
            body(w);
            w.WriteEndObject();
        }
        return Encoding.UTF8.GetString(ms.ToArray());
    }

    static void Head(Utf8JsonWriter w, string op, string id, string? mac)
    {
        w.WriteString("sbiot", "disc");
        w.WriteNumber("v", 1);
        w.WriteString("op", op);
        w.WriteString("id", id);
        if (mac is not null) w.WriteString("mac", mac);
    }

    public static string Search(string id, string? mac = null) => Write(w => Head(w, "search", id, mac));

    public static string Command(string op, string id, string mac) => Write(w => Head(w, op, id, mac));

    public static string Set(string id, string mac, NetSettings n) => Write(w =>
    {
        Head(w, "set", id, mac);
        w.WriteStartObject("net");
        w.WriteNumber("ipMode", n.IpMode);
        w.WriteString("ip", n.Ip);
        w.WriteString("netmask", n.Netmask);
        w.WriteString("gateway", n.Gateway);
        w.WriteNumber("dnsMode", n.DnsMode);
        w.WriteString("dns1", n.Dns1);
        w.WriteString("dns2", n.Dns2);
        w.WriteEndObject();
    });

    static JsonElement? Ours(string text, string op, string id)
    {
        try
        {
            var r = JsonDocument.Parse(text).RootElement;
            if (r.ValueKind == JsonValueKind.Object && r.TryGetProperty("sbiot", out var s) && s.GetString() == "disc" &&
                r.TryGetProperty("v", out var v) && v.GetInt32() == 1 && r.GetProperty("op").GetString() == op &&
                r.GetProperty("id").GetString() == id)
                return r;
        }
        catch (Exception e) when (e is JsonException or InvalidOperationException or KeyNotFoundException or FormatException) { }
        return null;
    }

    static NetSettings Net(JsonElement n) => new(n.GetProperty("ipMode").GetInt32(), n.GetProperty("ip").GetString()!,
        n.GetProperty("netmask").GetString()!, n.GetProperty("gateway").GetString()!, n.GetProperty("dnsMode").GetInt32(),
        n.GetProperty("dns1").GetString()!, n.GetProperty("dns2").GetString()!);

    /// <summary>A gateway's reply to our search `id`; null for anything else (our own broadcast comes back too).</summary>
    public static Found? ParseFound(string text, string id)
    {
        if (Ours(text, "found", id) is not { } r) return null;
        try
        {
            var e = r.GetProperty("eth");
            return new Found(r.GetProperty("mac").GetString()!, r.GetProperty("sn").GetString()!, r.GetProperty("model").GetString()!,
                r.GetProperty("fw").GetString()!, r.GetProperty("hostname").GetString()!, r.GetProperty("web").GetInt32(),
                r.GetProperty("boot").GetInt64(), e.GetProperty("ip").GetString()!, e.GetProperty("netmask").GetString()!,
                e.GetProperty("gateway").GetString()!, Net(r.GetProperty("net")));
        }
        catch (Exception x) when (x is InvalidOperationException or KeyNotFoundException or FormatException) { return null; }
    }

    /// <summary>The ack of our command `id` to `mac`; null for anything else.</summary>
    public static Ack? ParseAck(string text, string id, string mac)
    {
        if (Ours(text, "ack", id) is not { } r) return null;
        try
        {
            if (r.GetProperty("mac").GetString() != mac) return null;
            bool ok = r.GetProperty("ok").GetBoolean();
            return new Ack(mac, ok, ok ? null : r.GetProperty("err").GetString());
        }
        catch (Exception x) when (x is InvalidOperationException or KeyNotFoundException) { return null; }
    }
}
```

`FoundTable.cs`:

```csharp
namespace Sbiot.Finder.Protocol;

/// <summary>The window's rows: one per MAC, in the order first seen; a later reply replaces its row.</summary>
public sealed class FoundTable
{
    readonly List<Found> rows = new();
    public IReadOnlyList<Found> Rows => rows;

    public void Merge(Found f)
    {
        int i = rows.FindIndex(r => r.Mac == f.Mac);
        if (i >= 0) rows[i] = f; else rows.Add(f);
    }

    public void Clear() => rows.Clear();
}
```

`DiscClient.cs`:

```csharp
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Sbiot.Finder.Protocol;

/// <summary>Broadcasts from one adapter's address and reads the replies that come back to it (spec §3).</summary>
public sealed class DiscClient : IDisposable
{
    readonly UdpClient udp;

    public DiscClient(IPAddress local)
    {
        udp = new UdpClient(new IPEndPoint(local, 0)) { EnableBroadcast = true };
    }

    public void Dispose() => udp.Dispose();

    async Task Send(string text)
    {
        var b = Encoding.UTF8.GetBytes(text);
        await udp.SendAsync(b, b.Length, new IPEndPoint(IPAddress.Broadcast, Packets.Port));
    }

    /// <summary>Reads replies for `wait`, or until `done` says so.</summary>
    async Task Read(TimeSpan wait, Func<string, bool> done)
    {
        using var cts = new CancellationTokenSource(wait);
        try
        {
            while (true)
            {
                var r = await udp.ReceiveAsync(cts.Token);
                if (done(Encoding.UTF8.GetString(r.Buffer))) return;
            }
        }
        catch (OperationCanceledException) { }
    }

    public async Task<List<Found>> SearchAsync(TimeSpan wait, string? mac = null)
    {
        var id = Packets.NewId();
        var table = new FoundTable();
        await Send(Packets.Search(id, mac));
        await Read(wait, t => { if (Packets.ParseFound(t, id) is { } f) table.Merge(f); return false; });
        return table.Rows.ToList();
    }

    public async Task<Ack?> CommandAsync(string op, string mac, NetSettings? net, TimeSpan wait)
    {
        var id = Packets.NewId();
        Ack? ack = null;
        await Send(op == "set" ? Packets.Set(id, mac, net!) : Packets.Command(op, id, mac));
        await Read(wait, t => (ack = Packets.ParseAck(t, id, mac)) is not null);
        return ack;
    }

    /// <summary>Searches for `mac` every `every` until it answers with a boot above `boot`; null after `total`.</summary>
    public async Task<Found?> WaitBackAsync(string mac, long boot, TimeSpan total, TimeSpan every)
    {
        var end = DateTime.UtcNow + total;
        while (DateTime.UtcNow < end)
        {
            var f = (await SearchAsync(every, mac)).FirstOrDefault(x => x.Boot > boot);
            if (f is not null) return f;
        }
        return null;
    }
}
```

`Adapters.cs`:

```csharp
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace Sbiot.Finder.Protocol;

public static class Adapters
{
    /// <summary>Each IPv4 address of each adapter that is up, loopback left out.</summary>
    public static IReadOnlyList<(string Name, IPAddress Address)> List() =>
        NetworkInterface.GetAllNetworkInterfaces()
            .Where(n => n.OperationalStatus == OperationalStatus.Up && n.NetworkInterfaceType != NetworkInterfaceType.Loopback)
            .SelectMany(n => n.GetIPProperties().UnicastAddresses
                .Where(a => a.Address.AddressFamily == AddressFamily.InterNetwork)
                .Select(a => (n.Name, a.Address)))
            .ToList();
}
```

- [ ] **Step 5: Run them to see them pass.** Run: `set -o pipefail; cd stm32/tools/finder && dotnet test 2>&1 | tail -5`. Expected: `Passed!` with 0 failed.

- [ ] **Step 6: Commit.**

```bash
git add stm32/tools/finder
git commit -m "stm32 finder: the protocol library and its tests (Plan 12 task 5)"
```

### Task 6: the WinForms window, `SbiotFinder.exe`

**Files:**
- Create: `stm32/tools/finder/Sbiot.Finder/Sbiot.Finder.csproj`, `Program.cs`, `MainForm.cs`, `NetworkForm.cs`
- Modify: `stm32/tools/finder/Sbiot.Finder.slnx` (add the project)
- Create: `stm32/tools/finder/README.md` (build, publish, use)

**Interfaces:**
- Consumes: Task 5 `Packets`, `NetSettings`, `Found`, `Ack`, `FoundTable`, `DiscClient`, `Adapters`.

- [ ] **Step 1: The project.** `Sbiot.Finder.csproj`:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>WinExe</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <UseWindowsForms>true</UseWindowsForms>
    <EnableWindowsTargeting>true</EnableWindowsTargeting>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <AssemblyName>SbiotFinder</AssemblyName>
    <RootNamespace>Sbiot.Finder</RootNamespace>
    <IncludeNativeLibrariesForSelfExtract>true</IncludeNativeLibrariesForSelfExtract>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="../Sbiot.Finder.Protocol/Sbiot.Finder.Protocol.csproj" />
  </ItemGroup>
</Project>
```

Then `dotnet sln add Sbiot.Finder`.

- [ ] **Step 2: The window.** `Program.cs`:

```csharp
namespace Sbiot.Finder;

static class Program
{
    [STAThread]
    static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }
}
```

`MainForm.cs`:

```csharp
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using Sbiot.Finder.Protocol;

namespace Sbiot.Finder;

/// <summary>SBIOT Finder (Plan 12, spec §3): pick the adapter, Search, act on the selected gateway.</summary>
sealed class MainForm : Form
{
    readonly ComboBox adapter = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 360 };
    readonly Button search = new() { Text = "Search", AutoSize = true };
    readonly Button network = new() { Text = "Network…", AutoSize = true, Enabled = false };
    readonly Button restart = new() { Text = "Restart", AutoSize = true, Enabled = false };
    readonly Button factory = new() { Text = "Factory reset", AutoSize = true, Enabled = false };
    readonly ListView list = new() { View = View.Details, FullRowSelect = true, MultiSelect = false, Dock = DockStyle.Fill };
    readonly Label status = new() { Dock = DockStyle.Bottom, Height = 24, TextAlign = ContentAlignment.MiddleLeft };
    readonly FoundTable table = new();
    IReadOnlyList<(string Name, IPAddress Address)> adapters = [];

    public MainForm()
    {
        Text = "SBIOT Finder";
        Width = 900;
        Height = 480;
        foreach (var (h, w) in new[] { ("MAC", 130), ("SN", 110), ("IP", 110), ("Hostname", 130), ("Model", 100), ("Firmware", 80) })
            list.Columns.Add(h, w);
        var bar = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 40, Padding = new Padding(6) };
        bar.Controls.AddRange([new Label { Text = "Adapter:", AutoSize = true, Padding = new Padding(0, 6, 0, 0) }, adapter, search, network, restart, factory]);
        Controls.Add(list);
        Controls.Add(bar);
        Controls.Add(status);
        adapters = Adapters.List();
        foreach (var (name, addr) in adapters) adapter.Items.Add($"{name} — {addr}");
        if (adapter.Items.Count > 0) adapter.SelectedIndex = 0;
        search.Click += async (_, _) => await Search();
        list.SelectedIndexChanged += (_, _) => network.Enabled = restart.Enabled = factory.Enabled = Selected is not null;
        list.DoubleClick += (_, _) => OpenWeb();
        network.Click += async (_, _) => await SetNetwork();
        restart.Click += async (_, _) => await Act("restart", null, "Restart");
        factory.Click += async (_, _) =>
        {
            if (MessageBox.Show(this, "Factory reset erases every setting of this gateway (network, MQTT, Edge, certificates, web password) and restarts it. Continue?",
                    "Factory reset", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) == DialogResult.Yes)
                await Act("factory", null, "Factory reset");
        };
    }

    Found? Selected => list.SelectedItems.Count == 1 ? (Found)list.SelectedItems[0].Tag! : null;

    DiscClient? Client()
    {
        if (adapter.SelectedIndex < 0) { status.Text = "No network adapter with an IPv4 address."; return null; }
        return new DiscClient(adapters[adapter.SelectedIndex].Address);
    }

    void ShowRow(Found f)
    {
        table.Merge(f);
        list.BeginUpdate();
        list.Items.Clear();
        foreach (var r in table.Rows)
            list.Items.Add(new ListViewItem([r.Mac, r.Sn, r.EthIp, r.Hostname, r.Model, r.Fw]) { Tag = r });
        list.EndUpdate();
    }

    // Every handler catches the network errors itself (an async void handler must not throw, Codex plan r1).
    async Task Search()
    {
        search.Enabled = false;
        try
        {
            using var c = Client();
            if (c is null) return;
            status.Text = "Searching…";
            table.Clear();
            list.Items.Clear();
            var found = await c.SearchAsync(TimeSpan.FromSeconds(2));
            foreach (var f in found) ShowRow(f);
            status.Text = $"{found.Count} gateway(s) found.";
        }
        catch (Exception e) when (e is SocketException or ObjectDisposedException or InvalidOperationException)
        {
            status.Text = $"Search failed on this adapter: {e.Message}. Choose another adapter or try again.";
        }
        finally { search.Enabled = true; }
    }

    void OpenWeb()
    {
        if (Selected is not { } f) return;
        try { Process.Start(new ProcessStartInfo($"http://{f.EthIp}:{f.Web}/") { UseShellExecute = true }); }
        catch (System.ComponentModel.Win32Exception e) { status.Text = $"Could not open the browser: {e.Message}"; }
    }

    async Task SetNetwork()
    {
        if (Selected is not { } f) return;
        using var dlg = new NetworkForm(f.Net);
        if (dlg.ShowDialog(this) == DialogResult.OK)
            await Act("set", dlg.Result, "Network settings");
    }

    async Task Act(string op, NetSettings? net, string what)
    {
        if (Selected is not { } f) return;
        Enabled = false;
        bool sent = false;
        try
        {
            using var c = Client();
            if (c is null) return;
            status.Text = $"{what}: sending…";
            sent = true;  // from here a failure may come after the gateway got the command
            var ack = await c.CommandAsync(op, f.Mac, net, TimeSpan.FromSeconds(10));
            if (ack is null) { status.Text = $"{what}: outcome unknown (no reply). Press Search to check the gateway."; return; }
            if (!ack.Ok)
            {
                status.Text = ack.Err == "bad" ? $"{what}: the gateway refused the settings." : $"{what}: the gateway could not save (flash error).";
                return;
            }
            status.Text = $"{what}: done. Waiting for the gateway to restart (up to 2 minutes)…";
            var back = await c.WaitBackAsync(f.Mac, f.Boot, TimeSpan.FromSeconds(120), TimeSpan.FromSeconds(3));
            if (back is null) { status.Text = $"{what}: the gateway has not come back yet. Press Search later."; return; }
            ShowRow(back);
            status.Text = $"{what}: the gateway is back at {back.EthIp}.";
        }
        catch (Exception e) when (e is SocketException or ObjectDisposedException or InvalidOperationException)
        {
            status.Text = sent ? $"{what}: outcome unknown ({e.Message}). Press Search to check the gateway; nothing was sent again."
                               : $"{what}: the adapter failed ({e.Message}). Nothing was sent.";
        }
        finally { Enabled = true; }
    }
}
```

`NetworkForm.cs`:

```csharp
using Sbiot.Finder.Protocol;

namespace Sbiot.Finder;

/// <summary>The Ethernet settings of one gateway, filled from its saved values (`net` of found).</summary>
sealed class NetworkForm : Form
{
    readonly RadioButton dhcp = new() { Text = "DHCP", AutoSize = true };
    readonly RadioButton stat = new() { Text = "Static", AutoSize = true };
    readonly TextBox ip = new(), mask = new(), gw = new(), dns1 = new(), dns2 = new();
    readonly CheckBox manualDns = new() { Text = "Manual DNS", AutoSize = true };
    public NetSettings Result { get; private set; }

    public NetworkForm(NetSettings n)
    {
        Text = "Network settings";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        StartPosition = FormStartPosition.CenterParent;
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        Result = n;
        (n.IpMode == 1 ? stat : dhcp).Checked = true;
        ip.Text = n.Ip; mask.Text = n.Netmask; gw.Text = n.Gateway; dns1.Text = n.Dns1; dns2.Text = n.Dns2;
        manualDns.Checked = n.DnsMode == 1;
        var grid = new TableLayoutPanel { ColumnCount = 2, AutoSize = true, Padding = new Padding(10) };
        var mode = new FlowLayoutPanel { AutoSize = true };
        mode.Controls.AddRange([dhcp, stat]);
        void Row(string label, Control c) { grid.Controls.Add(new Label { Text = label, AutoSize = true, Padding = new Padding(0, 6, 0, 0) }); grid.Controls.Add(c); c.Width = 160; }
        Row("Address", mode);
        Row("IP address", ip);
        Row("Subnet mask", mask);
        Row("Gateway", gw);
        Row("DNS", manualDns);
        Row("DNS 1", dns1);
        Row("DNS 2", dns2);
        var ok = new Button { Text = "Save", DialogResult = DialogResult.None };
        var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel };
        var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Bottom };
        buttons.Controls.AddRange([cancel, ok]);
        grid.Controls.Add(new Label());
        grid.Controls.Add(buttons);
        Controls.Add(grid);
        AcceptButton = ok;
        CancelButton = cancel;
        void Sync()
        {
            ip.Enabled = mask.Enabled = gw.Enabled = stat.Checked;
            if (stat.Checked) manualDns.Checked = true;  // a static address always uses the manual DNS (spec §7.1)
            manualDns.Enabled = dhcp.Checked;
            dns1.Enabled = dns2.Enabled = manualDns.Checked;
        }
        dhcp.CheckedChanged += (_, _) => Sync();
        manualDns.CheckedChanged += (_, _) => Sync();
        Sync();
        ok.Click += (_, _) =>
        {
            var r = new NetSettings(stat.Checked ? 1 : 0, ip.Text.Trim(), mask.Text.Trim(), gw.Text.Trim(), manualDns.Checked ? 1 : 0, dns1.Text.Trim(), dns2.Text.Trim());
            if (r.Validate() is { } err) { MessageBox.Show(this, err, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
            Result = r;
            DialogResult = DialogResult.OK;
        };
    }
}
```

- [ ] **Step 3: Build and publish.** Run, from `stm32/tools/finder`:

```bash
set -e -o pipefail  # a failed step stops the block: nothing stale is copied
rm -rf out
dotnet build Sbiot.Finder 2>&1 | tail -3
dotnet publish Sbiot.Finder -c Release -r win-x64 --self-contained -p:PublishSingleFile=true -o out 2>&1 | tail -3
ls -la out
rm -rf /mnt/c/Users/Public/sbiot/finder && mkdir -p /mnt/c/Users/Public/sbiot/finder && cp out/SbiotFinder.exe /mnt/c/Users/Public/sbiot/finder/
dotnet test 2>&1 | tail -3
```

Expected: each command exits 0 with 0 warnings; `out` holds `SbiotFinder.exe` (tens of MB) and no native `.dll` beside it (only a `.pdb` at most); the exe is copied **alone** into a clean folder, which is how the user runs it.

- [ ] **Step 4: README.** `stm32/tools/finder/README.md`: what the tool does; the build and publish commands above; that the PC and the gateways must share a switch (broadcast does not cross routers); no password (the user's decision); the gateway port UDP 48600 must not be blocked by the Windows firewall (Windows asks on the first run: allow on private networks).

- [ ] **Step 5: Try it on Windows.** Ask the user to run `C:\Users\Public\sbiot\finder\SbiotFinder.exe` (alone in its folder) with the board: pick the LAN adapter, Search, double-click (Web UI opens), Network… to `10.10.0.7/24` gateway `10.10.0.1`, wait for it to come back at 10.10.0.7, then back to `192.168.0.7`, then Restart. Ledger what the user reports. Do not use Factory reset on the bench board.

- [ ] **Step 6: Commit.**

```bash
git add stm32/tools/finder
git commit -m "stm32 finder: the WinForms window, SbiotFinder.exe (Plan 12 task 6)"
```

---

## After the tasks

- Codex code review of the whole branch (mandatory before the PR), prompt and reply committed under `stm32/docs/review/`.
- PR flow as in the earlier plans (subtree split to `h750/discovery`, PR on `chuoichin1012-ctrl/SBIOT-H750`).
