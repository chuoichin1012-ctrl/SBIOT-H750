# Plan 01: Host core and web UI copy, implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create the separate `stm32/` tree with the reused hardware-free C core (`proto`, `cfgmodel`) changed to the STM32 board's scope and passing host tests, plus a separate `stm32/web` copy of the web UI changed to match.

**Architecture:**
- The ESP32 tree (`firmware/`, root `src/`) is the source of the copy and is **never modified**.
- Files are copied once into `stm32/firmware/components/` and `stm32/web/`, then edited directly. There is no shared code and no OSAL (spec D2).
- cJSON is vendored into `stm32/firmware/third_party/cjson`, so host tests no longer need `IDF_PATH`.

**Tech Stack:** C11, gcc with ASan/UBSan, GNU make, cJSON 1.7.19, Python 3, Node (Vite 8, Preact, TypeScript), from the existing `package-lock.json`.

**Spec:** `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` (§2 D2/D4/D6/D12, §4, §10.2, §10.3, §13). **Roadmap:** `stm32/docs/plans/2026-10-14-00-roadmap.md`.

## Global Constraints

- Do not modify anything under `firmware/` or the root `src/`, `public/`, `index.html`, `package*.json`, `vite.config.ts`, `tsconfig.json` (D2: separate trees).
- Board environment used by every host test: `uart_count = 2, do_count = 2, di_count = 2, ai_count = 2` (§3).
- Edge limit: `#define MAX_NODES 100`. The error text is exactly `At most 100 points across all devices.` (D6).
- Keys removed from the STM32 schema and defaults: `rfc2217` (port), `telnet` and `telnetPort` (system) (D12).
- TLS (`tls`, `tlsVerify`) is valid on **every** port, including `port2` (D4).
- Host build flags are unchanged from the ESP32 tree: `-std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined`.
- cJSON is the ESP-IDF copy, version 1.7.19, kept with its `LICENSE`.
- Every command below runs from the repository root `/home/linh/projects/SBIOT-Gateway` unless it says otherwise.

## Review Focus

1. A config saved by older firmware that still contains `telnet`, `telnetPort` or `rfc2217` must still load at boot (`cfg_load`). Those keys disappear, and every valid key (`hostname`, `baudRate`, `tls`) is kept. Pinned in Task 2, `test_stm32_scope`, for both `system` and `port1`.
2. Values that exist only because the board has two ports must be **accepted**, not just range-checked: `websocketTarget` = 3 (LOG), `edgeReport.channel` = 2, an Edge device with `uart` = 2, IO points `AI2`/`DI2`. Pinned in Task 2, `test_two_port_values` and `test_stm32_scope`.
3. The UART2 socket page must show the TLS fields and the certificate upload for `port2` in TCP client mode. Pinned in Task 3, Step 6 (mock check).
4. The Edge page must count against 100, not 300. Pinned in Task 3, Step 6 (mock check `0 of 100 points used`) and Step 1 (grep).
5. The ESP32 tree must stay byte-for-byte unchanged: every source, config, tool and managed-component file. Only the generated build directories are excluded. Those paths are untracked, so `git status` cannot see edits there. Pinned by the checksum snapshot in Task 1, Step 0, compared in Task 3, Step 8.

---

### Task 1: Scaffold `stm32/firmware` with the reused core and green host tests

**Files:**
- Create: `stm32/firmware/third_party/cjson/cJSON.c`, `cJSON.h`, `LICENSE` (copied from ESP-IDF)
- Create: `stm32/firmware/components/proto/` (copied from `firmware/components/proto/`, without `rfc2217.c` and `include/rfc2217.h`)
- Create: `stm32/firmware/components/cfgmodel/` (copied from `firmware/components/cfgmodel/`)
- Create: `stm32/firmware/test/host/test_cfgmodel.c`, `test_proto.c` (copied), `Makefile` (new)
- Create: `stm32/README.md`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `stm32/firmware/components/cfgmodel/include/cfgmodel.h`, with the same API as the ESP32 copy: `cfg_known`, `cfg_names`, `cfg_default`, `cfg_validate`, `cfg_load`, `cfg_env_t {uart_count, do_count, di_count, ai_count}`.
  - `stm32/firmware/components/proto/include/{edgecore,ipacl,modbus,mqttutil,rwprot}.h`.
  - `make -C stm32/firmware/test/host`.

