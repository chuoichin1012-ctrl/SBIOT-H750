// Mongoose 7.23 settings of the app (spec §10.1): FreeRTOS with lwIP sockets, no TLS, no file system.
#pragma once
#define MG_ARCH MG_ARCH_ARMGCC
#define MG_ENABLE_FREERTOS 1    // mg_calloc/mg_free on pvPortMalloc: the one heap_5 heap (spec §5.5)
#define MG_ENABLE_LWIP 1
#define MG_ENABLE_TCPIP 0
#define MG_TLS MG_TLS_NONE
#define MG_ENABLE_IPV6 0
#define MG_ENABLE_LOG 0
#define MG_ENABLE_POSIX_FS 0
#define MG_ENABLE_DIRLIST 0
#define MG_ENABLE_SSI 0
#define MG_ENABLE_CUSTOM_RANDOM 1  // mg_random: board_rng (webapi/mg_port.c)
#define MG_IO_SIZE 1460
#define MG_MAX_RECV_SIZE (12 * 1024)  // a 10 240-byte body and its headers; larger bodies are streamed
