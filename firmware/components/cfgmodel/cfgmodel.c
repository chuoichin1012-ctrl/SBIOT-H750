#include "cfgmodel.h"

#include "ipacl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Schema description ------------------------------------------------------

typedef enum { F_INT, F_NUM, F_BOOL, F_STR, F_ENUM } ftype_t;

typedef struct {
    const char *key;
    ftype_t type;
    double min;            // F_INT/F_NUM: lower bound; F_STR: unused
    double max;            // F_INT/F_NUM: upper bound; F_STR: max length
    const char *const *choices;  // F_ENUM: NULL-terminated list
} field_t;

#define INT(k, lo, hi) {k, F_INT, lo, hi, NULL}
#define NUM(k, lo, hi) {k, F_NUM, lo, hi, NULL}
#define BOOL(k) {k, F_BOOL, 0, 1, NULL}
#define STR(k, len) {k, F_STR, 0, len, NULL}
#define ENUM(k, list) {k, F_ENUM, 0, 0, list}
#define N(a) (sizeof(a) / sizeof((a)[0]))

// Mirrors src/pages/Network.tsx (single-SIM EC800; apnAuth 3 = PAP or CHAP)
static const field_t network_fields[] = {
    INT("priority", 0, 2), INT("ipMode", 0, 1), STR("ip", 15), STR("netmask", 15), STR("gateway", 15),
    INT("dnsMode", 0, 1), STR("dns1", 15), STR("dns2", 15), STR("apn", 30), STR("apnUser", 30), STR("apnPassword", 30),
    INT("apnAuth", 0, 3), INT("cellDnsMode", 0, 1), STR("cellDns1", 15),
    STR("cellDns2", 15),
};

// Mirrors src/pages/Port.tsx (serialDefs + socketDefs)
static const field_t port_fields[] = {
    INT("baudRate", 600, 230400), INT("dataBits", 7, 8), INT("parity", 0, 2), INT("stopBits", 1, 2),
    INT("flowControl", 0, 2), INT("packetLength", 0, 1460), INT("packetInterval", 0, 255),
    BOOL("serialHeartbeat"), INT("serialHeartbeatContent", 0, 4), INT("serialHeartbeatHex", 0, 1), STR("serialHeartbeatData", 40),
    INT("serialHeartbeatInterval", 1, 65535),
    INT("workMode", 0, 4), INT("socketMode", 0, 4), STR("remoteHost", 64), INT("remotePort", 1, 65535), INT("localPort", 0, 65535),
    INT("udpAnyRemotePort", 0, 1), INT("listenPort", 1, 65535), INT("maxClients", 1, 16), INT("clientLimitAction", 0, 1),
    STR("allowedClients", 140), STR("httpHost", 64), INT("httpPort", 1, 65535), INT("httpLocalPort", 0, 65535), INT("httpMethod", 0, 1), STR("httpPath", 100),
    STR("httpHeaders", 180), BOOL("httpStripHeaders"), INT("httpTimeout", 3, 255), INT("reconnectInterval", 0, 99999),
    INT("shortConnectionTime", 3, 255), BOOL("modbusPoll"), INT("modbusPollTimeout", 10, 9999), BOOL("modbusException"),
    BOOL("netHeartbeat"), INT("netHeartbeatContent", 0, 4), INT("netHeartbeatHex", 0, 1), STR("netHeartbeatData", 40),
    INT("netHeartbeatInterval", 1, 65535), INT("registration", 0, 5), INT("registrationSend", 0, 2), INT("registrationHex", 0, 1),
    STR("registrationData", 40), INT("tls", 0, 2), INT("tlsVerify", 0, 2),
};

// Mirrors src/pages/Mqtt.tsx (brokerDefs + customDefs)
static const field_t mqtt_fields[] = {
    INT("enabled", 0, 1), INT("version", 0, 1), STR("host", 128), INT("port", 1, 65535),
    STR("clientId", 128), INT("keepAlive", 0, 65535), INT("reconnectIfSilent", 0, 65535), INT("retryInterval", 3, 65535), BOOL("cleanSession"),
    BOOL("auth"), STR("username", 128), STR("password", 200), BOOL("will"), STR("willTopic", 128), STR("willMessage", 128),
    INT("willQos", 0, 2), BOOL("willRetain"), INT("tls", 0, 2), INT("tlsVerify", 0, 2), BOOL("sni"), STR("sniHost", 128),
    INT("customMode", 0, 1), INT("customModePorts", 0, 255),
};

// One MQTT topic (Mqtt.tsx `Topic`)
static const field_t topic_fields[] = {
    BOOL("enabled"), INT("mode", 0, 1), STR("topic", 128), INT("ports", 0, 255), INT("qos", 0, 2),
    BOOL("retain"), BOOL("ioControl"), STR("identifier", 64), STR("delimiter", 16),
};

// Mirrors src/pages/System.tsx; websocketTarget upper bound depends on ports.
static const field_t system_fields[] = {
    STR("hostname", 16), INT("bufferOffline", 0, 1), INT("idleRestart", 0, 65535), INT("rs485CollisionAvoid", 0, 1),
    INT("rs485IdleTime", 0, 2500), STR("username", 16), STR("password", 16), INT("webPort", 1, 65535),
    INT("websocketPort", 1, 65535),
    INT("websocketTarget", 0, 3), INT("ntp", 0, 1), STR("ntpServer", 128), INT("timezone", -12, 12),
};

