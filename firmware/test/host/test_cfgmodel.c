// Host unit tests for the config model: build with `make -C test/host`.
// Payloads below are copied from the web UI's mock (src/lib/mock.ts), so a
// failure here means firmware and UI disagree about a field.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cfgmodel.h"

static const cfg_env_t ENV = {.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};
static int failures;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

static cJSON *ok(const char *name, const char *json)
{
    char err[160] = "";
    cJSON *in = cJSON_Parse(json);
    CHECK(in, "%s: test JSON does not parse", name);
    cJSON *out = cfg_validate(&ENV, name, in, NULL, err, sizeof err);
    CHECK(out, "%s rejected: %s", name, err);
    cJSON_Delete(in);
    return out;
}

static void bad(const char *name, const char *json, const char *expect)
{
    char err[160] = "";
    cJSON *in = cJSON_Parse(json);
    cJSON *out = cfg_validate(&ENV, name, in, NULL, err, sizeof err);
    CHECK(!out, "%s accepted %s", name, json);
    CHECK(strstr(err, expect), "%s: message \"%s\" lacks \"%s\"", name, err, expect);
    cJSON_Delete(out);
    cJSON_Delete(in);
}

static void test_defaults_validate(void)
{
    const char *names[16];
    size_t n = cfg_names(&ENV, names, 16);
    CHECK(n == 12, "expected 12 configs with two UARTs, got %zu", n);
    for (size_t i = 0; i < n; i++) {
        cJSON *d = cfg_default(&ENV, names[i]);
        CHECK(d, "default for %s does not parse", names[i]);
        char err[160] = "";
        cJSON *v = cfg_validate(&ENV, names[i], d, NULL, err, sizeof err);
        CHECK(v, "default for %s fails its own schema: %s", names[i], err);
        cJSON_Delete(v);
        cJSON_Delete(d);
    }
    CHECK(cfg_known(&ENV, "port2"), "port2 must exist with two UARTs");
    cfg_env_t one = ENV;
    one.uart_count = 1;
    CHECK(!cfg_known(&one, "port2"), "port2 must not exist with one UART");
}

static void test_coercion_and_unknown_keys(void)
{
    cJSON *o = ok("port1", "{\"baudRate\":\"9600\",\"serialHeartbeat\":\"1\",\"modbusPoll\":0,\"bogus\":5,\"workMode\":1}");
    if (!o) return;
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(o, "baudRate")) == 9600, "baudRate not coerced");
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(o, "serialHeartbeat")), "\"1\" not coerced to true");
    CHECK(cJSON_IsFalse(cJSON_GetObjectItem(o, "modbusPoll")), "0 not coerced to false");
    CHECK(!cJSON_GetObjectItem(o, "bogus"), "unknown key kept");
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(o, "listenPort")) == 23, "missing key not filled from defaults");
    cJSON_Delete(o);
}

static void test_ranges(void)
{
    bad("port1", "{\"baudRate\":300}", "baudRate must be 600 to 230400");
    bad("port1", "{\"workMode\":1.5}", "workMode must be 0 to 4");
    bad("system", "{\"websocketTarget\":4}", "websocketTarget must be 0 to 3");
    bad("system", "{\"password\":\"\"}", "cannot be empty");
    bad("system", "{\"idleRestart\":30}", "Idle restart must be 0 (off) or 60");
    bad("port1", "{\"parity\":3}", "parity must be 0 to 2");
    bad("io", "{\"timer0Output\":2}", "timer0Output must be 0 to 1");
    bad("edgeReport", "{\"channel\":3}", "channel must be 0 to 2");
    bad("mqtt", "{\"host\":5}", "host must be text");
    bad("network", "{\"apnAuth\":4}", "apnAuth must be 0 to 3");
    bad("port1", "{\"flowControl\":1}", "not available on RS485");
    bad("port1", "{\"allowedClients\":\"192.168.1.300\"}", "Allowed clients");
    bad("port1", "{\"allowedClients\":\"10.0.0.0/33\"}", "Allowed clients");
    bad("port1", "{\"allowedClients\":\"1.1.1.1 2.2.2.2 3.3.3.3 4.4.4.4 5.5.5.5 6.6.6.6 7.7.7.7 8.8.8.8 9.9.9.9\"}", "Allowed clients");
}