- [ ] **Step 0: Snapshot the ESP32 tree**

These paths are not tracked by git yet, so `git status` cannot show changes in them. Take checksums now, and Task 3 compares against them at the end.

```bash
find firmware src public index.html package.json package-lock.json vite.config.ts tsconfig.json -type f \
     -not -path 'firmware/build/*' -not -path 'firmware/build-qemu/*' -not -path 'firmware/test/host/build/*' \
     -not -path '*/__pycache__/*' | sort | xargs sha256sum > .git/esp-tree-before.sha256
wc -l .git/esp-tree-before.sha256
```

Expected: a line count of a few hundred files. The file lives inside `.git/`, so it is never committed.

**What this covers:**
- every ESP32 source, config and tool file, including `firmware/managed_components/` (downloaded component sources), `sdkconfig*`, `partitions.csv` and `dependencies.lock`;
- the root UI files.

**Excluded, because they only hold generated output:** `firmware/build/`, `firmware/build-qemu/`, `firmware/test/host/build/` and `__pycache__/`. Nothing in this plan writes to them.

- [ ] **Step 1: Copy the sources**

```bash
mkdir -p stm32/firmware/third_party/cjson stm32/firmware/components stm32/firmware/test/host
cp ~/esp/esp-idf/components/json/cJSON/cJSON.c ~/esp/esp-idf/components/json/cJSON/cJSON.h \
   ~/esp/esp-idf/components/json/cJSON/LICENSE stm32/firmware/third_party/cjson/
cp -r firmware/components/proto firmware/components/cfgmodel stm32/firmware/components/
rm stm32/firmware/components/proto/rfc2217.c stm32/firmware/components/proto/include/rfc2217.h
rm stm32/firmware/components/proto/CMakeLists.txt stm32/firmware/components/cfgmodel/CMakeLists.txt
cp firmware/test/host/test_cfgmodel.c firmware/test/host/test_proto.c stm32/firmware/test/host/
grep -n "CJSON_VERSION_PATCH" stm32/firmware/third_party/cjson/cJSON.h
```

Expected: the last command prints `#define CJSON_VERSION_PATCH 19`. The two `CMakeLists.txt` files are ESP-IDF component files; Plan 02 writes the STM32 CMake.

- [ ] **Step 2: Write the host test Makefile**

Create `stm32/firmware/test/host/Makefile`:

```make
# Host tests for the hardware-free modules of the STM32 firmware: make -C stm32/firmware/test/host
CJSON := ../../third_party/cjson
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined \
          -I../../components/cfgmodel/include -I../../components/proto/include -I$(CJSON)
BUILD := build

all: $(BUILD)/test_cfgmodel $(BUILD)/test_proto
	$(BUILD)/test_cfgmodel
	$(BUILD)/test_proto

$(BUILD)/test_cfgmodel: test_cfgmodel.c ../../components/cfgmodel/cfgmodel.c ../../components/proto/ipacl.c $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_proto: test_proto.c $(wildcard ../../components/proto/*.c) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
```

(Recipe lines start with a tab.)

- [ ] **Step 3: Run the tests to see the expected failure**

Run: `make -C stm32/firmware/test/host`
Expected: the `test_proto` build fails with `fatal error: rfc2217.h: No such file or directory`. RFC 2217 was removed (D12), but its test is still there.

- [ ] **Step 4: Remove the RFC 2217 test**