// Mirrors src/pages/Io.tsx (2 DO, 6 timers; DO index 0-based)
#define TIMER(n)                                                                                              \
    INT("timer" #n "Enabled", 0, 1), STR("timer" #n "Time", 8), INT("timer" #n "Action", 0, 1),                    \
        INT("timer" #n "Output", 0, 1), INT("timer" #n "OutputAction", 0, 2)
static const field_t io_fields[] = {
    INT("modbusAddress", 1, 247), INT("keepOutputs", 0, 1), INT("do0Mode", 0, 2), INT("do0FlipDelay", 1, 65535),
    INT("do1Mode", 0, 2), INT("do1FlipDelay", 1, 65535), INT("diDebounce", 10, 65535),
    INT("diInvert", 0, 1), TIMER(0), TIMER(1), TIMER(2), TIMER(3), TIMER(4), TIMER(5),
};

static const field_t edge_fields[] = {BOOL("enabled")};

// Mirrors src/pages/edge/Report.tsx; channel: 0 = MQTT, n = UARTn socket.
static const field_t edge_report_fields[] = {
    INT("channel", 0, 2), INT("remoteAccess", 0, 1), BOOL("remoteWrite"), INT("remoteFormat", 0, 2), STR("requestTopic", 72), INT("requestQos", 0, 2),
    STR("replyTopic", 72), INT("replyQos", 0, 2), BOOL("replyRetain"), INT("reporting", 0, 1), STR("reportTopic", 72),
    INT("reportQos", 0, 2), BOOL("reportRetain"), BOOL("periodic"), INT("interval", 1, 36000), BOOL("scheduled"),
    INT("schedule", 0, 3), INT("scheduleHour", 0, 23), INT("scheduleMinute", 0, 59), BOOL("failurePlaceholder"), STR("placeholder", 16),
    BOOL("quoteValues"), STR("template", 2048),
};

// Mirrors src/lib/types.ts EdgeSlave / EdgeNode / LinkageEvent
static const char *const SOURCES[] = {"uart", "eth", "io", "virtual", NULL};
static const char *const PROTOCOLS[] = {"modbus", "dlt645", NULL};
static const char *const DTYPES[] = {"bool", "uint8", "int8", "uint16", "int16", "uint32_abcd", "uint32_cdab",
                                     "int32_abcd", "int32_cdab", "float32_abcd", "float32_cdab", "float64", NULL};
static const field_t device_fields[] = {
    STR("name", 16), ENUM("source", SOURCES), INT("uart", 1, 2), STR("ip", 15), INT("ethPort", 1, 65535),
    ENUM("protocol", PROTOCOLS), STR("addr", 12), INT("poll", 100, 65535), BOOL("mapEn"), INT("mapAddr", 1, 255),
    BOOL("merge"),
};
static const field_t node_fields[] = {
    STR("name", 16), INT("fc", 1, 4), INT("reg", 0, 65534), STR("di", 8), ENUM("dtype", DTYPES),
    INT("timeout", 10, 65535), BOOL("onChange"), INT("range", 1, 65535), BOOL("mapEn"), INT("mapAddr", 0, 65534),
    STR("formula", 64), STR("ioPoint", 4), INT("ioReport", 0, 2), NUM("upper", -1e9, 1e9), NUM("lower", -1e9, 1e9),
};
static const field_t linkage_fields[] = {
    STR("name", 16), BOOL("enable"), INT("cond", 0, 7), STR("point", 16), INT("cycle", 0, 10000),
    INT("minInterval", 500, 10000), NUM("upper", -1e9, 1e9), NUM("lower", -1e9, 1e9), STR("output", 4),
    INT("action", 0, 3), STR("desc", 64),
};

#define MAX_TOPICS 16
#define MAX_NODES 200  // across all devices (Plan 09e; 09d had 300)
#define MAX_ETH_SLAVES 5
#define MAX_EVENTS 10

// ---- Defaults (factory settings) --------------------------------------------------

// A static address and Ethernet only (priority 0): a new board, or a factory reset, never attaches LTE on its own
// (the user, 2026-10-17).
static const char DEF_NETWORK[] =
    "{\"priority\":0,\"ipMode\":1,\"ip\":\"192.168.0.7\",\"netmask\":\"255.255.255.0\",\"gateway\":\"192.168.0.1\","
    "\"dnsMode\":1,\"dns1\":\"8.8.8.8\",\"dns2\":\"1.1.1.1\",\"apn\":\"internet\",\"apnUser\":\"user\","
    "\"apnPassword\":\"password\",\"apnAuth\":0,\"cellDnsMode\":0,\"cellDns1\":\"8.8.8.8\",\"cellDns2\":\"1.1.1.1\"}";

// The ports' defaults differ only in the TCP server's listen port: both bridges run at once (Plan 08a).
#define DEF_PORT_JSON(LISTEN) \
    "{\"baudRate\":115200,\"dataBits\":8,\"parity\":0,\"stopBits\":1,\"flowControl\":0,\"packetLength\":0,\"packetInterval\":0,"\
    "\"serialHeartbeat\":false,\"serialHeartbeatContent\":0,\"serialHeartbeatHex\":1,\"serialHeartbeatData\":\"5342494F54\",\"serialHeartbeatInterval\":30,"\
    "\"workMode\":3,\"socketMode\":0,\"remoteHost\":\"192.168.0.201\",\"remotePort\":23,\"localPort\":0,\"udpAnyRemotePort\":0,"\
    "\"listenPort\":" LISTEN ",\"maxClients\":8,\"clientLimitAction\":0,\"allowedClients\":\"0.0.0.0/0\",\"httpHost\":\"192.168.0.201\",\"httpPort\":80,\"httpLocalPort\":0,\"httpMethod\":0,"\
    "\"httpPath\":\"/\",\"httpHeaders\":\"User-Agent: SBIOT-GW\",\"httpStripHeaders\":true,\"httpTimeout\":10,\"reconnectInterval\":5,\"shortConnectionTime\":10,"\
    "\"modbusPoll\":false,\"modbusPollTimeout\":200,\"modbusException\":false,\"netHeartbeat\":false,\"netHeartbeatContent\":0,\"netHeartbeatHex\":1,"\
    "\"netHeartbeatData\":\"5342494F54\",\"netHeartbeatInterval\":60,\"registration\":0,\"registrationSend\":0,\"registrationHex\":1,\"registrationData\":\"5342494F54\","\
    "\"tls\":0,\"tlsVerify\":0}"
static const char DEF_PORT[] = DEF_PORT_JSON("23");
static const char DEF_PORT2[] = DEF_PORT_JSON("24");

static const char DEF_MQTT[] =
    "{\"enabled\":0,\"version\":1,\"host\":\"broker.emqx.io\",\"port\":1883,\"clientId\":\"SBIOT-GW\",\"keepAlive\":60,\"reconnectIfSilent\":0,"
    "\"retryInterval\":5,\"cleanSession\":true,\"auth\":false,\"username\":\"admin\",\"password\":\"admin\",\"will\":false,\"willTopic\":\"sbiot/gw/status\","
    "\"willMessage\":\"offline\",\"willQos\":1,\"willRetain\":true,\"tls\":0,\"tlsVerify\":0,\"sni\":false,\"sniHost\":\"broker.emqx.io\",\"customMode\":0,\"customModePorts\":1}";

static const char DEF_SYSTEM[] =
    "{\"hostname\":\"sbiot-gw\",\"bufferOffline\":0,\"idleRestart\":0,\"rs485CollisionAvoid\":0,\"rs485IdleTime\":10,\"username\":\"admin\","
    "\"password\":\"admin\",\"webPort\":80,\"websocketPort\":6432,"
    "\"websocketTarget\":0,\"ntp\":1,\"ntpServer\":\"pool.ntp.org\",\"timezone\":7}";

#define DEF_TIMER(n)                                                                                      \
    "\"timer" #n "Enabled\":0,\"timer" #n "Time\":\"08:00:00\",\"timer" #n "Action\":0,\"timer" #n "Output\":0," \
    "\"timer" #n "OutputAction\":0"
static const char DEF_IO[] =
    "{\"modbusAddress\":100,\"keepOutputs\":0,\"do0Mode\":0,\"do0FlipDelay\":5,\"do1Mode\":0,\"do1FlipDelay\":5,"
    "\"diDebounce\":50,\"diInvert\":0," DEF_TIMER(0) "," DEF_TIMER(1) "," DEF_TIMER(2) "," DEF_TIMER(
        3) "," DEF_TIMER(4) "," DEF_TIMER(5) "}";

static const char DEF_EDGE[] = "{\"enabled\":false}";

static const char DEF_EDGE_REPORT[] =
    "{\"channel\":0,\"remoteAccess\":0,\"remoteWrite\":false,\"remoteFormat\":2,\"requestTopic\":\"sbiot/gw/request\",\"requestQos\":0,\"replyTopic\":\"sbiot/gw/reply\",\"replyQos\":0,"
    "\"replyRetain\":false,\"reporting\":0,\"reportTopic\":\"sbiot/gw/report\",\"reportQos\":0,\"reportRetain\":false,\"periodic\":true,\"interval\":60,"
    "\"scheduled\":false,\"schedule\":0,\"scheduleHour\":0,\"scheduleMinute\":0,\"failurePlaceholder\":false,\"placeholder\":\"null\",\"quoteValues\":false,"
    "\"template\":\"{\\\"sn\\\":\\\"sys_sn\\\",\\\"time\\\":\\\"sys_time\\\",\\\"temperature\\\":\\\"Temperature\\\","
    "\\\"humidity\\\":\\\"Humidity\\\"}\"}";

// Sample lists (the user: no config without data): each one off until the user turns it on (MQTT and
// Edge are off too), so a fresh gateway does nothing with them.
#define DEF_TOPIC_JSON(TOPIC)                                                                                      \
    "[{\"enabled\":false,\"mode\":0,\"topic\":\"" TOPIC "\",\"ports\":1,\"qos\":0,\"retain\":false,\"ioControl\":false," \
    "\"identifier\":\"sbiot\",\"delimiter\":\",\"}]"
static const char DEF_PUBLISH[] = DEF_TOPIC_JSON("sbiot/gw/up");
static const char DEF_SUBSCRIBE[] = DEF_TOPIC_JSON("sbiot/gw/down");

// One Modbus RTU slave on UART1 with two holding registers in tenths.
#define DEF_NODE_JSON(NAME, REG, DTYPE)                                                                                \
    "{\"name\":\"" NAME "\",\"fc\":3,\"reg\":" REG ",\"di\":\"00000000\",\"dtype\":\"" DTYPE "\",\"timeout\":200,"           \
    "\"onChange\":false,\"range\":1,\"mapEn\":false,\"mapAddr\":0,\"formula\":\"=%s*0.1\",\"ioPoint\":\"\",\"ioReport\":0," \
    "\"upper\":0,\"lower\":0}"
static const char DEF_DEVICES[] =
    "[{\"name\":\"Device1\",\"source\":\"uart\",\"uart\":1,\"ip\":\"192.168.0.201\",\"ethPort\":502,\"protocol\":\"modbus\","
    "\"addr\":\"1\",\"poll\":1000,\"mapEn\":false,\"mapAddr\":1,\"merge\":false,\"nodes\":["
    DEF_NODE_JSON("Temperature", "0", "int16") "," DEF_NODE_JSON("Humidity", "1", "uint16") "]}]";

// DO1 closes while Temperature is at least 50, off until enabled.
static const char DEF_LINKAGE[] =
    "[{\"name\":\"HighTemp\",\"enable\":false,\"cond\":2,\"point\":\"Temperature\",\"cycle\":1000,\"minInterval\":1000,"
    "\"upper\":60,\"lower\":50,\"output\":\"DO1\",\"action\":2,\"desc\":\"DO1 closes at 50 degrees or more\"}]";

// ---- Config table ------------------------------------------------------------------

typedef enum { K_OBJECT, K_TOPICS, K_SLAVES, K_EVENTS } kind_t;

typedef struct {
    const char *name;
    kind_t kind;
    const field_t *fields;
    size_t nfields;
    const char *defaults;
} cfgdef_t;

static const cfgdef_t CONFIGS[] = {
    {"network", K_OBJECT, network_fields, N(network_fields), DEF_NETWORK},
    {"port1", K_OBJECT, port_fields, N(port_fields), DEF_PORT},
    {"port2", K_OBJECT, port_fields, N(port_fields), DEF_PORT2},
    {"mqtt", K_OBJECT, mqtt_fields, N(mqtt_fields), DEF_MQTT},
    {"mqttPublish", K_TOPICS, topic_fields, N(topic_fields), DEF_PUBLISH},
    {"mqttSubscribe", K_TOPICS, topic_fields, N(topic_fields), DEF_SUBSCRIBE},
    {"system", K_OBJECT, system_fields, N(system_fields), DEF_SYSTEM},
    {"io", K_OBJECT, io_fields, N(io_fields), DEF_IO},
    {"edge", K_OBJECT, edge_fields, N(edge_fields), DEF_EDGE},
    {"edgeReport", K_OBJECT, edge_report_fields, N(edge_report_fields), DEF_EDGE_REPORT},
    {"edgeDevices", K_SLAVES, device_fields, N(device_fields), DEF_DEVICES},
    {"edgeLinkage", K_EVENTS, linkage_fields, N(linkage_fields), DEF_LINKAGE},
};

static const cfgdef_t *find(const cfg_env_t *env, const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < N(CONFIGS); i++) {
        if (strcmp(CONFIGS[i].name, name) != 0) continue;
        if (strcmp(name, "port2") == 0 && env->uart_count < 2) return NULL;
        return &CONFIGS[i];
    }
    return NULL;
}

bool cfg_known(const cfg_env_t *env, const char *name) { return find(env, name) != NULL; }

size_t cfg_names(const cfg_env_t *env, const char **out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < N(CONFIGS) && n < max; i++) {
        if (find(env, CONFIGS[i].name)) out[n++] = CONFIGS[i].name;
    }
    return n;
}

