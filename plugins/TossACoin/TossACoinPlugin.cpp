/*
 * TossACoin — minimal DPF plugin example for MOD Desktop / MOD Dwarf.
 *
 * Mono input → stereo output, with a single GAIN knob (±24 dB) and an
 * in-plugin bypass that crossfades instead of clicking. Replace the wet
 * path in run() with your own DSP, add parameters as needed.
 *
 * Useful patterns to crib (see INSTRUCTIONS.md):
 *   - Equal-power dry/wet crossfade for a MIX knob
 *   - Per-resonator tanh saturator inside feedback loops
 *   - Soft limit on the wet path of feedback effects
 *   - One-pole envelope follower for noise gates
 *   - Parameter smoothing to avoid zipper noise (only the bypass
 *     crossfade here; anything you build on top should smooth its knobs
 *     and snap them on the first run() the same way)
 */

#include "DistrhoPlugin.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

class TossACoinPlugin : public Plugin
{
public:
    enum ParamIndex {
        kParamGain = 0,
        kParamBypass,   // new controls go above this until the first release;
                        // after that, append at the end and never reorder
        kNumParams
    };

    TossACoinPlugin()
        : Plugin(kNumParams, 0, 0)
    {
        sampleRateChanged(getSampleRate());
    }

protected:
    // ---------------- Information ----------------
    // Derive label / maker / homepage from DISTRHO_PLUGIN_INFO macros so
    // the stable and beta builds report distinct identity to the host.

    const char* getLabel()       const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getMaker()       const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getHomePage()    const override { return PLUGIN_HOMEPAGE; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override
    {
        // Injected by the Makefile from the top-level VERSION file, which
        // `make release` keeps in sync with the release tag. The fallback
        // marks builds outside the Makefile as 0.0.0.
#ifdef PLUGIN_VERSION_MAJOR
        return d_version(PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_MICRO);
#else
        return d_version(0, 0, 0);
#endif
    }
    const char* getDescription() const override
    {
        return "A minimal mono-to-stereo plugin template.";
    }

    int64_t getUniqueId() const override
    {
        // Must match the 4-char DISTRHO_PLUGIN_UNIQUE_ID from
        // DistrhoPluginInfo.h. d_cconst packs four chars into an int32_t.
#ifdef TOSSACOIN_BETA
        return d_cconst('d', 'T', 'o', 'B');
#else
        return d_cconst('d', 'T', 'o', 'C');
#endif
    }

    // ---------------- Init ----------------

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        if (input)
            port.groupId = kPortGroupMono;
        else
            port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        switch (index)
        {
        case kParamGain:
            // dB-scale gain trim. Range ±24 covers typical input/output
            // matching needs. Pick a unit string so the host displays
            // values readably (MOD-UI uses this in tooltips).
            parameter.hints      = kParameterIsAutomatable;
            parameter.name       = "Gain";
            parameter.symbol     = "gain";
            parameter.unit       = "dB";
            parameter.ranges.min = -24.0f;
            parameter.ranges.max =  24.0f;
            parameter.ranges.def =   0.0f;
            break;
        case kParamBypass:
            // DPF exports this as LV2 `lv2:enabled` (value inverted), VST3
            // bypass and CLAP is-bypass. MOD then routes the pedal
            // footswitch here instead of switching the plugin out in one
            // sample, so run() can crossfade. Plugin-side 1 = bypassed.
            parameter.initDesignation(kParameterDesignationBypass);
            break;
        }
    }

    // ---------------- State access ----------------

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamGain:   return fGainDb;
        case kParamBypass: return fBypass ? 1.0f : 0.0f;
        }
        return 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamGain:
            if (value < -24.0f) value = -24.0f;
            if (value >  24.0f) value =  24.0f;
            fGainDb = value;
            break;
        case kParamBypass:
            fBypass = value > 0.5f;
            break;
        }
    }

    // ---------------- Lifecycle ----------------

    void activate() override
    {
        // Initialise any DSP state here (clear buffers, reset filters).
        // Don't initialise smoothers from parameters here: on LV2 the saved
        // control values only arrive with the first run(), so they would
        // ramp audibly from the defaults. Flag a snap instead.
        fSnap = true;
    }

    void sampleRateChanged(double newSampleRate) override
    {
        // Recompute sample-rate-dependent coefficients here when fs
        // changes (e.g. filter cutoffs, envelope time constants).
        fWetStep = 1.0f / (0.010f * static_cast<float>(newSampleRate));  // 10 ms
    }

    // ---------------- DSP ----------------

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* const in   = inputs[0];
        /* */ float* const outL = outputs[0];
        /* */ float* const outR = outputs[1];

        // dB → linear amplitude, once per block. For zipper-free knob
        // turns on more complex plugins, smooth the gain across the
        // block (linear ramp from old to new) — overkill for this trim.
        const float gainLin = std::pow(10.0f, fGainDb * (1.0f / 20.0f));

        const float wetTarget = fBypass ? 0.0f : 1.0f;
        if (fSnap)
        {
            fWet  = wetTarget;
            fSnap = false;
        }

        for (uint32_t f = 0; f < frames; ++f)
        {
            // Linear ramp, not a one-pole: it lands exactly on 0 or 1
            // (a one-pole creeps toward 0 through denormals).
            if (fWet < wetTarget)      fWet = std::min(fWet + fWetStep, wetTarget);
            else if (fWet > wetTarget) fWet = std::max(fWet - fWetStep, wetTarget);

            const float dry = in[f];
            const float wet = dry * gainLin;   // your DSP, Mix and Level go here

            // Crossfade the final output against the untouched input, so
            // bypass leaves the dry at unity whatever Mix/Level say.
            const float y = dry + (wet - dry) * fWet;
            outL[f] = y;
            outR[f] = y;
        }
    }

private:
    float fGainDb  = 0.0f;
    bool  fBypass  = false;
    bool  fSnap    = true;
    float fWet     = 1.0f;   // 1 = processed, 0 = bypassed; ramps between
    float fWetStep = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TossACoinPlugin)
};

// ---------------------------------------------------------------------------

Plugin* createPlugin()
{
    return new TossACoinPlugin();
}

END_NAMESPACE_DISTRHO
