// coreMQTT's transport (Plan 09); see mqtt_transport.h.
#include "mqtt_transport.h"
#include "bridge_core.h"  // NET_AGAIN, NET_FAIL

// Nothing moved: count it, and from the second time in a row give the CPU up even if select came back at once.
static int32_t none_yet(NetworkContext_t *nc)
{
    if (++nc->idle >= 2)
        nc->link->tick();
    return 0;
}

int32_t mqtt_tx(NetworkContext_t *nc, const void *buf, size_t n)
{
    const mqtt_link_t *l = nc->link;
    int r = l->send(l->conn, buf, n);
    if (r > 0) {
        nc->idle = 0;
        return r;
    }
    if (r != NET_AGAIN)
        return -1;
    l->wait(l->conn, true, MQTT_WAIT_MS);  // always: bytes held for reading say nothing about room to send
    return none_yet(nc);
}

int32_t mqtt_rx(NetworkContext_t *nc, void *buf, size_t n)
{
    const mqtt_link_t *l = nc->link;
    int r = l->recv(l->conn, buf, n);
    if (r > 0) {
        nc->idle = 0;
        return r;
    }
    if (r != NET_AGAIN)
        return -1;  // 0: closed by the peer (close-notify too); NET_FAIL
    if (!l->pending(l->conn))
        l->wait(l->conn, false, MQTT_WAIT_MS);
    return none_yet(nc);
}
