// Sockets for the bridges (the ESP32's net.c, Plan 08a), on lwIP's BSD sockets. Every socket the bridges
// keep is non-blocking: a send takes what it can (spec §5.6). lwIP's own names everywhere: with
// LWIP_POSIX_SOCKETS_IO_NAMES 0, close() and fcntl() would be newlib's (closesocket, lwip_fcntl).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bridge_core.h"  // NET_AGAIN, NET_FAIL
#include "lwip/sockets.h"

typedef struct {
    int fd;
    void *tls;  // Plan 08b; NULL until then
} conn_t;

bool net_resolve(const char *host, uint16_t port, struct sockaddr_in *out);
/** TCP connect with a timeout, binding local_port first when non-zero. A non-blocking fd, or -1. */
int net_tcp_connect(const char *host, uint16_t port, uint16_t local_port, uint32_t timeout_ms);
void net_nonblock(int fd);
/** What the socket takes now: >0 bytes, NET_AGAIN or NET_FAIL (net_classify). */
int conn_send_some(conn_t *c, const uint8_t *data, size_t len);
/** All of it within timeout_ms, sleeping a tick between tries (small replies, HTTP). False otherwise. */
bool conn_send_all(conn_t *c, const void *data, size_t len, uint32_t timeout_ms);
/** >0 bytes, 0 closed by the peer, NET_AGAIN nothing yet, NET_FAIL. */
int conn_recv(conn_t *c, uint8_t *buf, size_t max);
void conn_close(conn_t *c);