In `stm32/firmware/test/host/test_proto.c`:
- delete the line `#include "rfc2217.h"`;
- delete the whole function that starts at `static void test_rfc2217(void)` and ends just before the line `static bool m(const char *f, const char *t) { return mqtt_topic_match(f, t, strlen(t)); }`;
- in `main()`, delete the line `    test_rfc2217();`.

The same edit as a script, so it can be checked:

```bash
python3 - <<'EOF'
p = 'stm32/firmware/test/host/test_proto.c'
s = open(p).read()
s = s.replace('#include "rfc2217.h"\n', '', 1)
a = s.index('static void test_rfc2217(void)')
b = s.index('static bool m(const char *f')
s = s[:a] + s[b:]
s = s.replace('    test_rfc2217();\n', '', 1)
open(p, 'w').write(s)
EOF
grep -c rfc2217 stm32/firmware/test/host/test_proto.c
```

Expected: `0`.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `make -C stm32/firmware/test/host`
Expected: the output ends with `cfgmodel: all checks passed` and `proto: all tests passed`, and make exits 0. The tests still use the ESP32 one-UART environment, which Task 2 changes.

- [ ] **Step 6: Ignore build output and add the tree README**

Append to `.gitignore`:

```
stm32/firmware/test/host/build
stm32/firmware/build
stm32/firmware/build-*
stm32/firmware/components/webapi/www
```

The `www` directory is generated by `npm --prefix stm32/web run build:firmware` (Task 3) and regenerated by the firmware build (Plan 06), so it is not committed.

Create `stm32/README.md`:

```markdown
# SBIOT-H750 (STM32H750VBT6)

Firmware and web UI for the STM32H750 board (`docs/Job2.PDF`). This tree is separate from the ESP32 SBIOT-GW100 code in `firmware/` and `src/`, and the two are not kept in sync.

- `docs/specs/`: the design (approved by Codex). Start there.
- `docs/plans/`: the roadmap and the implementation plans.
- `docs/review/`: Codex reviews and how each finding was handled.
- `firmware/`: firmware sources. Host tests: `make -C stm32/firmware/test/host`.
- `web/`: the STM32 web UI (`npm --prefix stm32/web run build`).
```

- [ ] **Step 7: Commit**

```bash
git add .gitignore stm32/README.md stm32/firmware/third_party stm32/firmware/components stm32/firmware/test/host/Makefile \
        stm32/firmware/test/host/test_cfgmodel.c stm32/firmware/test/host/test_proto.c
git commit -m "stm32: copy proto and cfgmodel with host tests, vendor cJSON 1.7.19, drop RFC 2217"
```

---

### Task 2: STM32 board scope in `cfgmodel`

**Files:**
- Modify: `stm32/firmware/components/cfgmodel/cfgmodel.c` (the `port_fields` and `system_fields` tables, `DEF_PORT`, `DEF_SYSTEM`, `MAX_NODES`, the limit message)
- Modify: `stm32/firmware/components/cfgmodel/include/cfgmodel.h` (the header comment only)
- Test: `stm32/firmware/test/host/test_cfgmodel.c`

**Interfaces:**
- Consumes: the Task 1 tree.
- Produces: the schema with `port1`/`port2` and no `rfc2217`, `system` without `telnet`/`telnetPort`, and Edge limited to 100 points. Later plans call `cfg_validate`/`cfg_load` with `cfg_env_t{2,2,2,2}`.

- [ ] **Step 1: Switch the tests to the STM32 board and write the failing tests**

In `stm32/firmware/test/host/test_cfgmodel.c`, make these exact replacements:

