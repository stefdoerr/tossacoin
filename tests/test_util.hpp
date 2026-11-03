// Shared helpers for the host-less DSP tests (each test is a plain main()).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>

inline int& failures() { static int f = 0; return f; }

#define CHECK(cond) do { if (!(cond)) { failures()++; \
    std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define REPORT() do { \
    if (failures()) { std::printf("FAILED (%d failed checks)\n", failures()); return 1; } \
    std::printf("OK\n"); return 0; } while (0)

// Guitar-ish pluck: 8 harmonics at 1/k amplitude, 5 ms raised-cosine attack,
// exponential decay (time constant `decay` s), normalised so the peak is `peak`.
inline float pluck(float hz, float peak, double fs, uint32_t i, double decay = 0.3)
{
    static double norm = 0.0;
    if (norm == 0.0)                       // peak of sum_k sin(k th)/k over one cycle
        for (int j = 0; j < 4096; ++j)
        {
            double s = 0.0, th = 2.0 * M_PI * j / 4096.0;
            for (int k = 1; k <= 8; ++k) s += std::sin(k * th) / k;
            norm = std::fmax(norm, std::fabs(s));
        }
    const double t = i / fs, att = 0.005;
    const double env = t < att ? 0.5 * (1.0 - std::cos(M_PI * t / att)) : std::exp(-(t - att) / decay);
    double s = 0.0;
    for (int k = 1; k <= 8; ++k) s += std::sin(2.0 * M_PI * k * hz * t) / k;
    return float(peak * env * s / norm);
}

// Energy of x at frequency hz (Goertzel), normalised by n.
inline double goertzel(const float* x, uint32_t n, double hz, double fs)
{
    const double w = 2.0 * M_PI * hz / fs, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (uint32_t i = 0; i < n; ++i) { const double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

inline double rms(const float* x, uint32_t n)
{
    double sum = 0.0;
    for (uint32_t i = 0; i < n; ++i) sum += double(x[i]) * x[i];
    return std::sqrt(sum / n);
}
