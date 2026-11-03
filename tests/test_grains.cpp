// Sampled mode: the player's own attack, chopped and repitched per bounce.
#include "test_util.hpp"
#include "dsp/Grains.hpp"

#include <cstdlib>
#include <vector>

using namespace toss;

static const double kFs = 48000.0;

static float sine(double hz, double amp, uint32_t i) { return float(amp * std::sin(2.0 * M_PI * hz * i / kFs)); }

// Index of the last sample with |y| above 1e-6, or -1.
static long lastSound(const std::vector<float>& y)
{
    for (long i = long(y.size()) - 1; i >= 0; --i)
        if (std::fabs(y[size_t(i)]) > 1e-6f) return i;
    return -1;
}

static std::vector<float> play(Grains& g, uint32_t n)
{
    std::vector<float> y(n);
    for (uint32_t i = 0; i < n; ++i) y[i] = g.process();
    return y;
}

int main()
{
    const uint32_t L = uint32_t(0.12 * kFs);   // 5760

    {   // octave_up_doubles_pitch
        Grains g; g.prepare(kFs, 6, 16);
        g.clearDrop(0);
        for (uint32_t i = 0; i < L; ++i) g.record(0, sine(200.0, 0.1, i));
        CHECK(g.recorded(0) == int(L));
        g.lockLength(0, int(L));
        g.hit(0, 12.0f, 1.0f, 1.0f);
        const auto y = play(g, L);
        CHECK(goertzel(y.data(), L / 2, 400.0, kFs) >= 10.0 * goertzel(y.data(), L / 2, 200.0, kFs));
        const long last = lastSound(y);
        std::printf("  octave-up grain length %ld (L/2 = %u)\n", last + 1, L / 2);
        CHECK(std::abs(last + 1 - long(L / 2)) <= 64);
    }
    {   // truncates
        Grains g; g.prepare(kFs, 6, 16);
        g.clearDrop(0);
        for (uint32_t i = 0; i < L; ++i) g.record(0, sine(200.0, 0.1, i));
        g.lockLength(0, int(L));
        g.hit(0, 0.0f, 1.0f, 0.01f);
        CHECK(lastSound(play(g, L)) < long(480 + 64));
    }
    {   // respects_recorded: only 0.05 s recorded -> never plays past it
        Grains g; g.prepare(kFs, 6, 16);
        g.clearDrop(0);
        for (uint32_t i = 0; i < 2400; ++i) g.record(0, sine(200.0, 0.1, i));
        g.lockLength(0, int(L));
        g.hit(0, 0.0f, 1.0f, 1.0f);
        CHECK(lastSound(play(g, L)) < 2400);
    }
    {   // clearDrop_fades
        Grains g; g.prepare(kFs, 6, 16);
        g.clearDrop(0);
        for (uint32_t i = 0; i < L; ++i) g.record(0, sine(200.0, 0.1, i));
        g.lockLength(0, int(L));
        g.hit(0, 0.0f, 1.0f, 1.0f);
        auto y = play(g, 2000);
        g.clearDrop(0);                      // a new attack steals the slot
        for (uint32_t i = 0; i < 400; ++i)
        {
            g.record(0, sine(1000.0, 0.5, i));
            y.push_back(g.process());
        }
        float step = 0.0f;
        for (size_t i = 1990; i < y.size(); ++i) step = std::fmax(step, std::fabs(y[i] - y[i - 1]));
        CHECK(step < 0.05f);
        CHECK(lastSound(y) < 2000 + 64);
    }
    {   // frees_itself
        Grains g; g.prepare(kFs, 6, 16);
        g.clearDrop(0);
        for (uint32_t i = 0; i < L; ++i) g.record(0, sine(200.0, 0.1, i));
        g.lockLength(0, int(L));
        for (int k = 0; k < 40; ++k) g.hit(0, float(k % 13), 0.5f, 0.2f);   // more hits than players
        CHECK(g.activePlayers() <= 16);
        play(g, uint32_t(kFs));
        CHECK(g.activePlayers() == 0);
    }
    REPORT();
}
