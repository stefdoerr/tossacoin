#pragma once
#include <cmath>
#include <cstdint>

namespace toss {

// Sensitivity knob (0..100 %) -> attack threshold on the fast envelope, dBFS.
// 50 % = -40 dBFS: measured on real guitar at DI level (raw peaks ~-22 dBFS),
// hard picks reach -31..-35 dBFS on this envelope and soft notes -49..-55.
inline float sensitivityToThresholdDb(float percent) { return -20.0f - 0.4f * percent; }

// Hard-attack detector. Two one-pole envelopes of |x| (fast 2 ms, slow 100 ms);
// a drop starts on the rising edge of "fast is above the threshold AND well
// above the recent level (2.5x slow)", then a 250 ms hold-off. The ratio test
// is what keeps a note's own sustain from re-triggering.
class Onset {
public:
    void prepare(double fs)
    {
        aFast_   = float(std::exp(-1.0 / (0.002 * fs)));
        aSlow_   = float(std::exp(-1.0 / (0.100 * fs)));
        holdoff_ = uint32_t(0.250 * fs);
        reset();
    }
    void setThresholdDb(float db) { thr_ = std::pow(10.0f, db / 20.0f); }
    void reset() { fast_ = slow_ = 0.0f; above_ = false; sinceTrigger_ = holdoff_; }

    bool process(float x)
    {
        const float ax = std::fabs(x);
        fast_ += (1.0f - aFast_) * (ax - fast_);
        slow_ += (1.0f - aSlow_) * (ax - slow_);
        const bool above = fast_ > thr_ && fast_ > 2.5f * slow_;
        const bool rising = above && !above_;
        above_ = above;
        if (sinceTrigger_ < holdoff_) ++sinceTrigger_;
        if (rising && sinceTrigger_ >= holdoff_)
        {
            sinceTrigger_ = 0;
            return true;
        }
        return false;
    }
    float fastEnv() const { return fast_; }

private:
    float aFast_ = 0.0f, aSlow_ = 0.0f, thr_ = 0.025f;
    float fast_ = 0.0f, slow_ = 0.0f;
    bool above_ = false;
    uint32_t holdoff_ = 12000, sinceTrigger_ = 12000;
};

} // namespace toss