static void test_lists(void)
{
    cJSON *t = ok("mqttPublish",
                  "[{\"enabled\":true,\"mode\":0,\"topic\":\"site/gw/up\",\"ports\":1,\"qos\":1,\"retain\":false,"
                  "\"ioControl\":false,\"identifier\":\"\",\"delimiter\":\",\"}]");
    CHECK(t && cJSON_GetArraySize(t) == 1, "topic list");
    cJSON_Delete(t);

    cJSON *e = ok("edgeDevices",
                  "[{\"name\":\"device01\",\"source\":\"uart\",\"uart\":1,\"ip\":\"\",\"ethPort\":502,"
                  "\"protocol\":\"modbus\",\"addr\":\"1\",\"poll\":1000,\"mapEn\":false,\"mapAddr\":1,\"merge\":false,"
                  "\"nodes\":[{\"name\":\"node0101\",\"fc\":3,\"reg\":0,\"di\":\"\",\"dtype\":\"uint16\",\"timeout\":200,"
                  "\"onChange\":false,\"range\":1,\"mapEn\":false,\"mapAddr\":0,\"formula\":\"\",\"ioPoint\":\"\","
                  "\"ioReport\":0,\"upper\":0,\"lower\":0},{\"name\":\"node0102\",\"fc\":3,\"reg\":1,\"dtype\":"
                  "\"float32_cdab\",\"formula\":\"=%s*0.1\"}]}]");
    if (e) {
        cJSON *nodes = cJSON_GetObjectItem(cJSON_GetArrayItem(e, 0), "nodes");
        CHECK(cJSON_GetArraySize(nodes) == 2, "nodes kept");
        CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(cJSON_GetArrayItem(nodes, 1), "timeout")) == 200,
              "node default filled");
    }
    cJSON_Delete(e);

    bad("edgeDevices", "[{\"name\":\"a\",\"nodes\":[{\"name\":\"x\"},{\"name\":\"x\"}]}]", "used twice");
    // The built-in IO and virtual devices: fixed names (whatever is sent), one of each, names reserved.
    e = ok("edgeDevices", "[{\"name\":\"anything\",\"source\":\"io\"},{\"name\":\"x\",\"source\":\"virtual\",\"addr\":\"200\"}]");
    CHECK(e && strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(e, 0), "name")), "SBIOT_IO") == 0 &&
              strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(e, 1), "name")), "SBIOT_VIRTUAL") == 0,
          "IO and virtual devices get their fixed names");
    cJSON_Delete(e);
    bad("edgeDevices", "[{\"name\":\"a\",\"source\":\"io\"},{\"name\":\"b\",\"source\":\"io\"}]", "Only one built-in IO");
    bad("edgeDevices", "[{\"name\":\"a\",\"source\":\"virtual\"},{\"name\":\"b\",\"source\":\"virtual\"}]",
        "Only one virtual");
    bad("edgeDevices", "[{\"name\":\"SBIOT_IO\",\"source\":\"uart\"}]", "reserved");
    bad("edgeDevices", "[{\"name\":\"d1\"},{\"name\":\"d1\"}]", "Device name \"d1\" is used twice");
    // IO points are named after their channel; the channel must exist; each once.
    e = ok("edgeDevices", "[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"name\":\"whatever\",\"ioPoint\":\"DO2\"}]}]");
    CHECK(e && strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetArrayItem(e, 0),
                                                                                                      "nodes"), 0), "name")),
                      "DO2") == 0,
          "IO point named after its channel");
    if (e) {
        cJSON *pt = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetArrayItem(e, 0), "nodes"), 0);
        CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(pt, "fc")) == 1 && cJSON_GetNumberValue(cJSON_GetObjectItem(pt, "reg")) == 1 &&
                  strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(pt, "dtype")), "bool") == 0,
              "DO2 sits at coil 1, bool");
    }
    cJSON_Delete(e);
    e = ok("edgeDevices", "[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"name\":\"a\",\"ioPoint\":\"AI1\",\"fc\":3}]},"
                          "{\"name\":\"v\",\"source\":\"virtual\",\"addr\":\"200\",\"nodes\":[{\"name\":\"s\",\"fc\":3,"
                          "\"dtype\":\"float32_abcd\",\"formula\":\"=%s+1,AI1\"}]}]");
    if (e) {
        cJSON *ai = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetArrayItem(e, 0), "nodes"), 0);
        cJSON *vs = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetArrayItem(e, 1), "nodes"), 0);
        CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(ai, "fc")) == 4 &&
                  strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(ai, "dtype")), "float32_abcd") == 0,
              "AI1: input register, float32");
        CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(vs, "fc")) == 4, "virtual point forced to FC04");
    }
    cJSON_Delete(e);
    bad("edgeDevices", "[{\"name\":\"v\",\"source\":\"virtual\",\"nodes\":[{\"name\":\"s\",\"dtype\":\"bool\"}]}]",
        "choose a number type");
    bad("edgeDevices", "[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"name\":\"a\",\"ioPoint\":\"DI3\"}]}]",
        "IO channel \"DI3\" does not exist");
    bad("edgeDevices", "[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"name\":\"a\",\"ioPoint\":\"\"}]}]",
        "does not exist");
    bad("edgeDevices", "[{\"name\":\"io\",\"source\":\"io\",\"nodes\":[{\"name\":\"a\",\"ioPoint\":\"DO1\"},"
                       "{\"name\":\"b\",\"ioPoint\":\"DO1\"}]}]", "Point name \"DO1\" is used twice");
    bad("edgeDevices", "[{\"name\":\"bad name\"}]", "not valid");
    bad("edgeDevices", "[{\"name\":\"a\",\"source\":\"cloud\"}]", "unsupported value");
    bad("edgeDevices", "[{\"name\":\"a\",\"uart\":3}]", "uart must be 1 to 2");
    bad("edgeLinkage", "[{},{},{},{},{},{},{},{},{},{},{}]", "Too many");
}

