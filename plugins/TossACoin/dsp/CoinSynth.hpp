#pragma once
#include <cmath>
#include <vector>
#include "Rng.hpp"

namespace toss {

// The coin rings two octaves above the note; anything above 8 kHz is folded
// down by octaves so high notes that climb stay audible.
inline float coinFundamental(float noteHz, float semis)
{
    float f = noteHz * std::pow(2.0f, semis / 12.0f) * 4.0f;
    while (f > 8000.0f) f *= 0.5f;
    return f;
}

// Free circular plate mode ratios (constexpr at namespace scope: internal
// linkage, so the header is safe to include from several files in C++14).
constexpr float kPlateRatios[6] = { 1.0f, 1.73f, 2.33f, 3.91f, 4.11f, 6.30f };

// Pool of ringing coins. Each ring is a free circular plate: 6 modes, each a
// two-pole resonator (2 multiplies per sample), plus a 2 ms noise click.
// Like the approved prototype, a ring lasts min(1 s, 5 tau0); it fades out
// over its last 30 ms instead of the prototype's hard cut.
// Coefficients are computed once per hit. Rings overlap and are never choked
// (that overlap is the bell character); when the pool is full a new hit
// replaces the quietest ring.
class CoinSynth {
public:
    static constexpr int kModes = 6;

    void prepare(double fs, int maxRings)
    {
        fs_ = fs;
        rings_.assign(size_t(maxRings), Ring());
        clickLen_ = int(0.002 * fs);
        clickDecay_ = float(std::exp(-4.0 / clickLen_));   // tau = click length / 4
        fadeLen_ = int(0.030 * fs);
        for (int i = 0; i < kModes; ++i) tauDiv_[i] = std::pow(kPlateRatios[i], 0.8f);
        reset();
    }

    void reset()
    {
        for (Ring& r : rings_) r.active = false;
        active_ = 0;
    }

    void hit(float noteHz, float semis, float amp, Rng& rng)
    {
        Ring& r = freeOrQuietest();
        const float fc   = coinFundamental(noteHz, semis);
        const float tau0 = 0.35f * std::pow(440.0f / fc, 0.3f);
        r.nModes = 0;
        for (int i = 0; i < kModes; ++i)
        {
            const double fr = double(fc) * kPlateRatios[i];
            if (fr > 0.45 * fs_) continue;
            const double w = 2.0 * M_PI * fr / fs_;
            const double d = std::exp(-1.0 / (double(tau0 / tauDiv_[i]) * fs_));
            const double A = rng.uniform(0.3f, 1.0f) / (1.0 + 0.5 * i);
            const double ph = rng.uniform(0.0f, 2.0f * float(M_PI));
            Mode& m = r.modes[r.nModes++];
            m.a1 = float(2.0 * d * std::cos(w));
            m.a2 = float(d * d);
            m.y1 = float(A / d * std::sin(ph - w));            // so that y[0] = A sin(ph)
            m.y2 = float(A / (d * d) * std::sin(ph - 2.0 * w));
        }
        r.gain  = 0.35f * amp;
        r.age   = 0;
        r.fadeStart = int(std::fmin(1.0f, 5.0f * tau0) * float(fs_)) - fadeLen_;
        r.fade  = 1.0f;
        r.env   = 1.0f;
        r.envMul = float(std::exp(-1.0 / (double(tau0) * fs_)));   // slowest mode
        r.click = 1.0f;
        r.clickLeft = clickLen_;
        r.prevNoise = 0.0f;
        r.rng = Rng(uint32_t(rng.uniform() * 4294967040.0f) + 1u);
        if (!r.active) ++active_;
        r.active = true;
    }

    float process()
    {
        float out = 0.0f;
        for (Ring& r : rings_)
        {
            if (!r.active) continue;
            float s = 0.0f;
            for (int i = 0; i < r.nModes; ++i)
            {
                Mode& m = r.modes[i];
                const float y = m.a1 * m.y1 - m.a2 * m.y2;
                m.y2 = m.y1;
                m.y1 = y;
                s += y;
            }
            if (r.clickLeft > 0)
            {
                const float nz = r.rng.uniform(-1.0f, 1.0f);
                s += (nz - r.prevNoise) * r.click;
                r.prevNoise = nz;
                r.click *= clickDecay_;
                --r.clickLeft;
            }
            out += r.gain * r.fade * s;
            r.env *= r.envMul;
            if (++r.age > r.fadeStart) r.fade -= 1.0f / float(fadeLen_);
            if (r.fade <= 0.0f || r.env < 1e-4f) { r.active = false; --active_; }
        }
        return out;
    }

    int activeRings() const { return active_; }

private:
    struct Mode { float a1 = 0, a2 = 0, y1 = 0, y2 = 0; };
    struct Ring {
        Mode modes[kModes];
        int nModes = 0;
        float gain = 0, env = 0, envMul = 0, click = 0, prevNoise = 0;
        int clickLeft = 0, age = 0, fadeStart = 0;
        float fade = 1.0f;
        bool active = false;
        Rng rng;
    };

    Ring& freeOrQuietest()
    {
        Ring* q = &rings_[0];
        for (Ring& r : rings_)
        {
            if (!r.active) return r;
            if (r.gain * r.env * r.fade < q->gain * q->env * q->fade) q = &r;
        }
        return *q;
    }

    double fs_ = 48000.0;
    std::vector<Ring> rings_;
    int active_ = 0, clickLen_ = 96, fadeLen_ = 1440;
    float clickDecay_ = 0.96f;
    float tauDiv_[kModes] = {};
};

} // namespace toss
