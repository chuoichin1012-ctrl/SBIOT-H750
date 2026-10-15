// What Mongoose takes from the board.
#include "board_rng.h"
#include "mongoose.h"

// WebSocket masks and ids; the hardware RNG, or LWIP_RAND's fallback when it fails.
bool mg_random(void *buf, size_t len)
{
    if (board_rng(buf, len) == 0)
        return true;
    uint8_t *p = buf;
    for (size_t i = 0; i < len; i++)
        p[i] = (uint8_t)board_rand32();
    return true;
}
