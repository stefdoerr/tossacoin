#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace toss {

// Incremental YIN, 40 Hz-1.5 kHz. start() at the onset, then push() every
// input sample and call work(maxLags) once per block. The first 20 ms (pick
// noise) are discarded. Lags are computed in increasing order as soon as
// their samples have arrived, and detection stops at the first dip, so high
// notes finish early and the per-block cost is bounded by maxLags x W MACs.
class PitchDetector {
public:
    static constexpr float kMinHz = 40.0f, kMaxHz = 1500.0f;

    void prepare(double fs)
    {
        fs_     = fs;
        skip_   = uint32_t(std::lround(0.020 * fs));
        W_      = uint32_t(std::lround(0.0213 * fs));
        tauMin_ = uint32_t(std::floor(fs / kMaxHz));
        tauMax_ = uint32_t(std::ceil(fs / kMinHz));
        buf_.assign(W_ + tauMax_ + 2, 0.0f);
        cmnd_.assign(tauMax_ + 2, 1.0f);
        start();
        done_ = true;                       // idle until the first start()
    }

    void start()
    {
        skipped_ = n_ = 0;
        nextTau_ = 1;
        cum_ = 0.0;
        minTau_ = 0;
        done_ = false;
        hz_ = 0.0f;
    }

    void push(float x)
    {
        if (done_) return;
        if (skipped_ < skip_) { ++skipped_; return; }
        if (n_ < buf_.size()) buf_[n_++] = x;
    }

    void work(int maxLags)
    {
        for (; maxLags > 0 && !done_; --maxLags)
        {
            const uint32_t t = nextTau_;
            if (n_ < W_ + t + 1) return;     // its samples haven't arrived yet

            // Float with 4 independent partial sums: vectorises on the
            // Dwarf's NEON unit. The serial double loop this replaces cost
            // ~55% of a 128-frame block there (measured by make dwarf-bench).
            const float* x = buf_.data();
            const float* y = x + t;
            float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
            uint32_t j = 0;
            for (; j + 4 <= W_; j += 4)
            {
                const float e0 = x[j] - y[j], e1 = x[j + 1] - y[j + 1];
                const float e2 = x[j + 2] - y[j + 2], e3 = x[j + 3] - y[j + 3];
                s0 += e0 * e0; s1 += e1 * e1; s2 += e2 * e2; s3 += e3 * e3;
            }
            for (; j < W_; ++j) { const float e = x[j] - y[j]; s0 += e * e; }
            const double d = double((s0 + s1) + (s2 + s3));
            cum_ += d;
            cmnd_[t] = cum_ > 0.0 ? float(d * t / cum_) : 1.0f;
            ++nextTau_;

            const uint32_t c = t - 1;        // candidate: needs its right neighbour
            if (c >= tauMin_ && c <= tauMax_)
            {
                if (minTau_ == 0 || cmnd_[c] < cmnd_[minTau_]) minTau_ = c;
                if (cmnd_[c] < 0.15f && cmnd_[c] <= cmnd_[t]) { finish(c); return; }
            }
            if (t > tauMax_)                 // whole range done, no clear dip
            {
                if (minTau_ != 0 && cmnd_[minTau_] < 0.35f) finish(minTau_);
                else { hz_ = 0.0f; done_ = true; }
                return;
            }
        }
    }

    bool  done() const { return done_; }
    float hz()   const { return hz_; }

private:
    void finish(uint32_t tau)
    {
        double shift = 0.0;
        if (tau > 1 && tau + 1 < nextTau_)
        {
            const double a = cmnd_[tau - 1], b = cmnd_[tau], c = cmnd_[tau + 1];
            const double den = a - 2.0 * b + c;
            if (den != 0.0) shift = 0.5 * (a - c) / den;
            if (shift < -1.0 || shift > 1.0) shift = 0.0;
        }
        hz_ = float(fs_ / (tau + shift));
        done_ = true;
    }

    double fs_ = 48000.0, cum_ = 0.0;
    uint32_t skip_ = 0, W_ = 0, tauMin_ = 0, tauMax_ = 0;
    uint32_t skipped_ = 0, n_ = 0, nextTau_ = 1, minTau_ = 0;
    bool done_ = true;
    float hz_ = 0.0f;
    std::vector<float> buf_, cmnd_;
};

} // namespace toss
