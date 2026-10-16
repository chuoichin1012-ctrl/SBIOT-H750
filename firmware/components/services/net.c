#include "svc_net.h"
#include <errno.h>
#include <string.h>
#include "FreeRTOS.h"
#include "lwip/netdb.h"
#include "now_us.h"
#include "syslog.h"
#include "task.h"

bool net_resolve(const char *host, uint16_t port, struct sockaddr_in *out)
{
    memset(out, 0, sizeof *out);
    out->sin_family = AF_INET;
    out->sin_port = lwip_htons(port);
    if (!host || !*host)
        return false;
    if (inet_aton(host, &out->sin_addr))
        return true;
    const struct addrinfo hints = {.ai_family = AF_INET};
    struct addrinfo *res = NULL;
    if (lwip_getaddrinfo(host, NULL, &hints, &res) != 0 || !res) {
        SLOG_W("net", "cannot resolve %s", host);
        return false;
    }
    out->sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    lwip_freeaddrinfo(res);
    return true;
}

void net_nonblock(int fd) { lwip_fcntl(fd, F_SETFL, lwip_fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

int net_tcp_connect(const char *host, uint16_t port, uint16_t local_port, uint32_t timeout_ms)
{
    struct sockaddr_in to;
    if (!net_resolve(host, port, &to))
        return -1;
    int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;
    int one = 1;
    lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (local_port) {
        struct sockaddr_in me = {.sin_family = AF_INET, .sin_port = lwip_htons(local_port)};
        if (lwip_bind(fd, (struct sockaddr *)&me, sizeof me) != 0) {
            SLOG_W("net", "cannot bind local port %u", local_port);
            closesocket(fd);
            return -1;
        }
    }
    net_nonblock(fd);
    int r = lwip_connect(fd, (struct sockaddr *)&to, sizeof to), why = r ? errno : 0, err = 0;
    if (r != 0 && why == EINPROGRESS) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(fd, &w);
        struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
        socklen_t el = sizeof err;
        r = lwip_select(fd + 1, NULL, &w, NULL, &tv) == 1 && lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && !err ? 0 : -1;
        if (r)
            why = err ? err : ETIMEDOUT;
    }
    if (r != 0) {
        static uint64_t s_logged_us;  // one line a minute: a dead peer is retried every few seconds
        if (now_us() - s_logged_us > 60000000u || !s_logged_us) {
            s_logged_us = now_us();
            SLOG_I("net", "cannot connect to %s:%u (errno %d)", host, port, why);
        }
        closesocket(fd);
        return -1;
    }
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

int conn_send_some(conn_t *c, const uint8_t *data, size_t len)
{
    if (c->fd < 0)
        return NET_FAIL;
    if (c->tls)
        return tls_send_some(c->tls, data, len);  // after NET_AGAIN, called again with the same data
    int r = lwip_send(c->fd, data, len, MSG_DONTWAIT);
    return r > 0 ? r : r == 0 ? NET_AGAIN : net_classify(errno);
}

bool conn_send_all(conn_t *c, const void *data, size_t len, uint32_t timeout_ms)
{
    const uint8_t *p = data;
    uint64_t until = now_us() + (uint64_t)timeout_ms * 1000u;
    while (len) {
        int r = conn_send_some(c, p, len);
        if (r == NET_FAIL)
            return false;
        if (r > 0) {
            p += r;
            len -= (size_t)r;
        } else if (now_us() >= until) {
            return false;
        } else {
            vTaskDelay(1);
        }
    }
    return true;
}

int conn_recv(conn_t *c, uint8_t *buf, size_t max)
{
    if (c->fd < 0)
        return NET_FAIL;
    if (c->tls)
        return tls_recv(c->tls, buf, max);
    int r = lwip_recv(c->fd, buf, max, MSG_DONTWAIT);
    if (r < 0)
        return net_classify(errno);
    return r;
}

bool conn_pending(const conn_t *c) { return c->tls && tls_pending(c->tls); }

void conn_close(conn_t *c)
{
    tls_close(c->tls);
    c->tls = NULL;
    if (c->fd >= 0)
        closesocket(c->fd);
    c->fd = -1;
}