cJSON *cfg_default(const cfg_env_t *env, const char *name)
{
    const cfgdef_t *def = find(env, name);
    return def ? cJSON_Parse(def->defaults) : NULL;
}

bool cfg_is_list(const cfg_env_t *env, const char *name)
{
    const cfgdef_t *def = find(env, name);
    return def && def->kind != K_OBJECT;
}

const char *cfg_default_text(const cfg_env_t *env, const char *name)
{
    const cfgdef_t *def = find(env, name);
    return def ? def->defaults : NULL;
}

// ---- Validation -------------------------------------------------------------------------

static void fail(char *err, size_t len, const char *fmt, const char *a, const char *b)
{
    if (err && len) snprintf(err, len, fmt, a ? a : "", b ? b : "");
}

/** Upper bound that depends on the hardware, or the schema's own. */
static double bound_max(const cfg_env_t *env, const char *cfg, const field_t *f)
{
    if (strcmp(f->key, "websocketTarget") == 0) return env->uart_count + 1;  // OFF, UART1..n, LOG
    if (strcmp(cfg, "edgeReport") == 0 && strcmp(f->key, "channel") == 0) return env->uart_count;
    if (strcmp(f->key, "uart") == 0) return env->uart_count;
    size_t k = strlen(f->key);
    if (strncmp(f->key, "timer", 5) == 0 && k > 6 && strcmp(f->key + k - 6, "Output") == 0) {  // DO index
        return env->do_count > 0 ? env->do_count - 1 : 0;
    }
    return f->max;
}

/** cJSON_ReplaceItemInObjectCaseSensitive that frees `item` when it cannot be placed (no key, or no memory). */
static void put_field(cJSON *dst, const char *key, cJSON *item)
{
    if (!cJSON_ReplaceItemInObjectCaseSensitive(dst, key, item)) cJSON_Delete(item);
}

/** Reads one field from `src` into `dst` with type coercion. Returns false on a type/range error. */
static bool take(const cfg_env_t *env, const char *cfg, const field_t *f, const cJSON *src, cJSON *dst, char *err,
                 size_t errlen)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(src, f->key);
    if (!v) return true;  // keep the existing value
    double max = bound_max(env, cfg, f);

    switch (f->type) {
    case F_BOOL: {
        bool b;
        if (cJSON_IsBool(v)) b = cJSON_IsTrue(v);
        else if (cJSON_IsNumber(v)) b = v->valuedouble != 0;
        else if (cJSON_IsString(v)) b = strcmp(v->valuestring, "1") == 0 || strcmp(v->valuestring, "true") == 0;
        else return fail(err, errlen, "%s must be true or false.", f->key, NULL), false;
        put_field(dst, f->key, cJSON_CreateBool(b));
        return true;
    }
    case F_INT:
    case F_NUM: {
        double d;
        char *end = NULL;
        if (cJSON_IsNumber(v)) d = v->valuedouble;
        else if (cJSON_IsBool(v)) d = cJSON_IsTrue(v);
        else if (cJSON_IsString(v) && *v->valuestring && (d = strtod(v->valuestring, &end), *end == '\0'));
        else return fail(err, errlen, "%s must be a number.", f->key, NULL), false;
        if (!isfinite(d) || d < f->min || d > max || (f->type == F_INT && d != floor(d))) {
            char range[48];
            snprintf(range, sizeof range, f->type == F_INT ? "%.0f to %.0f" : "%g to %g", f->min, max);
            return fail(err, errlen, "%s must be %s.", f->key, range), false;
        }
        put_field(dst, f->key, cJSON_CreateNumber(d));
        return true;
    }
    case F_STR:
        if (!cJSON_IsString(v)) return fail(err, errlen, "%s must be text.", f->key, NULL), false;
        if (strlen(v->valuestring) > (size_t)f->max) return fail(err, errlen, "%s is too long.", f->key, NULL), false;
        put_field(dst, f->key, cJSON_CreateString(v->valuestring));
        return true;
    case F_ENUM:
        if (cJSON_IsString(v)) {
            for (const char *const *c = f->choices; *c; c++) {
                if (strcmp(*c, v->valuestring) == 0) {
                    put_field(dst, f->key, cJSON_CreateString(v->valuestring));
                    return true;
                }
            }
        }
        return fail(err, errlen, "%s has an unsupported value.", f->key, NULL), false;
    }
    return false;
}

/** cJSON's failed allocations so far: the board's allocator counts them (heap_wrap), host tests too. */
__attribute__((weak)) unsigned json_alloc_failures(void) { return 0; }

/** The loader's note (Plan 09b T3): "<cfg>: defaults used for a, b, ..." in err, as long as it fits. */
static void note(char *err, size_t len, const char *cfg, const char *key)
{
    if (!err || !len) return;
    size_t k = strlen(err);
    if (!k) snprintf(err, len, "%s: defaults used for %s", cfg, key);
    else if (k + 2 + strlen(key) < len) snprintf(err + k, len - k, ", %s", key);
}

/** The loader takes a value only of its field's own JSON type: no "1" for a bool, no true for a port (Codex r4). */
static bool type_ok(const field_t *f, const cJSON *v)
{
    switch (f->type) {
    case F_BOOL: return cJSON_IsBool(v);
    case F_INT:
    case F_NUM: return cJSON_IsNumber(v);
    case F_STR:
    case F_ENUM: return cJSON_IsString(v);
    }
    return false;
}

