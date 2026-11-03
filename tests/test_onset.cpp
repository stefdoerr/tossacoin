// Hard-attack detector: hard plucks drop a coin, soft playing and a note's
// own sustain never do.
#include "test_util.hpp"
#include "dsp/Onset.hpp"
#include "dsp/Rng.hpp"

#include <vector>

static const double kFs = 48000.0;

// Run the detector over x and return the trigger sample indices.
static std::vector<uint32_t> triggers(const std::vector<float>& x)
{
    toss::Onset on;
    on.prepare(kFs);
    on.setThresholdDb(toss::sensitivityToThresholdDb(50.0f));
    std::vector<uint32_t> t;
    for (uint32_t i = 0; i < x.size(); ++i)
        if (on.process(x[i])) t.push_back(i);
    return t;
}

static std::vector<float> plucks(float peak, std::vector<double> starts, double len, double decay = 0.3)
{
    std::vector<float> x(uint32_t(len * kFs), 0.0f);
    for (double s : starts)
        for (uint32_t i = uint32_t(s * kFs), j = 0; i < x.size(); ++i, ++j)
            x[i] += pluck(110.0f, peak, kFs, j, decay);
    return x;
}

int main()
{
    {   // hard_pluck_triggers_once
        auto t = triggers(plucks(0.1f, {0.0}, 1.0));
        CHECK(t.size() == 1);
        CHECK(!t.empty() && t[0] < uint32_t(0.010 * kFs));
    }
    {   // soft_pluck_never_triggers (-38 dBFS)
        CHECK(triggers(plucks(0.0126f, {0.0}, 1.0)).empty());
    }
    {   // two_plucks_300ms_apart_trigger_twice
        auto t = triggers(plucks(0.1f, {0.0, 0.3}, 1.0));
        CHECK(t.size() == 2);
        if (t.size() == 2)
            CHECK(std::fabs((t[1] - t[0]) / kFs - 0.3) <= 0.010);
    }
    {   // sustain_does_not_retrigger
        CHECK(triggers(plucks(0.1f, {0.0}, 3.0, 2.0)).size() == 1);
    }
    {   // threshold_mapping
        CHECK(toss::sensitivityToThresholdDb(0.0f) == -20.0f);
        CHECK(toss::sensitivityToThresholdDb(50.0f) == -40.0f);
        CHECK(toss::sensitivityToThresholdDb(100.0f) == -60.0f);
    }
    {   // rng_range
        toss::Rng rng(1);
        double sum = 0.0;
        bool inRange = true;
        for (int i = 0; i < 100000; ++i)
        {
            const float u = rng.uniform(0.97f, 1.03f);
            inRange = inRange && u >= 0.97f && u < 1.03f;
            sum += u;
        }
        CHECK(inRange);
        CHECK(std::fabs(sum / 100000.0 - 1.0) < 0.001);
    }
    REPORT();
}
