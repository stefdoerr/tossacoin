// Coin engine: hard attack -> pitch -> schedule -> coins, end to end.
#include "test_util.hpp"
#include "dsp/CoinEngine.hpp"

#include <functional>
#include <vector>

using namespace toss;

static EngineParams defaults()
{
    EngineParams p;
    p.thresholdDb = sensitivityToThresholdDb(50.0f);
    p.bounce = 0.92f; p.height = 0.5f; p.pitch = 12.0f; p.glitch = 0.0f;
    p.steps = Steps::Semitones; p.reverse = false; p.sampled = false;
    return p;
}

struct Run {
    std::vector<float> wet;
    int maxDrops = 0, maxRings = 0;
    bool finite = true;
    float peak = 0.0f, maxStep = 0.0f;
};

// Drive the engine like the plugin: per 128-frame block setParams + work, then per-sample process.
// `tweak` may change params before each block.
static Run run(CoinEngine& e, double fs, uint32_t n, std::function<float(uint32_t)> in,
               EngineParams p, std::function<void(EngineParams&, uint32_t)> tweak = nullptr)
{
    Run r;
    r.wet.resize(n);
    for (uint32_t b = 0; b < n; b += 128)
    {
        if (tweak) tweak(p, b);
        e.setParams(p);
        const uint32_t len = n - b < 128 ? n - b : 128;
        e.work(len);
        for (uint32_t i = b; i < b + len; ++i)
        {
            const float y = e.process(in(i));
            r.wet[i] = y;
            r.finite = r.finite && std::isfinite(y);
            r.peak = std::fmax(r.peak, std::fabs(y));
            if (i) r.maxStep = std::fmax(r.maxStep, std::fabs(y - r.wet[i - 1]));
        }
        r.maxDrops = std::max(r.maxDrops, e.activeDrops());
        r.maxRings = std::max(r.maxRings, e.activeRings());
    }
    return r;
}

static double firstWet(const std::vector<float>& w, double fs, uint32_t from = 0)
{
    for (uint32_t i = from; i < w.size(); ++i)
        if (w[i] != 0.0f) return (i - from) / fs;
    return -1.0;
}

static bool near(double got, double want, double tol) { return std::fabs(got / want - 1.0) <= tol; }

