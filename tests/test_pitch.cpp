// Incremental YIN: accurate over 40 Hz-1.5 kHz, no octave error on a weak
// fundamental, fast enough to hide behind the first bounce, silent on noise.
#include "test_util.hpp"
#include "dsp/PitchDetector.hpp"
#include "dsp/Rng.hpp"

#include <functional>

struct Result { float hz; double latency; bool done; };

// Feed 128-sample blocks (push, then work(64)) like the plugin does at 48 kHz.
static Result detect(double fs, std::function<float(uint32_t)> sig)
{
    toss::PitchDetector pd;
    pd.prepare(fs);
    pd.start();
    uint32_t i = 0;
    while (i < uint32_t(0.3 * fs))
    {
        for (int k = 0; k < 128; ++k, ++i) pd.push(sig(i));
        pd.work(64);
        if (pd.done()) return { pd.hz(), i / fs, true };
    }
    return { pd.hz(), i / fs, false };
}

static bool within(float hz, float want, float tol) { return std::fabs(hz / want - 1.0f) <= tol; }

int main()
{
    const double fs = 48000.0;

    // accuracy
    for (float f : { 41.2f, 82.4f, 110.0f, 261.6f, 440.0f, 1046.5f, 1400.0f })
    {
        const Result r = detect(fs, [&](uint32_t i) { return pluck(f, 0.1f, fs, i); });
        if (!(r.done && within(r.hz, f, 0.01f)))
            std::printf("  accuracy %.1f Hz -> %.2f Hz (done=%d)\n", f, r.hz, r.done);
        CHECK(r.done && within(r.hz, f, 0.01f));
    }

    {   // weak_fundamental: guitar low E often has a weaker fundamental than 2nd harmonic
        const float f = 82.4f, amps[4] = { 0.3f, 1.0f, 0.3f, 0.2f };
        const Result r = detect(fs, [&](uint32_t i) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += amps[k] * std::sin(2.0 * M_PI * (k + 1) * f * i / fs);
            return float(0.05 * s * std::exp(-double(i) / (0.3 * fs)));
        });
        if (!within(r.hz, f, 0.01f)) std::printf("  weak fundamental -> %.2f Hz\n", r.hz);
        CHECK(r.done && within(r.hz, f, 0.01f));
    }

    {   // latency: hidden behind the first bounce
        const float fr[3] = { 1046.5f, 82.4f, 41.2f };
        const double lim[3] = { 0.050, 0.075, 0.100 };
        for (int k = 0; k < 3; ++k)
        {
            const Result r = detect(fs, [&](uint32_t i) { return pluck(fr[k], 0.1f, fs, i); });
            std::printf("  latency %.1f Hz: %.1f ms\n", fr[k], r.latency * 1000.0);
            CHECK(r.done && r.latency <= lim[k]);
        }
    }

    {   // noise_is_unpitched
        toss::Rng rng(7);
        const Result r = detect(fs, [&](uint32_t) { return rng.uniform(-0.1f, 0.1f); });
        CHECK(r.done);
        CHECK(r.hz == 0.0f);
    }

    {   // fs_44100
        const Result r = detect(44100.0, [&](uint32_t i) { return pluck(110.0f, 0.1f, 44100.0, i); });
        CHECK(r.done && within(r.hz, 110.0f, 0.01f));
    }

    REPORT();
}