/** Copies `base` keeping only schema keys, then overlays validated fields from `in`. `lenient` (the loader,
 * Plan 09b T3): a field that fails keeps its base value and is noted, the others are used; never refused. */
static cJSON *object(const cfg_env_t *env, const char *cfg, const field_t *fields, size_t n, const cJSON *in,
                     const cJSON *base, bool lenient, char *err, size_t errlen)
{
    if (!cJSON_IsObject(in)) return fail(err, errlen, "Expected an object for %s.", cfg, NULL), NULL;
    cJSON *out = cJSON_CreateObject();
    for (size_t i = 0; i < n; i++) {
        const cJSON *b = cJSON_GetObjectItemCaseSensitive(base, fields[i].key);
        if (b) {
            cJSON *copy = cJSON_Duplicate(b, true);
            if (!cJSON_AddItemToObject(out, fields[i].key, copy)) cJSON_Delete(copy);  // the key copy failed
        }
        char why[160] = "";
        const cJSON *v = lenient ? cJSON_GetObjectItemCaseSensitive(in, fields[i].key) : NULL;
        if (v) {  // the loader: the value as the UI sent it, if it is of its field's JSON type (the user, 2026-10-16)
            if (type_ok(&fields[i], v)) put_field(out, fields[i].key, cJSON_Duplicate(v, false));
            else note(err, errlen, cfg, fields[i].key);
            continue;
        }
        if (!take(env, cfg, &fields[i], in, out, lenient ? why : err, lenient ? sizeof why : errlen)) {
            if (lenient) {
                note(err, errlen, cfg, fields[i].key);
                continue;
            }
            cJSON_Delete(out);
            return NULL;
        }
    }
    return out;
}

static bool name_ok(const char *s)
{
    if (!s || !*s || strlen(s) > 16) return false;
    for (; *s; s++) {
        char c = *s;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == '-'))
            return false;
    }
    return true;
}

/** Validates an array whose items share one schema; `item_base` supplies defaults per item. */
static cJSON *list(const cfg_env_t *env, const char *cfg, const field_t *fields, size_t n, const cJSON *in,
                   int max_items, const char *item_base, bool lenient, char *err, size_t errlen)
{
    if (!cJSON_IsArray(in)) return fail(err, errlen, "Expected a list for %s.", cfg, NULL), NULL;
    if (!lenient && cJSON_GetArraySize(in) > max_items) return fail(err, errlen, "Too many entries in %s.", cfg, NULL), NULL;
    cJSON *base = cJSON_Parse(item_base);
    cJSON *out = cJSON_CreateArray();
    const cJSON *it;
    int taken = 0;
    cJSON_ArrayForEach(it, in)
    {
        if (lenient && (taken >= max_items || !cJSON_IsObject(it))) {  // the loader: the first ones up to the limit
            note(err, errlen, cfg, taken >= max_items ? "the entries over the limit" : "an entry that is not an object");
            continue;
        }
        taken++;
        cJSON *o = out ? object(env, cfg, fields, n, it, base, lenient, err, errlen) : NULL;  // out is NULL only without memory
        if (!o) {
            cJSON_Delete(out);
            out = NULL;
            break;
        }
        cJSON_AddItemToArray(out, o);
    }
    cJSON_Delete(base);
    return out;
}

static const char ITEM_TOPIC[] =
    "{\"enabled\":false,\"mode\":0,\"topic\":\"sbiot/gw/data\",\"ports\":1,\"qos\":0,\"retain\":false,\"ioControl\":false,\"identifier\":\"sbiot\","
    "\"delimiter\":\",\"}";
static const char ITEM_EVENT[] =
    "{\"name\":\"Event1\",\"enable\":true,\"cond\":2,\"point\":\"\",\"cycle\":1000,\"minInterval\":1000,\"upper\":0,"
    "\"lower\":0,\"output\":\"\",\"action\":1,\"desc\":\"Linkage event\"}";
static const char ITEM_SLAVE[] =
    "{\"name\":\"Device1\",\"source\":\"uart\",\"uart\":1,\"ip\":\"192.168.0.201\",\"ethPort\":502,\"protocol\":\"modbus\","
    "\"addr\":\"1\",\"poll\":1000,\"mapEn\":false,\"mapAddr\":1,\"merge\":false}";
static const char ITEM_NODE[] =
    "{\"name\":\"Node1\",\"fc\":3,\"reg\":0,\"di\":\"00000000\",\"dtype\":\"uint16\",\"timeout\":200,\"onChange\":false,"
    "\"range\":1,\"mapEn\":false,\"mapAddr\":0,\"formula\":\"\",\"ioPoint\":\"\",\"ioReport\":0,\"upper\":0,"
    "\"lower\":0}";

// The built-in IO and the virtual (calculated) device: at most one each, under
// fixed names (as the reference unit's M100_IO / M100_VIRTUAL). Mirrors src/lib/edge.ts.
#define IO_DEVICE "SBIOT_IO"
#define VIRTUAL_DEVICE "SBIOT_VIRTUAL"

/** "DI2", "DO1", "AI1": a channel this hardware has. */
static bool io_channel_ok(const cfg_env_t *env, const char *s)
{
    if (!s || strlen(s) < 3) return false;
    int max = !strncmp(s, "DI", 2) ? env->di_count : !strncmp(s, "DO", 2) ? env->do_count : !strncmp(s, "AI", 2) ? env->ai_count : 0;
    char *end;
    long n = strtol(s + 2, &end, 10);
    return *end == '\0' && n >= 1 && n <= max;
}

static const char *sget(const cJSON *o, const char *k) { return cJSON_GetStringValue(cJSON_GetObjectItem(o, k)); }
static int iget(const cJSON *o, const char *k) { return (int)cJSON_GetNumberValue(cJSON_GetObjectItem(o, k)); }
static bool bget(const cJSON *o, const char *k) { return cJSON_IsTrue(cJSON_GetObjectItem(o, k)); }

static bool all_digits(const char *s, size_t min, size_t max)
{
    size_t n = strlen(s);
    if (n < min || n > max) return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
    }
    return true;
}

static int regs_of(const char *dtype) { return !strcmp(dtype, "float64") ? 4 : strstr(dtype, "32") ? 2 : 1; }

/** Slave ID the gateway answers this device under; -1 when not exposed or not known here (IO: set on the IO page). */
static int exposed_unit(const cJSON *s)
{
    if (bget(s, "mapEn")) return iget(s, "mapAddr");
    const char *src = sget(s, "source");
    bool own = (!strcmp(src, "uart") && !strcmp(sget(s, "protocol"), "modbus")) || !strcmp(src, "eth") || !strcmp(src, "virtual");
    return own ? atoi(sget(s, "addr")) : -1;
}

/** Function code space, first register and width a point is exposed at (mirrors edge.ts exposedSpan); false if none. */
static bool exposed_span(const cJSON *s, const cJSON *n, int *fc, long *start, int *width)
{
    const char *src = sget(s, "source");
    bool dlt = !strcmp(src, "uart") && !strcmp(sget(s, "protocol"), "dlt645");
    if (dlt && !bget(n, "mapEn")) return false;
    *fc = dlt ? 3 : iget(n, "fc");  // IO and virtual points were given their fc above
    *start = bget(n, "mapEn") ? iget(n, "mapAddr") : iget(n, "reg");
    *width = *fc <= 2 ? 1 : regs_of(sget(n, "dtype"));
    return true;
}

/**
 * Rules checked when settings are saved (mirroring src/pages/edge/Acquisition.tsx),
 * not when a stored copy is loaded: a model saved by older firmware keeps working.
 */
