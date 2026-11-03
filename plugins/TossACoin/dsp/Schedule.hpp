#pragma once
#include <cmath>
#include "Rng.hpp"

namespace toss {

enum class Steps : int { Glide = 0, Semitones = 1, FifthsOctaves = 2, Octaves = 3 };

struct DropParams {
    float f0;       // detected note, Hz
    float bounce;   // each gap as a fraction of the previous one (0.70-0.97)
    float height;   // first gap for E2 (82.4 Hz), s
    float pitch;    // total pitch travel over the drop, semitones (sign = direction)
    float glitch;   // chance per bounce of a random jump, 0-1
    Steps steps;
    bool  reverse;
};

struct Bounce { float time; float amp; float semis; };   // time: s after the attack

constexpr int kMaxBounces = 256;

// Snap a semitone offset to the notes Steps allows (mirrored for downward).
inline float snapSemis(float s, Steps steps)
{
    static const float kFifths[]  = { 0, 7, 12, 19, 24, 31, 36 };
    static const float kOctaves[] = { 0, 12, 24, 36 };
    const float* grid; int n;
    switch (steps)
    {
    case Steps::Glide:     return s;
    case Steps::Semitones: return std::round(s);
    case Steps::FifthsOctaves: grid = kFifths;  n = 7; break;
    default:                   grid = kOctaves; n = 4; break;
    }
    const float a = std::fabs(s);
    float best = grid[0];
    for (int i = 1; i < n; ++i)
        if (std::fabs(grid[i] - a) < std::fabs(best - a)) best = grid[i];
    return s < 0.0f ? -best : best;
}

// One drop's bounces (computed once, when the pitch is known). Gaps shrink
// geometrically from T1 = height * sqrt(82.4 / f0); jitter is applied to each
// gap, never to the cumulative time (that made the prototype's bounces clump
// into pairs). No transcendental work per bounce: amplitudes are running products.
inline int makeSchedule(const DropParams& p, float notBefore, Rng& rng, Bounce* out)
{
    float gaps[kMaxBounces];
    int n = 0;
    float g = p.height * std::sqrt(82.4f / p.f0), a = p.bounce;
    while (n < kMaxBounces && g >= 0.008f && a >= 0.001f)
    {
        gaps[n++] = g;
        g *= p.bounce;
        a *= p.bounce;
    }
    if (n == 0) return 0;
    if (p.reverse)
        for (int i = 0, j = n - 1; i < j; ++i, --j) { const float t = gaps[i]; gaps[i] = gaps[j]; gaps[j] = t; }

    // reverse fades as bounce^((k+1)/2) so its slow end stays audible
    const float decay = p.reverse ? std::sqrt(p.bounce) : p.bounce;
    float t = 0.0f, amp = 1.0f, off = 0.0f;
    for (int k = 0; k < n; ++k)
    {
        t   += gaps[k] * rng.uniform(0.97f, 1.03f);
        amp *= decay;
        out[k].time = t;
        out[k].amp  = amp * rng.uniform(0.8f, 1.1f);

        float s = p.pitch * float(k + 1) / float(n);
        float oneOff = 0.0f;
        if (p.glitch > 0.0f && rng.uniform() < p.glitch)
        {
            const float j = rng.uniform(-12.0f, 12.0f);
            if (rng.uniform() < 0.5f) oneOff = j;   // jumps back on the next bounce
            else                      off += j;     // carries on from there
        }
        s += off + oneOff;
        s = s > 36.0f ? 36.0f : (s < -36.0f ? -36.0f : s);
        out[k].semis = snapSemis(s, p.steps);
    }

    const float shift = notBefore - out[0].time;
    if (shift > 0.0f)
        for (int k = 0; k < n; ++k) out[k].time += shift;
    return n;
}

} // namespace toss
