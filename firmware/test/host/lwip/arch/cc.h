#pragma once
#include <stdio.h>
#include <stdlib.h>
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { printf("lwIP assert: %s at %s:%d\n", x, __FILE__, __LINE__); abort(); } while (0)
