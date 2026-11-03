// Synth coin: a ringing modal plate tuned two octaves above the note,
// folded below 8 kHz, bounded voice pool that steals the quietest ring.
#include "test_util.hpp"
#include "dsp/CoinSynth.hpp"

#include <vector>

using namespace toss;

static const double kFs = 48000.0;

static std::vector<float> render(CoinSynth& cs, uint32_t n)
{
    std::vector<float> y(n);
    for (uint32_t i = 0; i < n; ++i) y[i] = cs.process();
    return y;
}

int main()
{
    {   // fundamental_values
        CHECK(std::fabs(coinFundamental(82.4f, 0.0f) - 329.6f) < 0.1f);
        CHECK(std::fabs(coinFundamental(1046.5f, 12.0f) - 4186.0f) < 0.5f);   // 8372 folded
        CHECK(coinFundamental(1500.0f, 24.0f) <= 8000.0f);
    }
    {   // rings_at_fc, decays
        CoinSynth cs; cs.prepare(kFs, 32);
        Rng rng(3);
        cs.hit(220.0f, 0.0f, 0.1f, rng);
        const auto y = render(cs, uint32_t(0.5 * kFs));
        const uint32_t n200 = uint32_t(0.2 * kFs), n100 = uint32_t(0.1 * kFs);
        CHECK(goertzel(y.data(), n200, 880.0, kFs) >= 10.0 * goertzel(y.data(), n200, 932.0, kFs));
        CHECK(rms(y.data(), n100) >= 3.0 * rms(y.data() + 4 * n100, n100));
    }
    {   // amp_scales
        float peak = 0.0f;
        for (uint32_t seed = 1; seed <= 20; ++seed)
        {
            CoinSynth cs; cs.prepare(kFs, 32);
            Rng rng(seed);
            cs.hit(220.0f, 0.0f, 0.1f, rng);
            float p = 0.0f;
            for (float v : render(cs, uint32_t(0.2 * kFs))) p = std::fmax(p, std::fabs(v));
            CHECK(p >= 0.02f && p <= 0.15f);
            peak = std::fmax(peak, p);
        }
        std::printf("  max single-hit peak at amp 0.1: %.3f\n", peak);
    }
    {   // cap_and_steal, frees_itself
        CoinSynth cs; cs.prepare(kFs, 32);
        Rng rng(5);
        bool capped = true, finite = true;
        float peak = 0.0f;
        for (uint32_t i = 0; i < uint32_t(kFs); ++i)
        {
            if (i % 24 == 0) cs.hit(rng.uniform(40.0f, 1500.0f), rng.uniform(-24.0f, 24.0f), 0.1f, rng);
            const float y = cs.process();
            capped = capped && cs.activeRings() <= 32;
            finite = finite && std::isfinite(y);
            peak = std::fmax(peak, std::fabs(y));
        }
        CHECK(capped);
        CHECK(finite);
        CHECK(peak < 2.0f);
        std::printf("  stress peak: %.3f\n", peak);
        render(cs, uint32_t(3 * kFs));
        CHECK(cs.activeRings() == 0);
    }
    REPORT();
}
