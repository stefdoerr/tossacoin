// Whole plugin through DPF's PluginExporter: soft notes untouched, hard notes
// add coins, host block size doesn't matter, hot input stays bounded.
#include "src/DistrhoPluginInternal.hpp"
#include "test_util.hpp"

#include <functional>
#include <vector>

USE_NAMESPACE_DISTRHO

static bool requestStub(void*, uint32_t, float) { return true; }

enum { kSens = 0, kBounce, kHeight, kPitch, kSteps, kGlitch, kReverse, kSound, kLevel, kBypass };

static const double kFs = 48000.0;

struct Out { std::vector<float> in, l, r; };

static Out run(uint32_t block, uint32_t n, std::function<float(uint32_t)> sig,
               std::function<void(PluginExporter&)> setup = nullptr)
{
    d_nextBufferSize = 1024;
    d_nextSampleRate = kFs;
    d_nextCanRequestParameterValueChanges = true;
    PluginExporter plugin(nullptr, nullptr, requestStub, nullptr);
    if (setup) setup(plugin);
    plugin.activate();

    Out o;
    o.in.resize(n); o.l.resize(n); o.r.resize(n);
    for (uint32_t i = 0; i < n; ++i) o.in[i] = sig(i);
    for (uint32_t i = 0; i < n; i += block)
    {
        const uint32_t len = n - i < block ? n - i : block;
        const float* inputs[1]  = { o.in.data() + i };
        float*       outputs[2] = { o.l.data() + i, o.r.data() + i };
        plugin.run(inputs, outputs, len);
    }
    return o;
}

static long firstCoin(const Out& o)
{
    for (size_t i = 0; i < o.in.size(); ++i)
        if (o.l[i] != o.in[i]) return long(i);
    return -1;
}

int main()
{
    const uint32_t n1 = uint32_t(kFs);
    auto hard = [](uint32_t i) { return pluck(110.0f, 0.1f, kFs, i); };

    {   // soft_notes_are_untouched
        const Out o = run(128, n1, [](uint32_t i) { return pluck(110.0f, 0.0126f, kFs, i); });
        bool same = true;
        for (uint32_t i = 0; i < n1; ++i) same = same && o.l[i] == o.in[i] && o.r[i] == o.in[i];
        CHECK(same);
    }
    {   // hard_note_adds_coins
        const Out o = run(128, n1, hard);
        const long first = firstCoin(o);
        std::printf("  first coin at %.4f s\n", first / kFs);
        CHECK(first >= long(0.40 * kFs));
        std::vector<float> diff(uint32_t(0.2 * kFs));
        for (size_t i = 0; i < diff.size(); ++i) diff[i] = o.l[uint32_t(0.4 * kFs) + i] - o.in[uint32_t(0.4 * kFs) + i];
        CHECK(rms(diff.data(), uint32_t(diff.size())) > 0.0);
        bool lr = true;
        for (uint32_t i = 0; i < n1; ++i) lr = lr && o.l[i] == o.r[i];
        CHECK(lr);
    }
    {   // block_sizes: the first coin doesn't depend on the host block size
        const long ref = firstCoin(run(128, n1, hard));
        for (uint32_t b : { 1u, 37u, 1024u })
        {
            const long t = firstCoin(run(b, n1, hard));
            std::printf("  block %4u: first coin at sample %ld (128: %ld)\n", b, t, ref);
            CHECK(t >= 0 && std::labs(t - ref) <= long(b + 128));
        }
    }
    {   // hot_input_bounded: 0 dBFS plucks every 250 ms, Bounce 97, Level +6
        const uint32_t per = uint32_t(0.25 * kFs);
        const Out o = run(128, uint32_t(8 * kFs), [&](uint32_t i) { return pluck(110.0f * (1 + (i / per) % 5), 1.0f, kFs, i % per); },
                          [](PluginExporter& p) { p.setParameterValue(kBounce, 97.0f); p.setParameterValue(kLevel, 6.0f); });
        bool ok = true;
        float peak = 0.0f;
        for (size_t i = 0; i < o.l.size(); ++i)
        {
            ok = ok && std::isfinite(o.l[i]) && std::fabs(o.l[i]) <= 3.0f;
            peak = std::fmax(peak, std::fabs(o.l[i]));
        }
        std::printf("  hot input: output peak %.3f\n", peak);
        CHECK(ok);
    }
    {   // enum_labels
        d_nextBufferSize = 1024; d_nextSampleRate = kFs; d_nextCanRequestParameterValueChanges = true;
        PluginExporter p(nullptr, nullptr, requestStub, nullptr);
        const ParameterEnumerationValues& steps = p.getParameterEnumValues(kSteps);
        const ParameterEnumerationValues& sound = p.getParameterEnumValues(kSound);
        CHECK(steps.count == 4 && sound.count == 2);
        if (steps.count == 4)
            CHECK(steps.values[0].label == "Glide" && steps.values[1].label == "Semitones"
                  && steps.values[2].label == "Fifths & octaves" && steps.values[3].label == "Octaves");
        if (sound.count == 2)
            CHECK(sound.values[0].label == "Synth" && sound.values[1].label == "Sampled");
        CHECK(p.getParameterCount() == 10);
        CHECK(p.getParameterSymbol(kSens) == "sensitivity" && p.getParameterSymbol(kLevel) == "level");
    }
    REPORT();
}