```bash
python3 - <<'EOF'
p = 'stm32/firmware/test/host/test_cfgmodel.c'
s = open(p).read()
def R(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)
R('static const cfg_env_t ENV = {.uart_count = 1, .do_count = 2, .di_count = 1, .ai_count = 1};',
  'static const cfg_env_t ENV = {.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};')
R('CHECK(n == 11, "expected 11 configs with one UART, got %zu", n);',
  'CHECK(n == 12, "expected 12 configs with two UARTs, got %zu", n);')
R('''    CHECK(!cfg_known(&ENV, "port2"), "port2 must not exist with one UART");
    cfg_env_t two = ENV;
    two.uart_count = 2;
    CHECK(cfg_known(&two, "port2"), "port2 must exist with two UARTs");''',
  '''    CHECK(cfg_known(&ENV, "port2"), "port2 must exist with two UARTs");
    cfg_env_t one = ENV;
    one.uart_count = 1;
    CHECK(!cfg_known(&one, "port2"), "port2 must not exist with one UART");''')
R('bad("system", "{\\"websocketTarget\\":3}", "websocketTarget must be 0 to 2");',
  'bad("system", "{\\"websocketTarget\\":4}", "websocketTarget must be 0 to 3");')
R('bad("edgeReport", "{\\"channel\\":2}", "channel must be 0 to 1");',
  'bad("edgeReport", "{\\"channel\\":3}", "channel must be 0 to 2");')
R('bad("edgeDevices", "[{\\"name\\":\\"a\\",\\"uart\\":2}]", "uart must be 1 to 1");',
  'bad("edgeDevices", "[{\\"name\\":\\"a\\",\\"uart\\":3}]", "uart must be 1 to 2");')
open(p, 'w').write(s)
EOF
```

Then insert these two functions directly above `int main(void)`:

```c
/** Values that exist only because this board has two ports. */
static void test_two_port_values(void)
{
    cJSON *o = ok("system", "{\"websocketTarget\":3}");
    CHECK(o && cJSON_GetNumberValue(cJSON_GetObjectItem(o, "websocketTarget")) == 3, "LOG target is 3 with two UARTs");
    cJSON_Delete(o);
    o = ok("edgeReport", "{\"channel\":2}");
    CHECK(o && cJSON_GetNumberValue(cJSON_GetObjectItem(o, "channel")) == 2, "report channel 2 = UART2 socket");
    cJSON_Delete(o);
    o = ok("edgeDevices", "[{\"name\":\"m2\",\"uart\":2}]");
    CHECK(o && cJSON_GetNumberValue(cJSON_GetObjectItem(cJSON_GetArrayItem(o, 0), "uart")) == 2, "device on UART2");
    cJSON_Delete(o);
}

/** STM32 board: the keys this firmware dropped, and the 100-point Edge limit. */
static void test_stm32_scope(void)
{
    cJSON *p = ok("port2", "{\"rfc2217\":1,\"tls\":1,\"workMode\":1}");
    if (p) {
        CHECK(!cJSON_GetObjectItem(p, "rfc2217"), "rfc2217 must be dropped");
        CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(p, "tls")) == 1, "TLS must be kept on port2");
        cJSON_Delete(p);
    }
    cJSON *s = ok("system", "{\"telnet\":1,\"telnetPort\":23}");
    if (s) {
        CHECK(!cJSON_GetObjectItem(s, "telnet"), "telnet must be dropped");
        CHECK(!cJSON_GetObjectItem(s, "telnetPort"), "telnetPort must be dropped");
        cJSON_Delete(s);
    }
    cJSON *d = cfg_default(&ENV, "port1");
    CHECK(d && !cJSON_GetObjectItem(d, "rfc2217"), "port default must not carry rfc2217");
    cJSON_Delete(d);
    d = cfg_default(&ENV, "system");
    CHECK(d && !cJSON_GetObjectItem(d, "telnet") && !cJSON_GetObjectItem(d, "telnetPort"), "system default must not carry telnet");
    cJSON_Delete(d);

    // Copies stored by older firmware still load: the dropped keys disappear, valid ones are kept.
    cJSON *old = cJSON_Parse("{\"telnet\":1,\"telnetPort\":2323,\"hostname\":\"gw\"}");
    char err[160] = "";
    cJSON *ld = cfg_load(&ENV, "system", old, err, sizeof err);
    CHECK(ld && !cJSON_GetObjectItem(ld, "telnet") && !cJSON_GetObjectItem(ld, "telnetPort") &&
              strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(ld, "hostname")), "gw") == 0,
          "stored system with telnet must load without it: %s", err);
    cJSON_Delete(ld);
    cJSON_Delete(old);
    old = cJSON_Parse("{\"rfc2217\":1,\"baudRate\":9600,\"tls\":1}");
    err[0] = 0;
    ld = cfg_load(&ENV, "port1", old, err, sizeof err);
    CHECK(ld && !cJSON_GetObjectItem(ld, "rfc2217") && cJSON_GetNumberValue(cJSON_GetObjectItem(ld, "baudRate")) == 9600 &&
              cJSON_GetNumberValue(cJSON_GetObjectItem(ld, "tls")) == 1,
          "stored port1 with rfc2217 must load without it and keep the rest: %s", err);
    cJSON_Delete(ld);
    cJSON_Delete(old);

    // Both analog and digital inputs exist on this board.
    cJSON *io = ok("edgeDevices", "[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"ioPoint\":\"AI2\"},{\"ioPoint\":\"DI2\"}]}]");
    cJSON_Delete(io);
    bad("edgeDevices", "[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"ioPoint\":\"AI3\"}]}]", "IO channel \"AI3\" does not exist");

    // 100 points pass, 101 fail with the user-facing message.
    for (int total = 100; total <= 101; total++) {
        char json[16384];
        size_t o = (size_t)snprintf(json, sizeof json, "[{\"name\":\"m\",\"nodes\":[");
        for (int i = 0; i < total; i++)
            o += (size_t)snprintf(json + o, sizeof json - o, "%s{\"name\":\"p%d\",\"reg\":%d}", i ? "," : "", i, i);
        snprintf(json + o, sizeof json - o, "]}]");
        if (total == 100) {
            cJSON *e = ok("edgeDevices", json);
            cJSON_Delete(e);
        } else {
            bad("edgeDevices", json, "At most 100 points across all devices.");
        }
    }
}
```