/** F15: save-time Edge rules, and a stored model that breaks them still loads. */
static void test_edge_strict(void)
{
    bad("edgeDevices", "[{\"name\":\"a\",\"addr\":\"257\"}]", "slave ID must be 1 to 255");
    bad("edgeDevices", "[{\"name\":\"a\",\"addr\":\"1x\"}]", "slave ID must be 1 to 255");
    bad("edgeDevices", "[{\"name\":\"m\",\"protocol\":\"dlt645\",\"addr\":\"12345\"}]", "12-digit meter address");
    bad("edgeDevices", "[{\"name\":\"m\",\"protocol\":\"dlt645\",\"addr\":\"000000000001\",\"nodes\":[{\"name\":\"e\",\"di\":\"0201zz00\"}]}]",
        "8 hex digits");
    bad("edgeDevices", "[{\"name\":\"a\",\"nodes\":[{\"name\":\"c\",\"fc\":1,\"dtype\":\"uint16\"}]}]", "coils and discrete inputs are bool");
    bad("edgeDevices", "[{\"name\":\"a\",\"nodes\":[{\"name\":\"h\",\"fc\":3,\"dtype\":\"bool\"}]}]", "registers hold numbers");
    bad("edgeDevices", "[{\"name\":\"a\",\"nodes\":[{\"name\":\"f\",\"reg\":65534,\"dtype\":\"float64\"}]}]", "run past 65535");
    bad("edgeDevices", "[{\"name\":\"a\",\"nodes\":[{\"name\":\"f\",\"mapEn\":true,\"mapAddr\":65533,\"dtype\":\"float64\"}]}]",
        "mapped registers run past");
    bad("edgeDevices", "[{\"name\":\"a\",\"nodes\":[{\"name\":\"x\",\"reg\":10,\"dtype\":\"uint32_abcd\"},"
                       "{\"name\":\"y\",\"reg\":0,\"mapEn\":true,\"mapAddr\":11}]}]", "overlap x");
    bad("edgeDevices", "[{\"name\":\"a\",\"addr\":\"5\"},{\"name\":\"b\",\"addr\":\"6\",\"mapEn\":true,\"mapAddr\":5}]",
        "already answered for a");
    bad("edgeDevices", "[{\"name\":\"a\",\"addr\":\"7\"},{\"name\":\"v\",\"source\":\"virtual\",\"addr\":\"7\"}]",
        "already answered for a");
    // Same register in another function code, an unmapped duplicate read and two PLCs on unit 1 stay allowed.
    cJSON *e = ok("edgeDevices", "[{\"name\":\"a\",\"nodes\":[{\"name\":\"x\",\"fc\":4,\"reg\":3},{\"name\":\"y\",\"fc\":3,\"reg\":0,\"mapEn\":true,\"mapAddr\":3},"
                                 "{\"name\":\"z\",\"fc\":4,\"reg\":3,\"dtype\":\"int16\"}]},"
                                 "{\"name\":\"p1\",\"source\":\"eth\",\"ip\":\"10.0.0.1\",\"addr\":\"1\"},"
                                 "{\"name\":\"p2\",\"source\":\"eth\",\"ip\":\"10.0.0.2\",\"addr\":\"1\"}]");
    cJSON_Delete(e);

    // Stored by older firmware: loads (lenient), but would not be accepted as a new save.
    const char *old = "[{\"name\":\"a\",\"addr\":\"300\",\"nodes\":[{\"name\":\"c\",\"fc\":1,\"dtype\":\"uint16\"}]}]";
    cJSON *in = cJSON_Parse(old);
    char err[160] = "";
    cJSON *l = cfg_load(&ENV, "edgeDevices", in, err, sizeof err);
    CHECK(l && cJSON_GetArraySize(l) == 1 &&
          cJSON_GetArraySize(cJSON_GetObjectItem(cJSON_GetArrayItem(l, 0), "nodes")) == 1, "stored model kept on load (%s)", err);
    cJSON_Delete(l);
    CHECK(cfg_validate(&ENV, "edgeDevices", in, NULL, err, sizeof err) == NULL, "same model refused on save");
    cJSON_Delete(in);
}

