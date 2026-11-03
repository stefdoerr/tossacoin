/*
 * Toss a Coin — every hard attack drops a coin on the floor.
 *
 * The note you play sets the size of the coin: low notes are fat coins whose
 * bounces start slow, high notes small coins that clatter quickly. The
 * bounces speed up like a real dropped coin, ring like a small bell (or
 * replay your own attack, in Sampled mode) and step in pitch as they go.
 *
 * Mono in -> stereo out (same signal both sides). The dry signal always
 * passes at unity; coins are added on top: out = dry + Level * softLimit(wet).
 * All DSP lives in dsp/ (see dsp/CoinEngine.hpp); this file is the DPF glue.
 */

#include "DistrhoPlugin.hpp"
#include "dsp/CoinEngine.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

class TossACoinPlugin : public Plugin
{
public:
    enum ParamIndex {
        kParamSensitivity = 0,
        kParamBounce,
        kParamHeight,
        kParamPitch,
        kParamSteps,
        kParamGlitch,
        kParamReverse,
        kParamSound,
        kParamLevel,
        kParamBypass,   // after the first release: append new ports only, never reorder
        kNumParams
    };

    enum GroupId { kGroupTrigger = 0, kGroupDrop, kGroupPitch, kGroupOutput };

    TossACoinPlugin()
        : Plugin(kNumParams, 0, 0)
    {
        for (uint32_t i = 0; i < kParamBypass; ++i) fValues[i] = kSpecs[i].def;
        sampleRateChanged(getSampleRate());
    }

protected:
    // ---------------- Information ----------------

    const char* getLabel()       const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getMaker()       const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getHomePage()    const override { return PLUGIN_HOMEPAGE; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override
    {
#ifdef PLUGIN_VERSION_MAJOR
        return d_version(PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_MICRO);
#else
        return d_version(0, 0, 0);
#endif
    }
    const char* getDescription() const override
    {
        return "Every hard attack drops a coin: it bounces faster and faster, rings like a "
               "small bell and steps in pitch. The note you play sets the size of the coin.";
    }

    int64_t getUniqueId() const override
    {
#ifdef TOSSACOIN_BETA
        return d_cconst('d', 'T', 'o', 'B');
#else
        return d_cconst('d', 'T', 'o', 'C');
#endif
    }

    // ---------------- Init ----------------

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = input ? kPortGroupMono : kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initPortGroup(uint32_t groupId, PortGroup& portGroup) override
    {
        // gN_ prefixes pin the band order (MOD sorts groups by symbol).
        switch (groupId)
        {
        case kGroupTrigger: portGroup.name = "Trigger"; portGroup.symbol = "g1_trigger"; break;
        case kGroupDrop:    portGroup.name = "Drop";    portGroup.symbol = "g2_drop";    break;
        case kGroupPitch:   portGroup.name = "Pitch";   portGroup.symbol = "g3_pitch";   break;
        case kGroupOutput:  portGroup.name = "Output";  portGroup.symbol = "g4_output";  break;
        default:            Plugin::initPortGroup(groupId, portGroup);                   break;
        }
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index == kParamBypass)
        {
            parameter.initDesignation(kParameterDesignationBypass);
            return;
        }
        const Spec& s = kSpecs[index];
        parameter.hints      = kParameterIsAutomatable;
        parameter.name       = s.name;
        parameter.symbol     = s.symbol;
        parameter.unit       = s.unit;
        parameter.ranges.min = s.min;
        parameter.ranges.max = s.max;
        parameter.ranges.def = s.def;
        parameter.groupId    = s.group;

        switch (index)
        {
        case kParamReverse:
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;   // latch footswitch
            break;
        case kParamSteps:
            setEnum(parameter, kStepLabels, 4);
            break;
        case kParamSound:
            setEnum(parameter, kSoundLabels, 2);
            break;
        }
    }

    // ---------------- State access ----------------

    float getParameterValue(uint32_t index) const override
    {
        if (index == kParamBypass) return fBypass ? 1.0f : 0.0f;
        return index < kParamBypass ? fValues[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index == kParamBypass) { fBypass = value > 0.5f; return; }
        if (index >= kParamBypass) return;
        fValues[index] = std::min(std::max(value, kSpecs[index].min), kSpecs[index].max);
    }

    // ---------------- Lifecycle ----------------

    void activate() override
    {
        fEngine.reset();
        fSnap = true;   // LV2 delivers saved values with the first run(): snap, don't ramp
    }

    void sampleRateChanged(double newSampleRate) override
    {
        fEngine.prepare(newSampleRate);                                       // sizes all buffers
        fWetStep = 1.0f / (0.010f * static_cast<float>(newSampleRate));      // 10 ms bypass fade
    }