Add the two calls as the first lines inside `main()`:

```c
    test_stm32_scope();
    test_two_port_values();
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make -C stm32/firmware/test/host`
Expected: exits non-zero with exactly these failures, nothing else:
- `rfc2217 must be dropped`
- `telnet must be dropped`
- `telnetPort must be dropped`
- `port default must not carry rfc2217`
- `system default must not carry telnet`
- `stored system with telnet must load without it`
- `stored port1 with rfc2217 must load without it and keep the rest`
- an `edgeDevices accepted [...]` line, plus `message "" lacks "At most 100 points across all devices."`

That is `9 check(s) failed`. The two-port value checks already pass, because they depend only on the environment. They are there to stop anyone from narrowing the ranges again.

- [ ] **Step 3: Change the schema**

```bash
python3 - <<'EOF'
p = 'stm32/firmware/components/cfgmodel/cfgmodel.c'
s = open(p).read()
def R(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)
R(' INT("packetInterval", 0, 255), INT("rfc2217", 0, 1),', ' INT("packetInterval", 0, 255),')
R('"\\"rfc2217\\":0,\\"serialHeartbeat\\":false,', '"\\"serialHeartbeat\\":false,')
R('    INT("telnet", 0, 1), INT("telnetPort", 1, 65535), INT("websocketPort", 1, 65535),', '    INT("websocketPort", 1, 65535),')
R('\\"webPort\\":80,\\"telnet\\":0,\\"telnetPort\\":2323,\\"websocketPort\\":6432,', '\\"webPort\\":80,\\"websocketPort\\":6432,')
R('#define MAX_NODES 300', '#define MAX_NODES 100')
R('"At most 300 points across all devices."', '"At most 100 points across all devices."')
open(p, 'w').write(s)
EOF
grep -c "rfc2217\|telnet" stm32/firmware/components/cfgmodel/cfgmodel.c
```

