#include "rng_core.h"
#include <stdbool.h>

static bool wait_drdy(const rng_io_t *io)
{
    for (unsigned i = 0; i < io->spins; i++)
        if (io->sr(io->ctx) & RNG_DRDY)
            return true;
    return false;
}

// RM0433 §34.3.7: clear SEIS, read and drop 12 words to clean the pipeline, then SEIS must stay clear.
static int seed_recover(const rng_io_t *io)
{
    io->clear_seis(io->ctx);
    for (int i = 0; i < 12; i++) {
        if (!wait_drdy(io))
            return -1;
        (void)io->dr(io->ctx);
    }
    return io->sr(io->ctx) & (RNG_SEIS | RNG_SECS) ? -1 : 0;
}

int rng_word(const rng_io_t *io, uint32_t *w)
{
    bool recovered = false;  // one seed recovery per word at most
    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t sr = io->sr(io->ctx);
        if (sr & RNG_CECS)
            return -1;  // the clock is out of range: no word is trusted
        if (sr & (RNG_SEIS | RNG_SECS)) {
            if (recovered || seed_recover(io))
                return -1;
            recovered = true;
            continue;
        }
        if (!wait_drdy(io))
            return -1;
        uint32_t v = io->dr(io->ctx);
        // An error that shows with DRDY, or right after the read, may have spoiled this word: drop it.
        if (io->sr(io->ctx) & (RNG_SEIS | RNG_SECS | RNG_CECS))
            continue;
        *w = v;
        return 0;
    }
    return -1;
}