    // ---------------- DSP ----------------

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* const in   = inputs[0];
        /* */ float* const outL = outputs[0];
        /* */ float* const outR = outputs[1];

        toss::EngineParams p;
        p.thresholdDb = toss::sensitivityToThresholdDb(fValues[kParamSensitivity]);
        p.bounce  = fValues[kParamBounce] * 0.01f;
        p.height  = fValues[kParamHeight];
        p.pitch   = fValues[kParamPitch];
        p.glitch  = fValues[kParamGlitch] * 0.01f;
        p.steps   = toss::Steps(int(fValues[kParamSteps] + 0.5f));
        p.reverse = fValues[kParamReverse] > 0.5f;
        p.sampled = fValues[kParamSound] > 0.5f;
        fEngine.setParams(p);
        fEngine.work(frames);

        // ponytail: Level applied per block (coins only, small steps); ramp it if zipper is ever audible
        const float level = std::pow(10.0f, fValues[kParamLevel] * (1.0f / 20.0f));

        const float wetTarget = fBypass ? 0.0f : 1.0f;
        if (fSnap)
        {
            fWet  = wetTarget;
            fSnap = false;
        }

        for (uint32_t f = 0; f < frames; ++f)
        {
            if (fWet < wetTarget)      fWet = std::min(fWet + fWetStep, wetTarget);
            else if (fWet > wetTarget) fWet = std::max(fWet - fWetStep, wetTarget);

            const float dry = in[f];
            const float wet = dry + level * softLimit(fEngine.process(dry));   // engine runs even when bypassed
            const float y = dry + (wet - dry) * fWet;
            outL[f] = y;
            outR[f] = y;
        }
    }

private:
    struct Spec { const char* name; const char* symbol; const char* unit; float min, max, def; uint32_t group; };
    static const Spec kSpecs[kParamBypass];
    static const char* const kStepLabels[4];
    static const char* const kSoundLabels[2];

    static void setEnum(Parameter& parameter, const char* const* labels, uint8_t n)
    {
        parameter.hints |= kParameterIsInteger;
        parameter.enumValues.count = n;
        parameter.enumValues.restrictedMode = true;
        ParameterEnumerationValue* const ev = new ParameterEnumerationValue[n];
        for (uint8_t i = 0; i < n; ++i) { ev[i].value = float(i); ev[i].label = labels[i]; }
        parameter.enumValues.values = ev;
    }

    // Untouched below -6 dBFS, smooth above, never reaches +-1 (INSTRUCTIONS.md).
    static inline float softLimit(float x)
    {
        constexpr float kT = 0.5f, kK = 1.0f - kT;
        const float a = std::fabs(x) - kT;
        return a <= 0.0f ? x : std::copysign(kT + a * kK / (kK + a), x);
    }

    toss::CoinEngine fEngine;
    float fValues[kParamBypass];
    bool  fBypass  = false;
    bool  fSnap    = true;
    float fWet     = 1.0f;   // 1 = processed, 0 = bypassed; ramps between
    float fWetStep = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TossACoinPlugin)
};

const TossACoinPlugin::Spec TossACoinPlugin::kSpecs[TossACoinPlugin::kParamBypass] = {
    { "Sensitivity", "sensitivity", "%",    0.0f, 100.0f,  50.0f, kGroupTrigger },
    { "Bounce",      "bounce",      "%",   70.0f,  97.0f,  92.0f, kGroupDrop    },
    { "Height",      "height",      "s",    0.1f,   1.5f,   0.5f, kGroupDrop    },
    { "Pitch",       "pitch",       "st", -24.0f,  24.0f,  12.0f, kGroupPitch   },
    { "Steps",       "steps",       "",     0.0f,   3.0f,   1.0f, kGroupPitch   },
    { "Glitch",      "glitch",      "%",    0.0f, 100.0f,   0.0f, kGroupPitch   },
    { "Reverse",     "reverse",     "",     0.0f,   1.0f,   0.0f, kGroupDrop    },
    { "Sound",       "sound",       "",     0.0f,   1.0f,   0.0f, kGroupOutput  },
    { "Level",       "level",       "dB", -24.0f,   6.0f,   0.0f, kGroupOutput  },
};
const char* const TossACoinPlugin::kStepLabels[4]  = { "Glide", "Semitones", "Fifths & octaves", "Octaves" };
const char* const TossACoinPlugin::kSoundLabels[2] = { "Synth", "Sampled" };

// ---------------------------------------------------------------------------

Plugin* createPlugin()
{
    return new TossACoinPlugin();
}

END_NAMESPACE_DISTRHO