Expected: `0`.

In `stm32/firmware/components/cfgmodel/include/cfgmodel.h`, replace the header comment line `// (firmware/test/host). Keep it in step with the web field definitions.` with `// (stm32/firmware/test/host). Keep it in step with stm32/web/src/pages.`. Also replace `// UI's FieldDefs (src/pages/*.tsx) and a default value. This module is plain` with `// UI's FieldDefs (stm32/web/src/pages/*.tsx) and a default value. This module is plain`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make -C stm32/firmware/test/host`
Expected: `cfgmodel: all checks passed` and `proto: all tests passed`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add stm32/firmware/components/cfgmodel stm32/firmware/test/host/test_cfgmodel.c
git commit -m "stm32 cfgmodel: two UARTs, 2 DI/2 AI, no telnet or RFC 2217, Edge limited to 100 points"
```

---

### Task 3: `stm32/web`, the separate web UI copy, and the schema check

**Files:**
- Create: `stm32/web/` (copied from root `src/`, `public/`, `index.html`, `package.json`, `package-lock.json`, `tsconfig.json`, `vite.config.ts`)
- Modify (in the copy only):
  - `stm32/web/src/pages/System.tsx` (telnet fields)
  - `stm32/web/src/pages/Port.tsx` (rfc2217 field, TLS on every port)
  - `stm32/web/src/lib/edge.ts` (`MAX_NODES`)
  - `stm32/web/src/pages/Network.tsx` (EC600U)
  - `stm32/web/src/lib/api.ts` (scope comment)
  - `stm32/web/src/lib/mock.ts` (STM32 device)
  - `stm32/web/package.json` (name, `build:firmware` paths)
- Create: `stm32/firmware/tools/check_schema.py`, `stm32/firmware/tools/embed_web.py`

**Interfaces:**
- Consumes: the Task 2 schema in `stm32/firmware/components/cfgmodel/cfgmodel.c`.
- Produces:
  - `npm --prefix stm32/web run build`;
  - `npm --prefix stm32/web run build:firmware`, which writes `stm32/firmware/components/webapi/www/` and is used by Plan 06;
  - `python3 stm32/firmware/tools/check_schema.py`.

- [ ] **Step 1: Copy the UI and install dependencies**

```bash
mkdir -p stm32/web
cp -r src public index.html package.json package-lock.json tsconfig.json vite.config.ts stm32/web/
npm --prefix stm32/web ci
grep -n "MAX_NODES = " stm32/web/src/lib/edge.ts
```

Expected: `npm ci` finishes without errors. The grep prints `export const MAX_NODES = 300; // across all slaves`. That is the value about to change.

- [ ] **Step 2: Apply the STM32 changes**