static bool slaves_strict(const cJSON *out, char *err, size_t errlen)
{
    int n_dev = cJSON_GetArraySize(out);
    for (int i = 0; i < n_dev; i++) {
        const cJSON *s = cJSON_GetArrayItem(out, i);
        const char *sname = sget(s, "name"), *src = sget(s, "source"), *addr = sget(s, "addr");
        bool dlt = !strcmp(src, "uart") && !strcmp(sget(s, "protocol"), "dlt645");
        if (dlt && !all_digits(addr, 12, 12)) {
            return fail(err, errlen, "%s: enter the 12-digit meter address.", sname, NULL), false;
        }
        if (!dlt && strcmp(src, "io") != 0 && !(all_digits(addr, 1, 3) && atoi(addr) >= 1 && atoi(addr) <= 255)) {
            return fail(err, errlen, "%s: the slave ID must be 1 to 255.", sname, NULL), false;
        }
        int unit = exposed_unit(s);
        if (unit >= 0 && (bget(s, "mapEn") || !strcmp(src, "virtual"))) {  // a chosen ID: keep it unique
            for (int k = 0; k < n_dev; k++) {
                const cJSON *o = cJSON_GetArrayItem(out, k);
                if (k != i && exposed_unit(o) == unit) {
                    return fail(err, errlen, "%s: its slave ID is already answered for %s.", sname, sget(o, "name")), false;
                }
            }
        }
        const cJSON *nodes = cJSON_GetObjectItem(s, "nodes"), *n;
        cJSON_ArrayForEach(n, nodes)
        {
            const char *nname = sget(n, "name"), *dtype = sget(n, "dtype");
            int fc = iget(n, "fc");
            if (dlt) {
                const char *di = sget(n, "di");
                bool hex = strlen(di) == 8;
                for (const char *p = di; hex && *p; p++) hex = (*p >= '0' && *p <= '9') || ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f');
                if (!hex) return fail(err, errlen, "%s: the data identifier must be 8 hex digits.", nname, NULL), false;
                if (!strcmp(dtype, "bool")) return fail(err, errlen, "%s: a meter value is a number, not bool.", nname, NULL), false;
            } else if ((fc <= 2) != !strcmp(dtype, "bool")) {
                return fail(err, errlen, fc <= 2 ? "%s: coils and discrete inputs are bool." : "%s: registers hold numbers, not bool.",
                            nname, NULL), false;
            }
            int width = fc <= 2 && !dlt ? 1 : regs_of(dtype);
            if (!dlt && iget(n, "reg") + width > 65536) {
                return fail(err, errlen, "%s: its registers run past 65535.", nname, NULL), false;
            }
            int xfc, xw;
            long start;
            if (!exposed_span(s, n, &xfc, &start, &xw)) continue;
            if (start + xw > 65536) return fail(err, errlen, "%s: its mapped registers run past 65535.", nname, NULL), false;
            if (!bget(n, "mapEn")) continue;
            const cJSON *o;
            cJSON_ArrayForEach(o, nodes)
            {
                int ofc, ow;
                long ostart;
                if (o == n || !exposed_span(s, o, &ofc, &ostart, &ow) || ofc != xfc) continue;
                if (start < ostart + ow && ostart < start + xw) {
                    return fail(err, errlen, "%s: its mapped registers overlap %s.", nname, sget(o, "name")), false;
                }
            }
        }
    }
    return true;
}

/** Edge model: slave fields, nested nodes, unique names, global limits; `strict` adds the save-time rules. */
static cJSON *slaves(const cfg_env_t *env, const cJSON *in, bool strict, char *err, size_t errlen)
{
    cJSON *out = list(env, "edgeDevices", device_fields, N(device_fields), in, 64, ITEM_SLAVE, false, err, errlen);
    if (!out) return NULL;
    cJSON *node_base = cJSON_Parse(ITEM_NODE);
    int total = 0, eth = 0, io = 0, virt = 0;
    const char *names[MAX_NODES + 64];
    int nnames = 0;
    for (int i = 0; i < cJSON_GetArraySize(out); i++) {
        cJSON *s = cJSON_GetArrayItem(out, i);
        const char *src = cJSON_GetStringValue(cJSON_GetObjectItem(s, "source"));
        bool is_io = strcmp(src, "io") == 0, is_virt = strcmp(src, "virtual") == 0;
        if ((is_io && ++io > 1) || (is_virt && ++virt > 1)) {
            fail(err, errlen, is_io ? "Only one built-in IO device is allowed." : "Only one virtual device is allowed.",
                 NULL, NULL);
            goto bad;
        }
        // Their names are fixed: whatever was sent, they are stored under their own.
        if (is_io || is_virt) cJSON_ReplaceItemInObject(s, "name", cJSON_CreateString(is_io ? IO_DEVICE : VIRTUAL_DEVICE));
        const char *sname = cJSON_GetStringValue(cJSON_GetObjectItem(s, "name"));
        if (!name_ok(sname)) {
            fail(err, errlen, "Device name \"%s\" is not valid.", sname, NULL);
            goto bad;
        }
        if (!is_io && !is_virt && (strcmp(sname, IO_DEVICE) == 0 || strcmp(sname, VIRTUAL_DEVICE) == 0)) {
            fail(err, errlen, "\"%s\" is reserved for the built-in IO and virtual devices.", sname, NULL);
            goto bad;
        }
        for (int k = 0; k < i; k++) {
            if (strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(out, k), "name")), sname) == 0) {
                fail(err, errlen, "Device name \"%s\" is used twice.", sname, NULL);
                goto bad;
            }
        }
        if (strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(s, "source")), "eth") == 0 && ++eth > MAX_ETH_SLAVES) {
            fail(err, errlen, "At most 5 Modbus TCP devices.", NULL, NULL);
            goto bad;
        }
        const cJSON *nodes_in = cJSON_GetObjectItem(cJSON_GetArrayItem(in, i), "nodes");
        cJSON *nodes = cJSON_CreateArray();
        cJSON_AddItemToObject(s, "nodes", nodes);
        const cJSON *n;
        cJSON_ArrayForEach(n, nodes_in)
        {
            if (++total > MAX_NODES) {
                fail(err, errlen, "At most 200 points across all devices.", NULL, NULL);
                goto bad;
            }
            cJSON *o = object(env, "edgeDevices", node_fields, N(node_fields), n, node_base, false, err, errlen);
            if (!o) goto bad;
            cJSON_AddItemToArray(nodes, o);
            if (is_io) {
                // An IO point is named after its channel and sits at its register on the IO map
                // (DO = coils, DI = discrete inputs, AI = input registers as float32, two per channel).
                const char *ch = cJSON_GetStringValue(cJSON_GetObjectItem(o, "ioPoint"));
                if (!io_channel_ok(env, ch)) {
                    fail(err, errlen, "IO channel \"%s\" does not exist on this gateway.", ch ? ch : "", NULL);
                    goto bad;
                }
                int idx = atoi(ch + 2) - 1, fc = ch[1] == 'O' ? 1 : ch[1] == 'I' && ch[0] == 'D' ? 2 : 4;
                cJSON_ReplaceItemInObject(o, "name", cJSON_CreateString(ch));
                cJSON_ReplaceItemInObject(o, "fc", cJSON_CreateNumber(fc));
                cJSON_ReplaceItemInObject(o, "reg", cJSON_CreateNumber(fc == 4 ? idx * 2 : idx));
                cJSON_ReplaceItemInObject(o, "dtype", cJSON_CreateString(fc == 4 ? "float32_abcd" : "bool"));
            } else if (is_virt) {  // virtual points are input registers (FC04) holding numbers
                cJSON_ReplaceItemInObject(o, "fc", cJSON_CreateNumber(4));
                if (strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(o, "dtype")), "bool") == 0) {
                    fail(err, errlen, "Virtual point \"%s\" is an input register: choose a number type, not bool.",
                         cJSON_GetStringValue(cJSON_GetObjectItem(o, "name")), NULL);
                    goto bad;
                }
            }
            const char *nname = cJSON_GetStringValue(cJSON_GetObjectItem(o, "name"));
            if (!name_ok(nname)) {
                fail(err, errlen, "Point name \"%s\" is not valid.", nname, NULL);
                goto bad;
            }
            for (int k = 0; k < nnames; k++) {
                if (strcmp(names[k], nname) == 0) {
                    fail(err, errlen, "Point name \"%s\" is used twice.", nname, NULL);
                    goto bad;
                }
            }
            names[nnames++] = nname;
        }
    }
    if (strict && !slaves_strict(out, err, errlen)) goto bad;
    cJSON_Delete(node_base);
    return out;
bad:
    cJSON_Delete(node_base);
    cJSON_Delete(out);
    return NULL;
}

