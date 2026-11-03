#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace toss {

// Sampled mode. Each drop slot records the first 120 ms after its attack;
// every bounce plays that grain back resampled by 2^(semis/12) (higher and
// shorter), with a 1 ms fade-in and an exponential decay (tau = L/4), cut
// before the next bounce with a 64-sample fade. Players are a fixed pool;
// when it is full the oldest player is replaced.
class Grains {
public:
    void prepare(double fs, int maxDrops, int maxPlayers)
    {
        fs_ = fs;
        cap_ = int(0.120 * fs);
        buf_.assign(size_t(cap_) * size_t(maxDrops), 0.0f);
        recorded_.assign(size_t(maxDrops), 0);
        len_.assign(size_t(maxDrops), 0);
        players_.assign(size_t(maxPlayers), Player());
        fadeIn_ = float(0.001 * fs);
        reset();
    }

    void reset()
    {
        for (Player& p : players_) p.active = false;
        for (size_t d = 0; d < recorded_.size(); ++d) recorded_[d] = len_[d] = 0;
        active_ = 0;
        stamp_ = 0;
    }

    // A new drop takes this slot: forget the old grain, fade its players out.
    void clearDrop(int drop)
    {
        recorded_[size_t(drop)] = len_[size_t(drop)] = 0;
        for (Player& p : players_)
            if (p.active && p.drop == drop && p.fadeOut < 0) p.fadeOut = kFade;
    }

    void record(int drop, float x)
    {
        int& n = recorded_[size_t(drop)];
        if (n < cap_) buf_[size_t(drop) * size_t(cap_) + size_t(n++)] = x;
    }
    int recorded(int drop) const { return recorded_[size_t(drop)]; }

    void lockLength(int drop, int samples)
    {
        int L = samples < recorded_[size_t(drop)] ? samples : recorded_[size_t(drop)];
        len_[size_t(drop)] = L < cap_ ? L : cap_;
    }

    void hit(int drop, float semis, float amp, float maxLenSec)
    {
        const int L = len_[size_t(drop)];
        if (L < 2) return;
        const float rate = std::pow(2.0f, semis / 12.0f);
        int n = int(float(L - 1) / rate);
        const int maxN = int(maxLenSec * float(fs_));
        if (n > maxN) n = maxN;
        if (n < 2) return;

        Player& p = freeOrOldest();
        if (!p.active) ++active_;
        p.active  = true;
        p.drop    = drop;
        p.len     = L;
        p.pos     = 0.0f;
        p.rate    = rate;
        p.amp     = amp;
        p.env     = 1.0f;
        p.envMul  = std::exp(-rate / (0.25f * float(L)));
        p.left    = n;
        p.endFade = n > 2 * kFade ? kFade : n / 2;
        p.fadeOut = -1;
        p.stamp   = ++stamp_;
    }

    float process()
    {
        float out = 0.0f;
        for (Player& p : players_)
        {
            if (!p.active) continue;
            const int i = int(p.pos);
            if (i + 1 >= p.len) { stop(p); continue; }
            const float* g = &buf_[size_t(p.drop) * size_t(cap_)];
            const float fr = p.pos - float(i);
            float v = g[i] + (g[i + 1] - g[i]) * fr;
            float gain = p.amp * p.env;
            if (p.pos < fadeIn_)       gain *= p.pos / fadeIn_;
            if (p.left <= p.endFade)   gain *= float(p.left) / float(p.endFade);
            if (p.fadeOut >= 0)        gain *= float(p.fadeOut) / float(kFade);
            out += v * gain;

            p.pos += p.rate;
            p.env *= p.envMul;
            if (--p.left <= 0 || (p.fadeOut >= 0 && --p.fadeOut <= 0)) stop(p);
        }
        return out;
    }

    int activePlayers() const { return active_; }

private:
    static constexpr int kFade = 64;

    struct Player {
        bool active = false;
        int drop = 0, len = 0, left = 0, endFade = 0, fadeOut = -1;
        float pos = 0, rate = 1, amp = 0, env = 1, envMul = 1;
        uint32_t stamp = 0;
    };

    void stop(Player& p) { p.active = false; --active_; }

    Player& freeOrOldest()
    {
        Player* o = &players_[0];
        for (Player& p : players_)
        {
            if (!p.active) return p;
            if (p.stamp < o->stamp) o = &p;
        }
        return *o;
    }

    double fs_ = 48000.0;
    int cap_ = 0, active_ = 0;
    float fadeIn_ = 48.0f;
    uint32_t stamp_ = 0;
    std::vector<float> buf_;
    std::vector<int> recorded_, len_;
    std::vector<Player> players_;
};

} // namespace toss
