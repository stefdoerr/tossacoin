/*
 * Bypass and loudness checks that every plugin built from this template
 * should keep passing. Same harness as test_gain.cpp (read that first).
 *
 *   - Bypassed output is the dry input at unity, whatever the knobs say.
 *   - Toggling bypass crossfades instead of jumping (no click).
 *   - Saved values arriving with the first run() snap, no ramp on load.
 *   - At default settings the output is as loud as bypass (RMS, ±3 dB).
 *     If your effect is meant to be louder or quieter by default, change
 *     the window here and say why in a comment.
 *
 * The probe is played at a guitar DI's level (−20 dBFS peak) with a smooth
 * attack, not at full scale: level-sensitive DSP (drive, compression,
 * gates) behaves very differently there, and a hard onset makes clicks the
 * plugin isn't responsible for.
 */

#include "src/DistrhoPluginInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

USE_NAMESPACE_DISTRHO

static bool requestStub(void*, uint32_t, float)
{
    return true;
}

static int gFailures = 0;

static void expect(const char* what, bool ok)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++gFailures;
    }
}

static const uint32_t kBlock = 512;
static const double   kRate  = 48000.0;
static const uint32_t kLen   = 24000;      // 0.5 s
static const uint32_t kParamGain   = 0;
static const uint32_t kParamBypass = 1;

static float gIn[kLen], gOutL[kLen], gOutR[kLen];

// Run n samples starting at offset in kBlock-sized chunks, like a host.
static void process(PluginExporter& plugin, uint32_t offset, uint32_t n)
{
    for (uint32_t i = offset; i < offset + n; i += kBlock)
    {
        const uint32_t len = std::min(kBlock, offset + n - i);
        const float* inputs[1]  = { gIn + i };
        float*       outputs[2] = { gOutL + i, gOutR + i };
        plugin.run(inputs, outputs, len);
    }
}

static double rms(const float* x, uint32_t n)
{
    double sum = 0.0;
    for (uint32_t i = 0; i < n; ++i)
        sum += double(x[i]) * x[i];
    return std::sqrt(sum / n);
}

int main()
{
    d_nextBufferSize = kBlock;
    d_nextSampleRate = kRate;
    d_nextCanRequestParameterValueChanges = true;

    // Plucked low A (110 Hz): 5 ms raised-cosine attack, 0.3 s decay,
    // 0.1 peak (−20 dBFS).
    const uint32_t attack = uint32_t(0.005 * kRate);
    for (uint32_t i = 0; i < kLen; ++i)
    {
        const double env = i < attack ? 0.5 * (1.0 - std::cos(M_PI * i / attack))
                                      : std::exp(-double(i - attack) / (0.3 * kRate));
        gIn[i] = float(0.1 * env * std::sin(2.0 * M_PI * 110.0 * i / kRate));
    }

    // Loaded already bypassed (saved pedalboard): the values arrive with
    // the first run(), after activate(). Output must be dry from sample 0.
    {
        PluginExporter plugin(nullptr, nullptr, requestStub, nullptr);
        plugin.activate();
        plugin.setParameterValue(kParamGain, 6.0f);
        plugin.setParameterValue(kParamBypass, 1.0f);
        process(plugin, 0, kBlock);

        bool dry = true;
        for (uint32_t i = 0; i < kBlock; ++i)
            dry = dry && gOutL[i] == gIn[i] && gOutR[i] == gIn[i];
        expect("loaded bypassed: dry from the first sample, no ramp", dry);

        // Un-bypass: the first samples are still close to dry (no jump to
        // +6 dB), and the fade is finished within 10 ms (480 samples).
        plugin.setParameterValue(kParamBypass, 0.0f);
        process(plugin, kBlock, kBlock);
        const uint32_t early = kBlock + 20, late = 2 * kBlock - 1;
        expect("un-bypass fades in instead of jumping",
               std::fabs(gOutL[early]) < 1.1f * std::fabs(gIn[early]));
        expect("un-bypass reaches +6 dB within 10 ms",
               std::fabs(gOutL[late] - 1.99526f * gIn[late]) < 1e-6f);

        // Bypass again: after the fade, exact dry at unity despite +6 dB.
        plugin.setParameterValue(kParamBypass, 1.0f);
        process(plugin, 2 * kBlock, 2 * kBlock);
        bool unity = true;
        for (uint32_t i = 3 * kBlock; i < 4 * kBlock; ++i)
            unity = unity && gOutL[i] == gIn[i] && gOutR[i] == gIn[i];
        expect("bypassed output is the dry at unity, whatever the knobs say", unity);
    }

    // Loudness at default settings vs bypass, average level not peaks.
    {
        PluginExporter plugin(nullptr, nullptr, requestStub, nullptr);
        plugin.activate();
        process(plugin, 0, kLen);
        const double processed = rms(gOutL, kLen);

        plugin.setParameterValue(kParamBypass, 1.0f);
        process(plugin, 0, kBlock);   // let the crossfade finish
        process(plugin, 0, kLen);
        const double bypassed = rms(gOutL, kLen);

        const double diffDb = 20.0 * std::log10(processed / bypassed);
        std::printf("default vs bypass: %+.2f dB\n", diffDb);
        expect("default settings within ±3 dB of bypass", std::fabs(diffDb) <= 3.0);
    }

    if (gFailures != 0)
    {
        std::printf("%d failure(s)\n", gFailures);
        return 1;
    }

    std::printf("ok: bypass is click-free, dry at unity, and level-matched\n");
    return 0;
}
