// CPU benchmark for the MOD Dwarf (cross-built and run on the device by
// `make dwarf-bench`; also runs on the host). Worst case from the spec: a hard
// pluck every 250 ms at random pitches, Bounce 97 %, Height 1.5 s, Glide (no
// two hits share a pitch), Glitch 50 %. 48 kHz, 128-frame blocks, as on the
// Dwarf. Fails if the average block is above 20 % of the 2.667 ms budget or
// the worst block above 50 % (room left for an amp sim, cab IR and reverb).
#include "src/DistrhoPluginInternal.hpp"
#include "test_util.hpp"
#include "dsp/Rng.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <vector>

USE_NAMESPACE_DISTRHO

static bool requestStub(void*, uint32_t, float) { return true; }

enum { kSens = 0, kBounce, kHeight, kPitch, kSteps, kGlitch, kReverse, kSound, kLevel, kBypass };

static double now()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main()
{
    const double fs = 48000.0, budget = 128.0 / fs;
    const uint32_t n = uint32_t(20 * fs), per = uint32_t(0.25 * fs);

    toss::Rng rng(21);
    std::vector<float> in(n);
    float hz = 110.0f;
    for (uint32_t i = 0; i < n; ++i)
    {
        if (i % per == 0) hz = 41.0f * std::pow(1500.0f / 41.0f, rng.uniform());
        in[i] = pluck(hz, 0.1f, fs, i % per, 0.1);
    }

    int fails = 0;
    for (int sampled = 0; sampled < 2; ++sampled)
    {
        d_nextBufferSize = 128; d_nextSampleRate = fs; d_nextCanRequestParameterValueChanges = true;
        PluginExporter plugin(nullptr, nullptr, requestStub, nullptr);
        plugin.setParameterValue(kBounce, 97.0f);
        plugin.setParameterValue(kHeight, 1.5f);
        plugin.setParameterValue(kSteps, 0.0f);
        plugin.setParameterValue(kGlitch, 50.0f);
        plugin.setParameterValue(kSound, float(sampled));
        plugin.activate();

        std::vector<float> outL(128), outR(128);
        std::vector<double> t;
        t.reserve(n / 128);
        for (uint32_t i = 0; i + 128 <= n; i += 128)
        {
            const float* inputs[1] = { &in[i] };
            float* outputs[2] = { outL.data(), outR.data() };
            const double t0 = now();
            plugin.run(inputs, outputs, 128);
            t.push_back(now() - t0);
        }
        double sum = 0.0;
        for (double x : t) sum += x;
        if (std::getenv("BENCH_VERBOSE"))    // where in the 250 ms pluck cycle do the worst blocks fall?
        {
            std::vector<size_t> idx(t.size());
            for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
            std::partial_sort(idx.begin(), idx.begin() + 12, idx.end(), [&](size_t a, size_t b) { return t[a] > t[b]; });
            for (int k = 0; k < 12; ++k)
                std::printf("  block %5zu  phase %5.1f ms  %5.1f %%\n", idx[k], std::fmod(idx[k] * 128.0, per) / fs * 1000.0, 100.0 * t[idx[k]] / budget);
        }
        std::sort(t.begin(), t.end());
        const double avg = 100.0 * sum / t.size() / budget;
        const double p999 = 100.0 * t[size_t(t.size() * 0.999)] / budget;
        const double worst = 100.0 * t.back() / budget;
        std::printf("%-8s avg %5.1f %%  p99.9 %5.1f %%  worst %5.1f %%  of the 2.667 ms block\n",
                    sampled ? "Sampled" : "Synth", avg, p999, worst);
        if (avg > 20.0 || worst > 50.0) ++fails;
    }
    std::printf(fails ? "FAIL: over the CPU budget\n" : "OK: within the CPU budget\n");
    return fails ? 1 : 0;
}