```bash
python3 - <<'EOF'
def edit(p, pairs):
    s = open(p).read()
    for a, b in pairs:
        assert s.count(a) == 1, (p, a)
        s = s.replace(a, b)
    open(p, 'w').write(s)
W = 'stm32/web/'
edit(W + 'src/pages/System.tsx', [(
    "    { id: 'telnet', label: 'Telnet', kind: 'select', options: ON_OFF },\n"
    "    { id: 'telnetPort', label: 'Telnet port', kind: 'number', min: 1, max: 65535, show: (v) => Number(v.telnet) === 1 },\n", "")])
edit(W + 'src/pages/Port.tsx', [
    ("    { id: 'rfc2217', label: 'Baud rate sync (RFC 2217)', kind: 'select', options: ON_OFF },\n", ""),
    (" * the reference gateway. TLS exists on UART1 only (one TLS context on the MCU).\n",
     " * the reference gateway. TLS is available on every UART.\n"),
    ("function socketDefs(port: number): FieldDef[] {\n  const tlsMode = (v: Values) => port === 1 && is(TCP_CLIENT, HTTP_CLIENT)(v);",
     "function socketDefs(): FieldDef[] {\n  const tlsMode = is(TCP_CLIENT, HTTP_CLIENT);"),
    ("    // TLS: TCP client and HTTP client (HTTPS), UART1 only.\n",
     "    // TLS: TCP client and HTTP client (HTTPS), on every UART.\n"),
    ("  const socket = useMemo(() => socketDefs(n), [n]);", "  const socket = useMemo(() => socketDefs(), []);"),
])
edit(W + 'src/lib/edge.ts', [("export const MAX_NODES = 300; // across all slaves", "export const MAX_NODES = 100; // across all slaves")])
edit(W + 'src/pages/Network.tsx', [
    ("/** Quectel EC800 (LTE Cat 1, single SIM) over PPP. */", "/** Quectel EC600U (LTE Cat 1, single SIM) over PPP. */"),
    ('<Panel title="Cellular (EC800 LTE)">', '<Panel title="Cellular (EC600U LTE)">'),
])
edit(W + 'src/lib/api.ts', [(
    "/** scope: `mqtt` or `port1`; kind: server CA, client cert, client key. */",
    "/** scope: `mqtt`, `port1` or `port2`; kind: server CA, client cert, client key. */")])
edit(W + 'src/lib/mock.ts', [
    ("model: 'SBIOT-GW100', firmware: '0.1.0', hardware: 'ESP32-WROVER-E', portmax: 1,",
     "model: 'SBIOT-H750', firmware: '0.1.0', hardware: 'STM32H750VBT6', portmax: 2,"),
    ("io: { do: 2, di: 2, ai: 1, ao: 0 },", "io: { do: 2, di: 2, ai: 2, ao: 0 },"),
    (" packetInterval: 0, rfc2217: 1,", " packetInterval: 0,"),
    ("  port1: port(1),\n", "  port1: port(1),\n  port2: port(2),\n"),
    (" telnet: 0, telnetPort: 2323,", ""),
    ("ai: [12500], ao: []", "ai: [12500, 8000], ao: []"),
    ("os: 'FreeRTOS (ESP-IDF v5.5.5)'", "os: 'FreeRTOS (STM32H750)'"),
    ("      { state: 'LISTEN', tx: 1820 + t * 16, rx: 960 + t * 8 },\n",
     "      { state: 'LISTEN', tx: 1820 + t * 16, rx: 960 + t * 8 },\n      { state: 'CONNECTED(1)', tx: 400 + t * 4, rx: 220 + t * 2 },\n"),
])
edit(W + 'package.json', [
    ('"name": "sbiot-gateway-web"', '"name": "sbiot-h750-web"'),
    ('python3 firmware/tools/embed_web.py dist-firmware firmware/components/webapi/www',
     'python3 ../firmware/tools/embed_web.py dist-firmware ../firmware/components/webapi/www'),
])
EOF
grep -rn "telnet\|rfc2217\|EC800\|UART1 only" stm32/web/src | wc -l
```

Expected: `0`.

- [ ] **Step 3: Typecheck and build the UI**

Run: `npm --prefix stm32/web run build`
Expected: `tsc --noEmit` reports no errors, then Vite prints `✓ built in …` with `dist/assets/index-*.js` at about 115 kB (about 43 kB gzipped).

- [ ] **Step 4: Retarget the schema check and copy the embed script**

