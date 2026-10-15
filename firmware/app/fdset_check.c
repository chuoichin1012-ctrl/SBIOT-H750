// Plan 06a: Mongoose (06b) includes newlib's sys/select.h before lwIP's sockets. lwIP then keeps
// newlib's fd_set, and needs FD_SETSIZE >= LWIP_SOCKET_OFFSET + MEMP_NUM_NETCONN (the pool table,
// spec §5.6). lwIP stops with an #error otherwise; the assert says why.
#include <sys/select.h>
#include "lwip/sockets.h"

_Static_assert(FD_SETSIZE >= LWIP_SOCKET_OFFSET + MEMP_NUM_NETCONN, "newlib's FD_SETSIZE below lwIP's sockets");
