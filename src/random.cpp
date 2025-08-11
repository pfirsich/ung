#include "types.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>

#define EXPORT extern "C"

namespace ung::random {

uint64_t state;

// clang-format off
/* SplitMix64
 *
 * I spent so much time researching which algorithm to use considering the use case it doesn't
 * really matter.
 * The final round of contenders were:
 * - PCG: https://www.pcg-random.org/
 * - xoshiro256++: https://prng.di.unimi.it/
 * - SplitMix64
 * Here are some links that compare them:
 * - https://lemire.me/blog/2017/08/22/testing-non-cryptographic-random-number-generators-my-results/
 * - https://arvid.io/2018/07/02/better-cxx-prng/
 * - https://markus-seidl.de/unity-generalrandom/doc/rngs/
 * And after much deliberation I have come to the conclusion that they are all fine.
 * Their statistical properties are all surprisingly code given their complexity
 * and they are all certainly "good enough" for what I am doing with them.
 * I also don't need gigabytes of random data per second.
 * I chose splitmix because the implementation is very short and only has 64-bit state.
 */
// clang-format on
static uint64_t random(uint64_t& s)
{
    uint64_t z = (s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// I only do this because it's much better than float(rand())/max and it's easy to do
// You essentially fix the exponent to 2^0 (0x7f << 23) and then fill up the mantissa with
// some of the bits of the random number.
// The result is between [1, 2], so you have to subtract one.
static float randomf(uint64_t& s)
{
    const auto r = random(s);
    const uint32_t bits = (0x7ful << 23) | static_cast<uint32_t>(r >> (32 + 9));
    return std::bit_cast<float>(bits) - 1.0f;
}

void init()
{
    std::random_device rd;
    state = rd();
}

EXPORT uint64_t ung_random_get_state()
{
    return state;
}

EXPORT void ung_random_set_state(uint64_t s)
{
    state = s;
}

EXPORT uint64_t ung_random_u64()
{
    return random(state);
}

EXPORT uint64_t ung_random_u64_s(uint64_t* s)
{
    return random(*s);
}

EXPORT uint64_t ung_random_uint(uint64_t min, uint64_t max)
{
    return ung_random_uint_s(min, max, &state);
}

EXPORT uint64_t ung_random_uint_s(uint64_t min, uint64_t max, uint64_t* s)
{
    assert(min <= max);
    if (min == max) {
        return min;
    }

    // entire i64 range => reinterpret random bits
    if (min == 0 && max == UINT64_MAX) {
        return random(*s);
    }

    // +1 will not overflow, because the full-range case is handled above
    const auto range = max - min + 1ull;
    // largest multiple of range
    const auto thresh = UINT64_MAX / range * range;

    // to avoid modulo bias we reroll if the value is not a perfect multiple of `range`
    while (true) {
        const auto r = random(*s);
        // <, so we accept a multiple of `range` different values (including 0)
        if (r < thresh) [[likely]] {
            return min + (r % range);
        }
    }
}

EXPORT int64_t ung_random_int(int64_t min, int64_t max)
{
    return ung_random_int_s(min, max, &state);
}

EXPORT int64_t ung_random_int_s(int64_t min, int64_t max, uint64_t* s)
{
    assert(min <= max);
    if (min == max) {
        return min;
    }

    if (min == INT64_MIN && max == INT64_MAX) {
        return std::bit_cast<int64_t>(random(*s));
    }

    // $e do math with unsigned range because in twos-complement
    // -N is stored as 2^64 - N and unsigned overflow is well defined and
    // N is equivalent to N + 2^64.
    // So regardless of whether min/max are positive or negative:
    // max - min = (umax + 2^64) - (umin + 2^64) = umax - umin.
    const auto umin = (uint64_t)min;
    const auto umax = (uint64_t)max;
    const auto range = umax - umin + 1ull;
    const auto thresh = UINT64_MAX / range * range;

    while (true) {
        const auto r = random(*s);
        if (r < thresh) [[likely]] {
            return std::bit_cast<int64_t>(umin + (r % range));
        }
    }
}

EXPORT float ung_random_float(float min, float max)
{
    return ung_random_float_s(min, max, &state);
}

EXPORT float ung_random_float_s(float min, float max, uint64_t* s)
{
    return min + randomf(*s) * (max - min);
}
}