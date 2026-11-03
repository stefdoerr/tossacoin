#pragma once
#include <cmath>
#include <cstdint>
#include "CoinSynth.hpp"
#include "Grains.hpp"
#include "Onset.hpp"
#include "PitchDetector.hpp"
#include "Rng.hpp"
#include "Schedule.hpp"

namespace toss {

struct EngineParams {
    float thresholdDb, bounce, height, pitch, glitch;   // bounce/glitch as fractions
    Steps steps;
    bool reverse, sampled;
};

// Drops lifecycle. Each hard attack takes a drop slot (free, or the oldest is
// stolen): it measures the attack peak, records the grain and detects the
// pitch; once the pitch is in, its schedule is built and its bounces fire
// into the shared synth ring pool or the grain players. A drop keeps the
// parameters it started with.
class CoinEngine {
public:
    static constexpr int kMaxDrops = 6;
    static constexpr int kMaxRings = 32;
    static constexpr int kMaxPlayers = 16;

    void prepare(double fs)
    {
        fs_ = fs;
        onset_.prepare(fs);
        pd_.prepare(fs);
        synth_.prepare(fs, kMaxRings);
        grains_.prepare(fs, kMaxDrops, kMaxPlayers);
        peakLen_ = uint32_t(0.020 * fs);
        reset();
    }

    void reset()
    {
        onset_.reset();
        synth_.reset();
        grains_.reset();
        for (Drop& d : drops_) d.state = Drop::Free;
        detecting_ = -1;
        lastHz_ = 0.0f;
        stamp_ = 0;
    }

    void setParams(const EngineParams& p)
    {
        p_ = p;
        onset_.setThresholdDb(p.thresholdDb);
    }

    // Once per block: bounded pitch work, then start the drop that just got its pitch.
    void work(uint32_t frames)
    {
        pd_.work(int(frames * 3 / 4));
        if (detecting_ < 0 || !pd_.done()) return;

        Drop& d = drops_[detecting_];
        float f0 = pd_.hz();
        if (f0 > 0.0f) lastHz_ = f0;
        else           f0 = lastHz_ > 0.0f ? lastHz_ : 110.0f;
        d.params.f0 = f0;

        Bounce b[kMaxBounces];
        d.n = makeSchedule(d.params, float(d.age / fs_), rng_, b);
        float longest = 0.0f;
        for (int k = 0; k < d.n; ++k)
        {
            d.at[k]    = uint32_t(b[k].time * fs_);
            d.amp[k]   = b[k].amp;
            d.semis[k] = b[k].semis;
            const float gap = k ? b[k].time - b[k - 1].time : 0.0f;
            if (gap > longest) longest = gap;
        }
        if (d.sampled)
        {
            const float L = std::fmin(0.12f, 0.5f * (longest > 0.0f ? longest : b[0].time));
            grains_.lockLength(detecting_, int(L * fs_));
        }
        d.next  = 0;
        d.state = d.n > 0 ? Drop::Bouncing : Drop::Free;
        detecting_ = -1;
    }

    float process(float x)
    {
        if (onset_.process(x)) startDrop();

        for (int s = 0; s < kMaxDrops; ++s)
        {
            Drop& d = drops_[s];
            if (d.state == Drop::Free) continue;
            if (d.sampled) grains_.record(s, x);           // no-op once the grain is full
            if (d.state == Drop::Detecting)
            {
                pd_.push(x);
                if (d.age < peakLen_) d.attackPeak = std::fmax(d.attackPeak, std::fabs(x));
            }
            else
            {
                while (d.next < d.n && d.age >= d.at[d.next]) fire(s, d);
                if (d.next >= d.n) d.state = Drop::Free;
            }
            ++d.age;
        }
        return synth_.process() + grains_.process();
    }

    int activeDrops() const
    {
        int n = 0;
        for (const Drop& d : drops_) n += d.state != Drop::Free;
        return n;
    }
    int activeRings() const { return synth_.activeRings(); }

private:
    struct Drop {
        enum State { Free, Detecting, Bouncing } state = Free;
        DropParams params;
        bool sampled = false;
        float attackPeak = 0.0f;
        uint32_t age = 0, stamp = 0;
        int n = 0, next = 0;
        uint32_t at[kMaxBounces];       // bounce times, samples after the attack
        float amp[kMaxBounces], semis[kMaxBounces];
    };

    void startDrop()
    {
        int s = 0;
        for (int i = 0; i < kMaxDrops; ++i)
        {
            if (drops_[i].state == Drop::Free) { s = i; break; }
            if (drops_[i].stamp < drops_[s].stamp) s = i;   // steal the oldest
        }
        Drop& d = drops_[s];
        d.state = Drop::Detecting;
        d.params = DropParams{ 0.0f, p_.bounce, p_.height, p_.pitch, p_.glitch, p_.steps, p_.reverse };
        d.sampled = p_.sampled;
        d.attackPeak = 0.0f;
        d.age = 0;
        d.n = d.next = 0;
        d.stamp = ++stamp_;
        grains_.clearDrop(s);
        pd_.start();
        detecting_ = s;                 // a still-detecting older drop is abandoned
        for (int i = 0; i < kMaxDrops; ++i)
            if (i != s && drops_[i].state == Drop::Detecting) drops_[i].state = Drop::Free;
    }

    void fire(int s, Drop& d)
    {
        const int k = d.next++;
        if (d.sampled)
        {
            const float toNext = k + 1 < d.n ? float(d.at[k + 1] - d.at[k]) / float(fs_) : 0.2f;
            grains_.hit(s, d.semis[k], d.amp[k], 0.9f * toNext);
        }
        else
            synth_.hit(d.params.f0, d.semis[k], d.attackPeak * d.amp[k], rng_);
    }

    double fs_ = 48000.0;
    Onset onset_;
    PitchDetector pd_;
    CoinSynth synth_;
    Grains grains_;
    Rng rng_{ 12345u };
    EngineParams p_{ -32.0f, 0.92f, 0.5f, 12.0f, 0.0f, Steps::Semitones, false, false };
    Drop drops_[kMaxDrops];
    int detecting_ = -1;
    float lastHz_ = 0.0f;
    uint32_t peakLen_ = 960, stamp_ = 0;
};

} // namespace toss