// ---- cfg_edge_load (Plan 08c): normalised headers and nodes, one at a time ------------------------
typedef struct {
    cJSON *devs;  // what the visitors saw: headers with their nodes added
    int stop_at;  // the visitor call that answers false (-1: none)
    int calls;
} visit_t;

static bool on_dev(void *ctx, const cJSON *dev, int index)
{
    visit_t *v = ctx;
    if (v->calls++ == v->stop_at)
        return false;
    CHECK(index == cJSON_GetArraySize(v->devs), "device index %d in order", index);
    cJSON *d = cJSON_Duplicate(dev, true);
    cJSON_AddItemToObject(d, "nodes", cJSON_CreateArray());
    cJSON_AddItemToArray(v->devs, d);
    return true;
}

static bool on_node(void *ctx, const cJSON *node, int dev)
{
    visit_t *v = ctx;
    if (v->calls++ == v->stop_at)
        return false;
    CHECK(dev == cJSON_GetArraySize(v->devs) - 1, "node of the device just visited (%d)", dev);
    cJSON_AddItemToArray(cJSON_GetObjectItem(cJSON_GetArrayItem(v->devs, dev), "nodes"), cJSON_Duplicate(node, true));
    return true;
}

static void test_edge_load(void)
{
    const char *text = "[{\"name\":\"plc\",\"addr\":\"5\",\"uart\":2,\"nodes\":[{\"name\":\"t\",\"reg\":10,\"dtype\":\"int16\"},"
                       "{\"name\":\"c\",\"fc\":1,\"reg\":3,\"dtype\":\"bool\"}]},"
                       "{\"name\":\"x\",\"source\":\"io\",\"nodes\":[{\"name\":\"q\",\"ioPoint\":\"DO1\"}]},"
                       "{\"name\":\"y\",\"source\":\"virtual\",\"addr\":\"200\",\"nodes\":[{\"name\":\"s\",\"formula\":\"t+1\",\"dtype\":\"float32_abcd\"}]},"
                       "{\"name\":\"old\",\"addr\":\"300\",\"nodes\":[{\"name\":\"k\",\"fc\":1,\"dtype\":\"uint16\"}]}]";
    char err[160] = "";
    visit_t v = {cJSON_CreateArray(), -1, 0};
    cfg_result_t r = cfg_edge_load(&ENV, text, strlen(text), on_dev, on_node, &v, err, sizeof err);
    CHECK(r == CFG_OK, "a stored model loads, the lenient rules (%d %s)", r, err);
    cJSON *in = cJSON_Parse(text);
    cJSON *tree = cfg_load(&ENV, "edgeDevices", in, err, sizeof err);
    CHECK(tree && cJSON_Compare(tree, v.devs, true), "the visits make exactly cfg_load's tree");
    const cJSON *io = cJSON_GetArrayItem(v.devs, 1), *q = cJSON_GetArrayItem(cJSON_GetObjectItem(io, "nodes"), 0);
    CHECK(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(io, "name")), "SBIOT_IO") &&
              !strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(q, "name")), "DO1"),
          "the IO device and its channel names fixed up as saved");
    CHECK(v.calls == 4 + 5, "4 devices and 5 nodes visited (%d)", v.calls);
    cJSON_Delete(tree);
    cJSON_Delete(in);
    cJSON_Delete(v.devs);
    for (int stop = 0; stop < 9; stop++) {
        visit_t w = {cJSON_CreateArray(), stop, 0};
        r = cfg_edge_load(&ENV, text, strlen(text), on_dev, on_node, &w, err, sizeof err);
        CHECK(r == CFG_NO_MEMORY && w.calls == stop + 1, "a visitor's false at call %d stops the walk: no memory (%d)", stop, r);
        cJSON_Delete(w.devs);
    }
    r = cfg_edge_load(&ENV, "[{\"name\":5}]", 13, on_dev, on_node, &v, err, sizeof err);
    CHECK(r == CFG_INVALID && err[0], "a schema error as cfg_load words it (%s)", err);
}

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
    // Two bridges run at once (Plan 08a): the ports' default TCP servers must not share a listen port.
    cJSON *d1 = cfg_default(&ENV, "port1"), *d2 = cfg_default(&ENV, "port2");
    int l1 = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(d1, "listenPort")), l2 = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(d2, "listenPort"));
    CHECK(l1 == 23 && l2 == 24, "default listen ports 23 and 24 (%d, %d)", l1, l2);
    cJSON_Delete(d1);
    cJSON_Delete(d2);
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

