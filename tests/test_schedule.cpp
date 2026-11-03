// Bounce schedule: geometric gaps set by pitch, smooth (never "in pairs"),
// reverse, notBefore, pitch steps, glitch.
#include "test_util.hpp"
#include "dsp/Schedule.hpp"

#include <vector>

using namespace toss;

static DropParams defaults()
{
    DropParams p;
    p.f0 = 82.4f; p.bounce = 0.92f; p.height = 0.5f; p.pitch = 12.0f; p.glitch = 0.0f;
    p.steps = Steps::Semitones; p.reverse = false;
    return p;
}

static std::vector<Bounce> run(const DropParams& p, float notBefore = 0.0f, uint32_t seed = 1)
{
    Rng rng(seed);
    std::vector<Bounce> b(kMaxBounces);
    b.resize(makeSchedule(p, notBefore, rng, b.data()));
    return b;
}

static float gap(const std::vector<Bounce>& b, size_t k) { return k ? b[k].time - b[k - 1].time : b[0].time; }
static bool isInt(float s) { return s == std::round(s); }

int main()
{
    {   // first_gap_tracks_pitch
        DropParams p = defaults();
        CHECK(std::fabs(run(p)[0].time / 0.5f - 1.0f) <= 0.03f);
        p.f0 = 1046.5f;
        CHECK(std::fabs(run(p)[0].time / 0.1403f - 1.0f) <= 0.03f);
    }
    {   // e2_at_92_has_about_50_bounces
        const size_t n = run(defaults()).size();
        std::printf("  E2 @ 92%%: %zu bounces\n", n);
        CHECK(n >= 48 && n <= 52);
    }
    {   // gaps_shrink_smoothly (regression: the prototype's "pairs" bug)
        const auto b = run(defaults());
        bool ok = true;
        for (size_t k = 1; k < b.size(); ++k)
        {
            const float r = gap(b, k) / gap(b, k - 1);
            ok = ok && gap(b, k) > 0.0f && r >= 0.92f * 0.94f && r <= 0.92f * 1.07f;
        }
        CHECK(ok);
        // last_gap_not_below_8ms (planned gap >= 8 ms, jitter at most -3%)
        CHECK(gap(b, b.size() - 1) >= 0.008f * 0.97f);
    }
    {   // reverse_gaps_grow_and_fade
        DropParams p = defaults();
        p.reverse = true;
        const auto b = run(p);
        bool grow = true, fade = true;
        for (size_t k = 1; k < b.size(); ++k)
        {
            const float r = gap(b, k) / gap(b, k - 1);
            grow = grow && r >= 0.94f / 0.92f && r <= 1.07f / 0.92f;
            fade = fade && b[k].amp <= 1.4f * b[k - 1].amp;
        }
        CHECK(grow);
        CHECK(fade);
        CHECK(b.back().amp < 0.5f * b.front().amp);
    }
    {   // notBefore_shifts_reverse
        DropParams p = defaults();
        p.reverse = true;
        const auto a = run(p), b = run(p, 0.07f);
        CHECK(b[0].time >= 0.07f && b[0].time <= 0.0701f);
        bool same = a.size() == b.size();
        for (size_t k = 1; same && k < a.size(); ++k)
            same = std::fabs(gap(a, k) - gap(b, k)) < 1e-5f;
        CHECK(same);
    }
    {   // semitones_path
        const auto b = run(defaults());
        bool ok = true;
        for (size_t k = 0; k < b.size(); ++k)
            ok = ok && isInt(b[k].semis) && (k == 0 || b[k].semis >= b[k - 1].semis);
        CHECK(ok);
        CHECK(b.back().semis == 12.0f);
    }
    {   // fifths_down
        DropParams p = defaults();
        p.pitch = -19.0f; p.steps = Steps::FifthsOctaves;
        const auto b = run(p);
        bool ok = true;
        for (const Bounce& x : b)
            ok = ok && (x.semis == 0.0f || x.semis == -7.0f || x.semis == -12.0f || x.semis == -19.0f);
        CHECK(ok);
        CHECK(b.back().semis == -19.0f);
    }
    {   // octaves
        DropParams p = defaults();
        p.pitch = 24.0f; p.steps = Steps::Octaves;
        bool ok = true;
        for (const Bounce& x : run(p))
            ok = ok && (x.semis == 0.0f || x.semis == 12.0f || x.semis == 24.0f);
        CHECK(ok);
    }
    {   // glide_not_snapped
        DropParams p = defaults();
        p.steps = Steps::Glide;
        bool someFraction = false;
        for (const Bounce& x : run(p)) someFraction = someFraction || !isInt(x.semis);
        CHECK(someFraction);
    }
    {   // glitch_zero_is_base_path
        const auto b = run(defaults());
        bool ok = true;
        for (size_t k = 0; k < b.size(); ++k)
            ok = ok && b[k].semis == snapSemis(12.0f * (k + 1) / b.size(), Steps::Semitones);
        CHECK(ok);
    }
    {   // glitch_one_deviates
        for (Steps st : { Steps::Semitones, Steps::FifthsOctaves })
        {
            DropParams p = defaults();
            p.glitch = 1.0f; p.steps = st;
            const auto b = run(p);
            size_t differ = 0;
            bool onGrid = true;
            for (size_t k = 0; k < b.size(); ++k)
            {
                differ += b[k].semis != snapSemis(12.0f * (k + 1) / b.size(), st);
                onGrid = onGrid && std::fabs(b[k].semis) <= 36.0f && b[k].semis == snapSemis(b[k].semis, st);
            }
            CHECK(differ * 4 >= b.size());
            CHECK(onGrid);
        }
    }
    {   // snap_table
        CHECK(snapSemis(4.4f, Steps::FifthsOctaves) == 7.0f);
        CHECK(snapSemis(-9.6f, Steps::Octaves) == -12.0f);
        CHECK(snapSemis(3.5f, Steps::Glide) == 3.5f);
        CHECK(snapSemis(-2.6f, Steps::Semitones) == -3.0f);
    }
    {   // bounce_97_bounded
        DropParams p = defaults();
        p.f0 = 40.0f; p.height = 1.5f; p.bounce = 0.97f;
        const auto b = run(p);
        bool finite = true;
        for (const Bounce& x : b) finite = finite && std::isfinite(x.time) && std::isfinite(x.amp);
        CHECK(b.size() <= size_t(kMaxBounces));
        CHECK(finite);
    }
    REPORT();
}
