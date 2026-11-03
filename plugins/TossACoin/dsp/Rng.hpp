#pragma once
#include <cstdint>

namespace toss {

// xorshift32: tiny, allocation-free, deterministic per seed (tests rely on it).
struct Rng {
    explicit Rng(uint32_t seed = 1) : s(seed ? seed : 1u) {}

    // Uniform in [0, 1): top 24 bits -> exactly representable floats below 1.
    float uniform()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return float(s >> 8) * (1.0f / 16777216.0f);
    }
    float uniform(float a, float b) { return a + (b - a) * uniform(); }

    uint32_t s;
};

} // namespace toss