static void test_listen_ports(void)
{
    cJSON *a = cJSON_Parse("{\"workMode\":3,\"listenPort\":23}"), *b = cJSON_Parse("{\"workMode\":3,\"listenPort\":23}");
    cJSON *b24 = cJSON_Parse("{\"workMode\":3,\"listenPort\":24}"), *bcl = cJSON_Parse("{\"workMode\":1,\"listenPort\":23}");
    cJSON *sys = cJSON_Parse("{\"webPort\":80,\"websocketPort\":6432}"), *w23 = cJSON_Parse("{\"webPort\":23,\"websocketPort\":6432}");
    cJSON *a80 = cJSON_Parse("{\"workMode\":3,\"listenPort\":80}"), *a6432 = cJSON_Parse("{\"workMode\":3,\"listenPort\":6432}");
    char err[160] = "";
    CHECK(!cfg_listen_ok(a, b, sys, err, sizeof err) && strstr(err, "23"), "both TCP servers on 23: refused (%s)", err);
    CHECK(cfg_listen_ok(a, b24, sys, err, sizeof err), "23 and 24: fine");
    CHECK(cfg_listen_ok(a, bcl, sys, err, sizeof err), "port2 not a TCP server: its listenPort does not count");
    CHECK(!cfg_listen_ok(a80, b24, sys, err, sizeof err) && strstr(err, "80"), "UART1 on the web port: refused (%s)", err);
    CHECK(!cfg_listen_ok(a, a6432, sys, err, sizeof err), "UART2 on the WebSocket port: refused");
    CHECK(!cfg_listen_ok(a, b24, w23, err, sizeof err), "system: the web port moved onto UART1's: refused");
    CHECK(cfg_listen_ok(a, NULL, NULL, err, sizeof err), "nothing else configured: fine");
    cJSON *all[] = {a, b, b24, bcl, sys, w23, a80, a6432};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
        cJSON_Delete(all[i]);
}

int main(void)
{
    test_edge_load();
    test_stm32_scope();
    test_listen_ports();
    test_two_port_values();
    test_edge_strict();
    test_defaults_validate();
    test_coercion_and_unknown_keys();
    test_ranges();
    test_lists();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("cfgmodel: all checks passed\n");
    return 0;
}