int main()
{
    const double fs = 48000.0;
    const double T110 = 0.5 * std::sqrt(82.4 / 110.0), T220 = 0.5 * std::sqrt(82.4 / 220.0);

    {   // soft_note_no_wet
        CoinEngine e; e.prepare(fs);
        const Run r = run(e, fs, uint32_t(2 * fs), [&](uint32_t i) { return pluck(110.0f, 0.0126f, fs, i); }, defaults());
        CHECK(r.peak == 0.0f);
    }
    {   // first_coin_after_T1
        CoinEngine e; e.prepare(fs);
        const Run r = run(e, fs, uint32_t(fs), [&](uint32_t i) { return pluck(110.0f, 0.1f, fs, i); }, defaults());
        const double t = firstWet(r.wet, fs);
        std::printf("  first coin at %.4f s (T1 %.4f)\n", t, T110);
        CHECK(near(t, T110, 0.04));
    }
    {   // reverse_starts_soon
        CoinEngine e; e.prepare(fs);
        EngineParams p = defaults(); p.reverse = true;
        const Run r = run(e, fs, uint32_t(fs), [&](uint32_t i) { return pluck(110.0f, 0.1f, fs, i); }, p);
        const double t = firstWet(r.wet, fs);
        std::printf("  reverse first coin at %.4f s\n", t);
        CHECK(t >= 0.04 && t <= 0.12);
    }
    {   // fallback_pitch
        Rng nz(9);
        auto noise = [&](uint32_t i) {   // 200 ms burst, 5 ms fade-in
            const double t = i / fs;
            if (t > 0.2) return 0.0f;
            return float((t < 0.005 ? t / 0.005 : 1.0) * nz.uniform(-0.1f, 0.1f));
        };
        CoinEngine e; e.prepare(fs);
        const Run a = run(e, fs, uint32_t(fs), noise, defaults());
        CHECK(near(firstWet(a.wet, fs), T110, 0.04));

        CoinEngine e2; e2.prepare(fs);
        run(e2, fs, uint32_t(fs), [&](uint32_t i) { return pluck(220.0f, 0.1f, fs, i); }, defaults());
        for (int k = 0; k < 20 && (e2.activeDrops() || e2.activeRings()); ++k)   // let the 220 Hz drop finish
            run(e2, fs, uint32_t(fs), [](uint32_t) { return 0.0f; }, defaults());
        CHECK(e2.activeDrops() == 0 && e2.activeRings() == 0);
        const Run b = run(e2, fs, uint32_t(fs), noise, defaults());
        std::printf("  noise after 220 Hz: first coin at %.4f s (T1 %.4f)\n", firstWet(b.wet, fs), T220);
        CHECK(near(firstWet(b.wet, fs), T220, 0.04));
    }

    Rng pr(11);
    std::vector<float> pitches(64);
    for (float& f : pitches) f = 41.0f * std::pow(1500.0f / 41.0f, pr.uniform());
    auto stressIn = [&](uint32_t i) {   // a hard pluck every 250 ms at a random pitch
        const uint32_t per = uint32_t(0.25 * fs), k = i / per;
        return pluck(pitches[k % pitches.size()], 0.1f, fs, i % per);
    };

    for (bool sampled : { false, true })
    {   // stress_synth / stress_sampled
        CoinEngine e; e.prepare(fs);
        EngineParams p = defaults(); p.bounce = 0.97f; p.sampled = sampled;
        const Run r = run(e, fs, uint32_t(10 * fs), stressIn, p);
        std::printf("  stress %s: drops %d rings %d peak %.3f\n", sampled ? "sampled" : "synth", r.maxDrops, r.maxRings, r.peak);
        CHECK(r.maxDrops <= CoinEngine::kMaxDrops);
        CHECK(r.maxRings <= 32);
        CHECK(r.finite);
        CHECK(r.peak < 1.0f);
    }
    {   // knobs_mid_drop
        CoinEngine e; e.prepare(fs);
        Rng kr(13);
        const Run r = run(e, fs, uint32_t(10 * fs), stressIn, defaults(), [&](EngineParams& p, uint32_t) {
            p.thresholdDb = sensitivityToThresholdDb(kr.uniform(0.0f, 100.0f));
            p.bounce = kr.uniform(0.70f, 0.97f); p.height = kr.uniform(0.1f, 1.5f);
            p.pitch = kr.uniform(-24.0f, 24.0f); p.glitch = kr.uniform();
            p.steps = Steps(int(kr.uniform(0.0f, 3.99f)));
            p.reverse = kr.uniform() < 0.5f; p.sampled = kr.uniform() < 0.5f;
        });
        std::printf("  knobs mid-drop: peak %.3f max step %.3f\n", r.peak, r.maxStep);
        CHECK(r.finite);
        CHECK(r.maxStep <= 0.5f);
    }
    {   // silence_frees_everything
        CoinEngine e; e.prepare(fs);
        run(e, fs, uint32_t(2 * fs), stressIn, defaults());
        run(e, fs, uint32_t(15 * fs), [](uint32_t) { return 0.0f; }, defaults());
        CHECK(e.activeDrops() == 0);
        CHECK(e.activeRings() == 0);
    }
    for (double rate : { 44100.0, 96000.0 })
    {   // other_rates
        CoinEngine e; e.prepare(rate);
        const Run r = run(e, rate, uint32_t(rate), [&](uint32_t i) { return pluck(110.0f, 0.1f, rate, i); }, defaults());
        const double t = firstWet(r.wet, rate);
        std::printf("  %.0f Hz: first coin at %.4f s\n", rate, t);
        CHECK(near(t, T110, 0.04));
    }
    REPORT();
}