static cJSON *validate_kind(const cfg_env_t *env, const cfgdef_t *def, const char *name, const cJSON *in, const cJSON *current,
                            bool lenient, bool strict, char *err, size_t errlen);

static cJSON *validate(const cfg_env_t *env, const char *name, const cJSON *in, const cJSON *current, bool strict,
                       char *err, size_t errlen)
{
    const cfgdef_t *def = find(env, name);
    if (!def) return fail(err, errlen, "Unknown settings \"%s\".", name, NULL), NULL;

    // The loader (cfg_load) on the small configs goes field by field and never refuses one (Plan 09b T3):
    // the device runs with the config it is given. Edge's model keeps its own rules.
    bool lenient = !strict && def->kind != K_SLAVES;
    if (lenient) {  // out of memory is not a wrong field: NULL, never a config with keys missing (Codex r3)
        unsigned failures = json_alloc_failures();
        cJSON *out = validate_kind(env, def, name, in, current, lenient, strict, err, errlen);
        if (out && json_alloc_failures() != failures) {
            cJSON_Delete(out);
            fail(err, errlen, "Out of memory.", NULL, NULL);
            return NULL;
        }
        return out;
    }
    return validate_kind(env, def, name, in, current, lenient, strict, err, errlen);
}

static cJSON *validate_kind(const cfg_env_t *env, const cfgdef_t *def, const char *name, const cJSON *in, const cJSON *current,
                            bool lenient, bool strict, char *err, size_t errlen)
{
    switch (def->kind) {
    case K_OBJECT: {
        cJSON *defaults = cJSON_Parse(def->defaults);
        cJSON *out = object(env, name, def->fields, def->nfields, in, current ? current : defaults, lenient, err, errlen);
        cJSON_Delete(defaults);
        if (lenient) return out;  // no cross rules: the UI's
        if (out && strncmp(name, "port", 4) == 0 && cJSON_GetNumberValue(cJSON_GetObjectItem(out, "flowControl")) == 1) {
            // RS485 half duplex: RTS drives DE and there is no CTS line.
            fail(err, errlen, "RTS/CTS flow control is not available on RS485.", NULL, NULL);
            cJSON_Delete(out);
            return NULL;
        }
        ipacl_t acl;
        if (out && strncmp(name, "port", 4) == 0 &&
            !ipacl_parse(cJSON_GetStringValue(cJSON_GetObjectItem(out, "allowedClients")), &acl)) {
            fail(err, errlen, "Allowed clients: up to 8 IPv4 addresses or ranges such as 192.168.1.0/24.", NULL, NULL);
            cJSON_Delete(out);
            return NULL;
        }
        if (out && strcmp(name, "system") == 0) {
            const char *u = cJSON_GetStringValue(cJSON_GetObjectItem(out, "username"));
            const char *p = cJSON_GetStringValue(cJSON_GetObjectItem(out, "password"));
            double rt = cJSON_GetNumberValue(cJSON_GetObjectItem(out, "idleRestart"));
            if (!u || !*u || !p || !*p) {
                fail(err, errlen, "The user name and password cannot be empty.", NULL, NULL);
            } else if (rt != 0 && rt < 60) {
                fail(err, errlen, "Idle restart must be 0 (off) or 60 to 65535 seconds.", NULL, NULL);
            } else {
                return out;
            }
            cJSON_Delete(out);
            return NULL;
        }
        return out;
    }
    case K_TOPICS:
        return list(env, name, def->fields, def->nfields, in, MAX_TOPICS, ITEM_TOPIC, lenient, err, errlen);
    case K_EVENTS:
        return list(env, name, def->fields, def->nfields, in, MAX_EVENTS, ITEM_EVENT, lenient, err, errlen);
    case K_SLAVES:
        return slaves(env, in, strict, err, errlen);
    }
    return NULL;
}

cJSON *cfg_validate(const cfg_env_t *env, const char *name, const cJSON *in, const cJSON *current, char *err,
                    size_t errlen)
{
    return validate(env, name, in, current, true, err, errlen);
}

cJSON *cfg_load(const cfg_env_t *env, const char *name, const cJSON *in, char *err, size_t errlen)
{
    return validate(env, name, in, NULL, false, err, errlen);
}

// ---- Text in, text out ----------------------------------------------------------------
// The STM32 has no PSRAM: a whole Edge model as a cJSON tree does not fit (spec §9.2). These
// functions keep the same rules and messages as the tree-based code above, which stays the
// reference: test/host/test_edge_text.c checks the two against each other byte for byte.

#include "jsonscan.h"

static const char NOT_JSON[] = "The request body is not valid JSON.";


cfg_result_t cfg_validate_text(const cfg_env_t *env, const char *name, const char *text, size_t len,
                               const cJSON *current, char *out, size_t out_size, char *err, size_t errlen)
{
    js_span_t root;
    if (!js_parse_root(text, len, &root)) return fail(err, errlen, NOT_JSON, NULL, NULL), CFG_INVALID;
    unsigned failures = json_alloc_failures();
    cJSON *in = cJSON_ParseWithLength(root.p, root.n);
    if (!in) return fail(err, errlen, "Out of memory.", NULL, NULL), CFG_NO_MEMORY;  // the scanner accepted it
    char why[160] = "";
    cJSON *doc = cfg_validate(env, name, in, current, why, sizeof why);
    cJSON_Delete(in);  // before printing: the peak is the input or the output, not both
    bool fits = doc && cJSON_PrintPreallocated(doc, out, (int)out_size, false) && strlen(out) <= CFG_SMALL_MAX;
    cJSON_Delete(doc);
    if (json_alloc_failures() != failures) return fail(err, errlen, "Out of memory.", NULL, NULL), CFG_NO_MEMORY;
    if (!doc) return fail(err, errlen, "%s", why, NULL), CFG_INVALID;
    if (!fits) return fail(err, errlen, "These settings are too large.", NULL, NULL), CFG_TOO_LARGE;
    return CFG_OK;
}

/** What the save-time rules need to know about one device, kept between passes. */
typedef struct {
    char name[17];
    char addr[13];
    bool dlt, own, virt, io, map_en;
    int map_addr;
} edev_t;

/** What the save-time rules need to know about one point. */
typedef struct {
    int dev;
    char name[17];
    char di[9];
    int fc, reg, regs, map_addr;
    bool is_bool, map_en;
} enode_t;

typedef struct {
    edev_t dev[64];
    enode_t node[MAX_NODES];
    int ndev, nnode;
} esum_t;

typedef struct {
    cfg_emit_fn emit;
    void *ctx;
    size_t used, max;
    cfg_result_t r;
} eout_t;

static bool out_put(eout_t *o, const char *s, size_t n)
{
    if (!o->emit) return true;  // cfg_edge_load: nothing is kept
    if (o->r != CFG_OK) return false;
    if (o->used + n > o->max) return o->r = CFG_TOO_LARGE, false;
    if (!o->emit(o->ctx, s, n)) return o->r = CFG_ABORTED, false;
    o->used += n;
    return true;
}

/** Builds the object cJSON would see, keeping only the schema's keys (first occurrence wins later). */
static cJSON *pick(js_span_t obj, const field_t *fields, size_t n)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    js_iter_t it;
    js_span_t k, v;
    js_iter_init(&it, obj);
    while (js_iter_next(&it, &k, &v)) {
        for (size_t i = 0; i < n; i++) {
            if (!js_key_is(k, fields[i].key, false)) continue;
            cJSON *val = cJSON_ParseWithLength(v.p, v.n);
            if (!val || !cJSON_AddItemToObject(o, fields[i].key, val)) {  // the key copy can fail too
                cJSON_Delete(val);
                cJSON_Delete(o);
                return NULL;
            }
            break;
        }
    }
    return o;
}

/** The value of the first member named "nodes" in any case, as cJSON_GetObjectItem finds it. */
static js_span_t nodes_of(js_span_t dev)
{
    js_iter_t it;
    js_span_t k, v, none = {"", 0};
    if (js_type(dev) != JS_OBJECT) return none;
    js_iter_init(&it, dev);
    while (js_iter_next(&it, &k, &v))
        if (js_key_is(k, "nodes", true)) return v;
    return none;
}

