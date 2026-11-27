/* SPDX-License-Identifier: MIT */
#include "arcade/arcade_common.h"

void arcade_rng_seed(arcade_rng_t *rng, uint32_t seed)
{
    /* xorshift32 绝不能用 0 作为种子。 */
    rng->state = (seed == 0u) ? 0x9E3779B9u : seed;
    /* 先混洗一次，让相近的种子立刻分叉。 */
    (void)arcade_rng_next(rng);
}

uint32_t arcade_rng_next(arcade_rng_t *rng)
{
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

int32_t arcade_rng_below(arcade_rng_t *rng, int32_t bound)
{
    if (bound <= 0) {
        return 0;
    }
    /* 在这种量级下（bound <= 1024），取模偏差可以忽略。 */
    return (int32_t)(arcade_rng_next(rng) % (uint32_t)bound);
}