```bash
mkdir -p stm32/firmware/tools
sed -e 's#CFG = "firmware/components/cfgmodel/cfgmodel.c"#CFG = "stm32/firmware/components/cfgmodel/cfgmodel.c"\nWEB = "stm32/web/src"#' \
    -e 's#open("src/pages/#open(f"{WEB}/pages/#g' \
    -e 's#section("src/pages/#section(f"{WEB}/pages/#g' \
    -e 's#python3 firmware/tools/check_schema.py#python3 stm32/firmware/tools/check_schema.py#' \
    firmware/tools/check_schema.py > stm32/firmware/tools/check_schema.py
cp firmware/tools/embed_web.py stm32/firmware/tools/embed_web.py
grep -c '"src/pages/' stm32/firmware/tools/check_schema.py
```

Expected: `0`. Every page path now goes through `WEB`.

- [ ] **Step 5: Run the schema check**

Run: `python3 stm32/firmware/tools/check_schema.py`
Expected: every line shows `dropped by firmware: -   schema-only: -`, the last line is `consistent`, and the exit code is 0.

Negative check, so the tool is proven to look at the STM32 tree: temporarily add `INT("telnet", 0, 1),` back into `system_fields` in the STM32 `cfgmodel.c`, and re-run. The `system_fields` line must report `schema-only: ['telnet']`. Then revert with `git checkout stm32/firmware/components/cfgmodel/cfgmodel.c`.

- [ ] **Step 6: Check the UI behaviour in mock mode**

Run: `npm --prefix stm32/web run dev:mock`, open the printed URL, and sign in as `admin` / `admin`.

| Page | Expected |
|---|---|
| `#/port/2/socket` | Set Working mode to **TCP client**. The **TLS** select appears. Set it to TLS 1.2: **Certificate check** appears, with no upload rows yet (the default is None). Set Certificate check to **Verify server certificate**: one row, **Server CA certificate**, appears. Set it to **Verify both sides**: **Client certificate** and **Client private key** rows appear too. Upload any PEM file on the CA row and confirm in the browser's network tab that the request goes to `api/cert?scope=port2&kind=ca` |
| `#/port/1/serial` | No "Baud rate sync (RFC 2217)" field |
| `#/system` | No Telnet fields. "Websocket connects to" offers Off, UART1, UART2 and "Gateway log and shell" |
| `#/edge/acquisition` | The counter reads `… of 100 points used` |
| `#/network` | The panel title is `Cellular (EC600U LTE)` |
| `#/io/control` | AI1 and AI2 are listed |

Stop the dev server.

- [ ] **Step 7: Build the firmware bundle once**

Run: `npm --prefix stm32/web run build:firmware`
Expected: exit 0. `stm32/firmware/components/webapi/www/` contains `www_files.c` and `.gz` files, and `git status --porcelain stm32/firmware/components/webapi` prints nothing, because the directory is ignored. Plan 06 consumes this output.

- [ ] **Step 8: Verify the ESP32 tree is untouched, then commit**

```bash
sha256sum --quiet -c .git/esp-tree-before.sha256 && echo "no file changed or deleted"
find firmware src public index.html package.json package-lock.json vite.config.ts tsconfig.json -type f \
     -not -path 'firmware/build/*' -not -path 'firmware/build-qemu/*' -not -path 'firmware/test/host/build/*' \
     -not -path '*/__pycache__/*' | sort \
  | diff <(cut -c67- .git/esp-tree-before.sha256) - && echo "no file added"
```

Expected: `no file changed or deleted` and `no file added`. Any other output names the ESP32 file that was edited, added or deleted: restore it before committing.

```bash
git add stm32/web/src stm32/web/public stm32/web/index.html stm32/web/package.json stm32/web/package-lock.json \
        stm32/web/tsconfig.json stm32/web/vite.config.ts stm32/firmware/tools
git commit -m "stm32/web: separate UI copy for the H750 board; schema check retargeted at the stm32 tree"
```

---

## Plan 01 done when

- `make -C stm32/firmware/test/host` passes.
- `npm --prefix stm32/web run build` passes.
- `python3 stm32/firmware/tools/check_schema.py` prints `consistent`.
- The mock check table in Task 3 Step 6 holds.
- No file under `firmware/` or the root UI changed.