/** One device through the header schema: NULL with `err` set on a schema error, or on no memory. */
static cJSON *edge_header(const cfg_env_t *env, js_span_t dev, const cJSON *base, char *err, size_t errlen)
{
    if (js_type(dev) != JS_OBJECT) return fail(err, errlen, "Expected an object for %s.", "edgeDevices", NULL), NULL;
    cJSON *in = pick(dev, device_fields, N(device_fields));
    if (!in) return NULL;
    cJSON *s = object(env, "edgeDevices", device_fields, N(device_fields), in, base, false, err, errlen);
    cJSON_Delete(in);
    return s;
}

static int unit_of(const edev_t *d) { return d->map_en ? d->map_addr : d->own ? atoi(d->addr) : -1; }

static bool span_of(const edev_t *d, const enode_t *n, int *fc, long *start, int *width)
{
    if (d->dlt && !n->map_en) return false;
    *fc = d->dlt ? 3 : n->fc;
    *start = n->map_en ? n->map_addr : n->reg;
    *width = *fc <= 2 ? 1 : n->regs;
    return true;
}

/** slaves_strict() over the summary, in the same order with the same messages. */
static bool edge_strict(const esum_t *m, char *err, size_t errlen)
{
    for (int i = 0; i < m->ndev; i++) {
        const edev_t *s = &m->dev[i];
        if (s->dlt && !all_digits(s->addr, 12, 12)) {
            return fail(err, errlen, "%s: enter the 12-digit meter address.", s->name, NULL), false;
        }
        if (!s->dlt && !s->io && !(all_digits(s->addr, 1, 3) && atoi(s->addr) >= 1 && atoi(s->addr) <= 255)) {
            return fail(err, errlen, "%s: the slave ID must be 1 to 255.", s->name, NULL), false;
        }
        int unit = unit_of(s);
        if (unit >= 0 && (s->map_en || s->virt)) {
            for (int k = 0; k < m->ndev; k++) {
                if (k != i && unit_of(&m->dev[k]) == unit) {
                    return fail(err, errlen, "%s: its slave ID is already answered for %s.", s->name, m->dev[k].name), false;
                }
            }
        }
        for (int j = 0; j < m->nnode; j++) {
            const enode_t *n = &m->node[j];
            if (n->dev != i) continue;
            if (s->dlt) {
                bool hex = strlen(n->di) == 8;
                for (const char *p = n->di; hex && *p; p++) hex = (*p >= '0' && *p <= '9') || ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f');
                if (!hex) return fail(err, errlen, "%s: the data identifier must be 8 hex digits.", n->name, NULL), false;
                if (n->is_bool) return fail(err, errlen, "%s: a meter value is a number, not bool.", n->name, NULL), false;
            } else if ((n->fc <= 2) != n->is_bool) {
                return fail(err, errlen, n->fc <= 2 ? "%s: coils and discrete inputs are bool." : "%s: registers hold numbers, not bool.",
                            n->name, NULL), false;
            }
            int width = n->fc <= 2 && !s->dlt ? 1 : n->regs;
            if (!s->dlt && n->reg + width > 65536) return fail(err, errlen, "%s: its registers run past 65535.", n->name, NULL), false;
            int xfc, xw;
            long start;
            if (!span_of(s, n, &xfc, &start, &xw)) continue;
            if (start + xw > 65536) return fail(err, errlen, "%s: its mapped registers run past 65535.", n->name, NULL), false;
            if (!n->map_en) continue;
            for (int q = 0; q < m->nnode; q++) {
                const enode_t *o = &m->node[q];
                int ofc, ow;
                long ostart;
                if (o->dev != i || q == j || !span_of(s, o, &ofc, &ostart, &ow) || ofc != xfc) continue;
                if (start < ostart + ow && ostart < start + xw) {
                    return fail(err, errlen, "%s: its mapped registers overlap %s.", n->name, o->name), false;
                }
            }
        }
    }
    return true;
}

/** cJSON_ReplaceItemInObject that frees `item` when it cannot be placed. */
static void put_any(cJSON *dst, const char *key, cJSON *item)
{
    if (!cJSON_ReplaceItemInObject(dst, key, item)) cJSON_Delete(item);
}

/** Prints `doc` into buf without reallocation; false when it does not fit (no memory). */
static bool print_into(const cJSON *doc, char *buf, int size) { return cJSON_PrintPreallocated((cJSON *)doc, buf, size, false); }

// cfg_edge_text and cfg_edge_load: one walk. The visitors see each normalised header and node before it
// is printed; with emit NULL nothing is kept (Plan 08c).
static cfg_result_t edge_walk(const cfg_env_t *env, const char *text, size_t len, bool strict, size_t max_out,
                              cfg_emit_fn emit, void *ctx, cfg_edge_dev_fn on_dev, cfg_edge_node_fn on_node, void *vctx,
                              char *err, size_t errlen)
{
    js_span_t root;
    if (!js_parse_root(text, len, &root)) return fail(err, errlen, NOT_JSON, NULL, NULL), CFG_INVALID;
    if (js_type(root) != JS_ARRAY) return fail(err, errlen, "Expected a list for %s.", "edgeDevices", NULL), CFG_INVALID;
    if (js_count(root) > 64) return fail(err, errlen, "Too many entries in %s.", "edgeDevices", NULL), CFG_INVALID;

    enum { PRINT_MAX = 1024 };
    cfg_result_t r = CFG_NO_MEMORY;
    esum_t *m = cJSON_malloc(sizeof *m);
    char *buf = cJSON_malloc(PRINT_MAX);
    cJSON *dev_base = cJSON_Parse(ITEM_SLAVE), *node_base = cJSON_Parse(ITEM_NODE);
    cJSON *s = NULL, *o = NULL;
    eout_t out = {emit, ctx, 0, max_out, CFG_OK};
    unsigned failures = json_alloc_failures();
    // A failed allocation can leave a field out of an object (object() skips it) or keep an old
    // value (a replacement fails), so check before reading any field of a fresh object.
#define NO_MEMORY_YET() (json_alloc_failures() == failures)
    if (!m || !buf || !dev_base || !node_base || !NO_MEMORY_YET()) goto done;
    m->ndev = m->nnode = 0;
    if (err && errlen) err[0] = '\0';

    // Pass 1, as list(): every device header against the schema, before anything else.
    js_iter_t it;
    js_span_t dev;
    js_iter_init(&it, root);
    while (js_iter_next(&it, NULL, &dev)) {
        s = edge_header(env, dev, dev_base, err, errlen);
        if (!NO_MEMORY_YET()) goto done;
        if (!s) goto bad;
        cJSON_Delete(s);
        s = NULL;
    }

    // Pass 2, as slaves(): each device's own rules, then its points, emitting the normalised text.
    int io = 0, virt = 0, eth = 0;
    // Text past max_out is not kept, but validation goes on: a schema error is the more useful answer.
    // A refusal from the store stops everything at once.
#define PUT(s, n)                                                     \
    do {                                                              \
        if (!out_put(&out, s, n) && out.r == CFG_ABORTED) goto aborted; \
    } while (0)
    PUT("[", 1);
    js_iter_init(&it, root);
    while (js_iter_next(&it, NULL, &dev)) {
        s = edge_header(env, dev, dev_base, err, errlen);
        if (!NO_MEMORY_YET()) goto done;
        if (!s) goto bad;
        const char *src = sget(s, "source");
        bool is_io = strcmp(src, "io") == 0, is_virt = strcmp(src, "virtual") == 0;
        if ((is_io && ++io > 1) || (is_virt && ++virt > 1)) {
            fail(err, errlen, is_io ? "Only one built-in IO device is allowed." : "Only one virtual device is allowed.", NULL, NULL);
            goto bad;
        }
        if (is_io || is_virt) put_any(s, "name", cJSON_CreateString(is_io ? IO_DEVICE : VIRTUAL_DEVICE));
        if (!NO_MEMORY_YET()) goto done;
        const char *sname = sget(s, "name");
        if (!name_ok(sname)) {
            fail(err, errlen, "Device name \"%s\" is not valid.", sname, NULL);
            goto bad;
        }
        if (!is_io && !is_virt && (strcmp(sname, IO_DEVICE) == 0 || strcmp(sname, VIRTUAL_DEVICE) == 0)) {
            fail(err, errlen, "\"%s\" is reserved for the built-in IO and virtual devices.", sname, NULL);
            goto bad;
        }
        for (int k = 0; k < m->ndev; k++) {
            if (strcmp(m->dev[k].name, sname) == 0) {
                fail(err, errlen, "Device name \"%s\" is used twice.", sname, NULL);
                goto bad;
            }
        }
        if (strcmp(src, "eth") == 0 && ++eth > MAX_ETH_SLAVES) {
            fail(err, errlen, "At most 5 Modbus TCP devices.", NULL, NULL);
            goto bad;
        }
        edev_t *d = &m->dev[m->ndev];
        snprintf(d->name, sizeof d->name, "%s", sname);
        snprintf(d->addr, sizeof d->addr, "%s", sget(s, "addr"));
        d->dlt = strcmp(src, "uart") == 0 && strcmp(sget(s, "protocol"), "dlt645") == 0;
        d->own = (strcmp(src, "uart") == 0 && strcmp(sget(s, "protocol"), "modbus") == 0) || strcmp(src, "eth") == 0 || is_virt;
        d->virt = is_virt;
        d->io = is_io;
        d->map_en = bget(s, "mapEn");
        d->map_addr = iget(s, "mapAddr");
        if (on_dev && !on_dev(vctx, s, m->ndev)) goto stopped;
        if (!print_into(s, buf, PRINT_MAX)) goto done;
        cJSON_Delete(s);
        s = NULL;
        if (m->ndev) PUT(",", 1);
        PUT(buf, strlen(buf) - 1);  // the header without its closing brace
        PUT(",\"nodes\":[", 10);

        js_iter_t nit;
        js_span_t node;
        int here = 0;
        js_iter_init(&nit, nodes_of(dev));
        while (js_iter_next(&nit, NULL, &node)) {
            if (m->nnode + 1 > MAX_NODES) {
                fail(err, errlen, "At most 200 points across all devices.", NULL, NULL);
                goto bad;
            }
            if (js_type(node) != JS_OBJECT) {
                fail(err, errlen, "Expected an object for %s.", "edgeDevices", NULL);
                goto bad;
            }
            cJSON *in = pick(node, node_fields, N(node_fields));
            if (!in) goto done;
            o = object(env, "edgeDevices", node_fields, N(node_fields), in, node_base, false, err, errlen);
            cJSON_Delete(in);
            if (!NO_MEMORY_YET()) goto done;
            if (!o) goto bad;
            if (is_io) {
                const char *ch = sget(o, "ioPoint");
                if (!io_channel_ok(env, ch)) {
                    fail(err, errlen, "IO channel \"%s\" does not exist on this gateway.", ch ? ch : "", NULL);
                    goto bad;
                }
                int idx = atoi(ch + 2) - 1, fc = ch[1] == 'O' ? 1 : ch[1] == 'I' && ch[0] == 'D' ? 2 : 4;
                put_any(o, "name", cJSON_CreateString(ch));
                put_any(o, "fc", cJSON_CreateNumber(fc));
                put_any(o, "reg", cJSON_CreateNumber(fc == 4 ? idx * 2 : idx));
                put_any(o, "dtype", cJSON_CreateString(fc == 4 ? "float32_abcd" : "bool"));
            } else if (is_virt) {
                put_any(o, "fc", cJSON_CreateNumber(4));
                if (strcmp(sget(o, "dtype"), "bool") == 0) {
                    fail(err, errlen, "Virtual point \"%s\" is an input register: choose a number type, not bool.", sget(o, "name"), NULL);
                    goto bad;
                }
            }
            if (!NO_MEMORY_YET()) goto done;
            const char *nname = sget(o, "name"), *dtype = sget(o, "dtype");
            if (!name_ok(nname)) {
                fail(err, errlen, "Point name \"%s\" is not valid.", nname, NULL);
                goto bad;
            }
            for (int k = 0; k < m->nnode; k++) {
                if (strcmp(m->node[k].name, nname) == 0) {
                    fail(err, errlen, "Point name \"%s\" is used twice.", nname, NULL);
                    goto bad;
                }
            }
            enode_t *e = &m->node[m->nnode++];
            e->dev = m->ndev;
            snprintf(e->name, sizeof e->name, "%s", nname);
            snprintf(e->di, sizeof e->di, "%s", sget(o, "di"));
            e->fc = iget(o, "fc");
            e->reg = iget(o, "reg");
            e->regs = regs_of(dtype);
            e->is_bool = strcmp(dtype, "bool") == 0;
            e->map_en = bget(o, "mapEn");
            e->map_addr = iget(o, "mapAddr");
            if (on_node && !on_node(vctx, o, m->ndev)) goto stopped;
            if (!print_into(o, buf, PRINT_MAX)) goto done;
            cJSON_Delete(o);
            o = NULL;
            if (here++) PUT(",", 1);
            PUT(buf, strlen(buf));
        }
        PUT("]}", 2);
        m->ndev++;
    }
    PUT("]", 1);
#undef PUT
#undef NO_MEMORY_YET

    // Pass 3, as slaves_strict(): the save-time rules across devices and points.
    if (strict && !edge_strict(m, err, errlen)) goto bad;
    r = out.r;
    if (r == CFG_TOO_LARGE) fail(err, errlen, "The Edge model is too large.", NULL, NULL);
    goto done;
stopped:  // a visitor could not keep it: no memory
    r = CFG_NO_MEMORY;
    goto done;
aborted:
    r = CFG_ABORTED;
    fail(err, errlen, "The Edge model could not be stored.", NULL, NULL);
    goto done;
bad:
    // A failed allocation inside object() leaves no message; a schema error always writes one.
    r = (err && errlen && err[0]) ? CFG_INVALID : CFG_NO_MEMORY;
done:
    // cJSON and object() do not report every failed allocation (a field can silently keep its default).
    if (r != CFG_ABORTED && json_alloc_failures() != failures) r = CFG_NO_MEMORY;
    if (r == CFG_NO_MEMORY) fail(err, errlen, "Out of memory.", NULL, NULL);
    cJSON_Delete(o);
    cJSON_Delete(s);
    cJSON_Delete(node_base);
    cJSON_Delete(dev_base);
    cJSON_free(buf);
    cJSON_free(m);
    return r;
}

cfg_result_t cfg_edge_text(const cfg_env_t *env, const char *text, size_t len, bool strict, size_t max_out,
                           cfg_emit_fn emit, void *ctx, char *err, size_t errlen)
{
    return edge_walk(env, text, len, strict, max_out, emit, ctx, NULL, NULL, NULL, err, errlen);
}

cfg_result_t cfg_edge_load(const cfg_env_t *env, const char *text, size_t len, cfg_edge_dev_fn on_dev, cfg_edge_node_fn on_node,
                           void *ctx, char *err, size_t errlen)
{
    return edge_walk(env, text, len, false, SIZE_MAX, NULL, NULL, on_dev, on_node, ctx, err, errlen);
}

// A port's TCP server listen port, or 0 when its bridge is not a TCP server.
static int tcp_listen(const cJSON *port)
{
    const cJSON *w = cJSON_GetObjectItemCaseSensitive(port, "workMode"), *l = cJSON_GetObjectItemCaseSensitive(port, "listenPort");
    return cJSON_IsNumber(w) && w->valueint == 3 && cJSON_IsNumber(l) ? l->valueint : 0;
}

bool cfg_listen_ok(const cJSON *port1, const cJSON *port2, const cJSON *system, char *err, size_t errlen)
{
    int l1 = tcp_listen(port1), l2 = tcp_listen(port2);
    if (l1 && l1 == l2) {
        snprintf(err, errlen, "UART1 and UART2 both listen on TCP port %d: give each its own port.", l1);
        return false;
    }
    static const char *const KEYS[] = {"webPort", "websocketPort"}, *const WHAT[] = {"the web interface", "the WebSocket console"};
    for (int k = 0; k < 2; k++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(system, KEYS[k]);
        int p = cJSON_IsNumber(v) ? v->valueint : 0;
        for (int n = 1; n <= 2; n++) {
            int l = n == 1 ? l1 : l2;
            if (p && l == p) {
                snprintf(err, errlen, "UART%d listens on TCP port %d, which %s uses: choose another port.", n, p, WHAT[k]);
                return false;
            }
        }
    }
    return true;
}
