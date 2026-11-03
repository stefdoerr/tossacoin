# Toss a Coin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A MOD Dwarf / desktop pedal where each hard attack drops a ringing "coin" whose bounces speed up and step in pitch. The played note's pitch sets how fast the bounces start. Shipped with the full Boreas treatment by 1 December 2026.

**Architecture:** A DPF plugin forked from `~/Fun/mod-plugin-template`. The DSP lives in small header-only units under `plugins/TossACoin/dsp/` (namespace `toss`), each with a host-less test. `CoinEngine` wires them together: onset → pitch detection → bounce schedule → synth rings or sampled grains. `TossACoinPlugin.cpp` adds the parameters, bypass, level and soft limit on top of the engine.

**Tech Stack:** C++14 (gnu++14), DPF (submodule), LV2 + VST3 + CLAP, modgui (HTML/CSS/JS), Docker cross-builds (Dwarf aarch64, Patchstorage), headless Chrome for the manual, Playwright for the modgui screenshot.

**Spec:** `docs/superpowers/specs/2026-11-02-tossacoin-design.md`. The sound reference is `docs/superpowers/specs/2026-11-02-tossacoin-prototype.py`. Read both before any task. Also read `INSTRUCTIONS.md` (copied from the template) before touching the build, modgui, tests or the Dwarf.

## Global Constraints

- **Identity:**
  - `PLUGIN := tossacoin`
  - `PLUGIN_DIR := plugins/TossACoin`
  - `BRAND := Stefan`
  - `LABEL := Toss a Coin`
  - `PLUGIN_URI_BASE := http://tossacoin.local/plugins`
  - Homepage `https://github.com/stefdoerr/tossacoin`
  - CLAP id `local.tossacoin.tossacoin` (beta: `local.tossacoin.tossacoin-beta`)
  - Brand ID `Toss` / unique ID `dToC` (beta: `TosB` / `dToB`)
  - LV2 category `lv2:PitchPlugin`
- **Real-time safety:**
  - No allocation, locks, I/O or loops of transcendental functions in `run()`.
  - Size all buffers in `prepare(fs)`.
  - Per-hit setup (a few `cos`/`exp` per mode) is allowed.
  - No per-sample `std::exp`/`std::pow`/`std::sin`.
- **Sample rates and block sizes:** 44.1, 48 and 96 kHz, any block size. On the Dwarf, budget for 48 kHz with 128-frame blocks (2.67 ms).
- **Audio layout:** the template's mono in, stereo out. Both outputs carry the same signal.
- **Dry signal:** always at unity. Output is `dry + Level × softLimit(wet)`, with the template's click-free bypass crossfade around everything.
- **Defaults (spec):**
  - Bounce 92%
  - Height 0.5 s
  - Pitch +12
  - Steps Semitones
  - Glitch 0%
  - Reverse off
  - Sound Synth
  - Level 0 dB
  - Sensitivity 50%
- **Look:** a travelling minstrel's style. No franchise or character names, logos, wolf medallion, or art from the show or games, anywhere in the repo, pedal or manual. The README may name the song the title nods to.
- **CPU target, measured on the Dwarf by `make dwarf-bench`:** average ≤ 20% and worst block ≤ 50% of the 2.67 ms budget in the worst-case stress (hard plucks every 250 ms at Bounce 97%).
- **Before release, port order is free.** After the first release, append ports only and never remove them (`INSTRUCTIONS.md`, "Removing or renaming a port").

## Review Focus

1. **Sample rates other than 48 kHz** (desktop DAWs at 44.1 or 96 kHz). Timing in seconds and pitch in Hz must hold. Tested in Task 7.
2. **Odd host block sizes** (1, 37 or 1024 frames). Pitch work is budgeted per frame, so first-coin timing must not depend on block size beyond one block. Tested in Task 8.
3. **Knobs turned while coins are bouncing.** A drop keeps the parameters it started with. Changes apply to the next drop, never NaN or clicks. Tested in Task 7.
4. **Long silence after a drop.** Rings and grains free themselves, so no work or denormals are left. `activeRings() == 0` within 3 s. Tested in Tasks 5 and 7.
5. **Very hot input** (0 dBFS plucks, a synth into the pedal). The wet peak stays bounded by the soft limit and never hard-clips beyond the dry's own level. Tested in Task 8.

---

## File Structure

```
tossacoin/
├── (template files: Makefile, install.sh, mod-build/, patchstorage-build/, .github/, dpf/ submodule, INSTRUCTIONS.md, DEVELOPERS.md)
├── plugins/TossACoin/
│   ├── TossACoinPlugin.cpp     — DPF glue: params, port groups, bypass, level, soft limit
│   ├── DistrhoPluginInfo.h     — identity
│   ├── Makefile                — inner DPF makefile (renamed)
│   ├── dsp/Rng.hpp             — xorshift32 RNG (RT-safe, seedable)
│   ├── dsp/Onset.hpp           — hard-attack detector
│   ├── dsp/PitchDetector.hpp   — incremental YIN, 40 Hz–1.5 kHz
│   ├── dsp/Schedule.hpp        — bounce times/amps/semitones + snapping + glitch
│   ├── dsp/CoinSynth.hpp       — modal plate ring pool
│   ├── dsp/Grains.hpp          — sampled-mode grain recorder/players
│   ├── dsp/CoinEngine.hpp      — drops lifecycle, wires the above
│   ├── modgui/…, modgui.ttl    — themed pedal (Task 11)
├── tests/test_*.cpp            — host-less tests, run by `make test` (ASAN)
├── tests/bench_dwarf.cpp       — CPU benchmark, cross-built and run on the Dwarf (Task 9)
└── docs/manual/tossacoin-manual.{html,pdf}
```

Tests include DSP headers as `#include "dsp/Onset.hpp"`. The template's `make test` already passes `-I$(PLUGIN_DIR)` and compiles every `tests/test_*.cpp`. Shared test helpers (tone generators, Goertzel, RMS) go in `tests/test_util.hpp`, created in Task 2:
- `float pluck(float hz, float peak, double fs, uint32_t i)`: 8 harmonics at 1/k amplitude, a 5 ms raised-cosine attack and a 0.3 s exponential decay, normalised to `peak`.
- `double goertzel(const float*, uint32_t n, double hz, double fs)`
- `double rms(const float*, uint32_t n)`
- `CHECK`/`REPORT` macros as in `~/Fun/boreas/tests/test_util.hpp`

---

### Task 1: Create the repo from the template and rename it

**Files:**
- Create: everything copied from `~/Fun/mod-plugin-template` (its **working tree**, which holds uncommitted improvements such as `tests/test_bypass.cpp`)
- Modify: `Makefile:546-550`, `plugins/TossACoin/{DistrhoPluginInfo.h,Makefile,TossACoinPlugin.cpp,modgui.ttl,modgui/*}`, `patchstorage.json`, `VERSION`
- Delete: `tests/test_gain.cpp` (tests the template's gain example, which is replaced)

- [ ] **Step 1: Copy the template next to the existing docs**

```bash
rsync -a --exclude .git --exclude dpf --exclude bin --exclude build --exclude dist \
  ~/Fun/mod-plugin-template/ ~/Fun/tossacoin/
cd ~/Fun/tossacoin && git init -b main
git submodule add https://github.com/DISTRHO/DPF.git dpf
git -C dpf checkout 4238e1c7f0351bbe488d79f0899c540543ac7583 && git -C dpf submodule update --init --recursive
```

- [ ] **Step 2: Start the Dwarf toolchain image build in the background (30–60 min, once per machine)**

Run `make dwarf-image` in the background and carry on. Task 9 needs it.

- [ ] **Step 3: Rename** following the README's "Renaming" section, with the identity values from Global Constraints:
  - `plugins/MyPlugin` → `plugins/TossACoin`
  - `MyPluginPlugin.cpp` → `TossACoinPlugin.cpp`, with class `TossACoinPlugin`
  - Macros `MYPLUGIN_BETA` → `TOSSACOIN_BETA`
  - modgui file names `*-myplugin.*` → `*-tossacoin.*`
  - Manual `docs/manual/myplugin-manual.*` → `tossacoin-manual.*`
  - Set `DISTRHO_PLUGIN_LV2_CATEGORY` to `"lv2:PitchPlugin"`, `PLUGIN_HOMEPAGE` to the GitHub URL, and `patchstorage.json` `source_code_url` to the GitHub URL
  - Leave `VERSION` at `0.0.0`
  - Leave `INSTRUCTIONS.md` and `DEVELOPERS.md` untouched. They are template docs, as in Boreas.

- [ ] **Step 4: Verify the rename is complete**

Run: `grep -ri myplugin --exclude-dir=dpf --exclude-dir=.git --exclude=INSTRUCTIONS.md --exclude=DEVELOPERS.md . ; rm tests/test_gain.cpp; make && make test`
Expected: grep prints nothing. `bin/tossacoin.lv2/` contains `tossacoin.ttl` with `lv2:PitchPlugin`, and `test_bypass` passes (it still exercises the template's gain knob at index 0).

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "Fork mod-plugin-template as Toss a Coin; add design spec and prototype"
```

---

### Task 2: RNG and hard-attack detector

**Files:**
- Create: `plugins/TossACoin/dsp/Rng.hpp`, `plugins/TossACoin/dsp/Onset.hpp`, `tests/test_util.hpp`, `tests/test_onset.cpp`

**Interfaces:**
- Produces:
  - `struct toss::Rng { explicit Rng(uint32_t seed = 1); float uniform(); /* [0,1) */ float uniform(float a, float b); };` (xorshift32; a seed of 0 is replaced by 1)
  - `class toss::Onset { void prepare(double fs); void setThresholdDb(float db); void reset(); bool process(float x); float fastEnv() const; };`
  - `inline float toss::sensitivityToThresholdDb(float percent)` returns `-12 - 0.4 * percent` (0% → −12 dBFS, 50% → −32, 100% → −52)

`Onset::process` returns true on exactly the sample a drop should start. It uses one-pole envelopes of `|x|`, fast 2 ms and slow 100 ms. It triggers on the rising edge of `fast > threshold && fast > 2.5 × slow`, with a 250 ms hold-off (prototype values, from the spec).

- [ ] **Step 1: Write `tests/test_onset.cpp`** at 48 kHz, threshold `sensitivityToThresholdDb(50)`:
  - `hard_pluck_triggers_once`: `pluck(110, 0.1)` for 1 s gives exactly 1 trigger, within 10 ms of the start.
  - `soft_pluck_never_triggers`: `pluck(110, 0.0126)` (−38 dBFS) gives 0 triggers.
  - `two_plucks_300ms_apart_trigger_twice`: 2 triggers, 300 ± 10 ms apart.
  - `sustain_does_not_retrigger`: a pluck with 2 s decay gives 1 trigger.
  - `threshold_mapping`: `sensitivityToThresholdDb(0) == -12`, `(50) == -32`, `(100) == -52`.
  - `rng_range`: 10⁵ draws of `uniform(0.97f, 1.03f)` all lie in `[0.97, 1.03)` and their mean is within 0.001 of 1.0.

- [ ] **Step 2: Run `make test` and confirm `test_onset` fails to compile (headers missing)**

- [ ] **Step 3: Implement `Rng.hpp` and `Onset.hpp`**

- [ ] **Step 4: Run `make test`.** Expected: all tests pass.

- [ ] **Step 5: Commit** — `git commit -m "dsp: RNG and hard-attack onset detector"`

---

### Task 3: Incremental pitch detector

**Files:**
- Create: `plugins/TossACoin/dsp/PitchDetector.hpp`, `tests/test_pitch.cpp`

**Interfaces:**
- Produces:
  - `class toss::PitchDetector { void prepare(double fs); void start(); void push(float x); void work(int maxLags); bool done() const; float hz() const; };`
  - `hz()` returns 0 when no pitch was found.
  - Range constants: `kMinHz = 40.f`, `kMaxHz = 1500.f`.

The algorithm is pinned because the latency tests depend on it:
- `start()` is called at the onset. `push()` receives every following input sample and discards the first `round(0.020 · fs)` (the pick noise).
- It is YIN, with `W = round(0.0213 · fs)` (1024 at 48 kHz) and `τ` in `[floor(fs/1500), ceil(fs/40)]`.
- `d(τ) = Σ_{j<W} (x[j] − x[j+τ])²`, and `cmnd(τ) = d(τ)·τ / Σ_{1..τ} d`.
- `work(maxLags)` computes the next lags **in increasing τ**, only those whose `W + τ + 1` samples have arrived, and at most `maxLags` per call.
- It finishes at the **first** τ with `cmnd(τ) < 0.15` and `cmnd(τ) ≤ cmnd(τ+1)`, refined with parabolic interpolation. High notes therefore finish early.
- If τmax is reached with no dip, it uses the global minimum if that is below 0.35, and otherwise reports `hz = 0`.

The plugin calls `work(frames * 3 / 4)` once per block.

- [ ] **Step 1: Write `tests/test_pitch.cpp`** at 48 kHz, feeding 128-sample blocks (push 128 samples, then `work(96)`):
  - `accuracy`: plucks at 41.2, 82.4, 110, 261.6, 440, 1046.5 and 1400 Hz are detected within ±1%.
  - `weak_fundamental`: 82.4 Hz with fundamental amplitude 0.3 and 2nd harmonic 1.0 still reads 82.4 ± 1% (no octave error).
  - `latency`: `done()` becomes true within 0.050 s of `start()` at 1046.5 Hz, 0.075 s at 82.4 Hz and 0.100 s at 41.2 Hz.
  - `noise_is_unpitched`: white noise at peak 0.1 gives `done()` and `hz() == 0`.
  - `fs_44100`: at 44.1 kHz a 110 Hz pluck reads within ±1%.

- [ ] **Step 2: Run `make test` and confirm it fails**

- [ ] **Step 3: Implement `PitchDetector.hpp`** (buffers sized in `prepare`, running cumulative sum of `d`)

- [ ] **Step 4: Run `make test`.** Expected: PASS.

- [ ] **Step 5: Commit** — `git commit -m "dsp: incremental YIN pitch detector"`

---

### Task 4: Bounce schedule, pitch path, snapping, glitch

**Files:**
- Create: `plugins/TossACoin/dsp/Schedule.hpp`, `tests/test_schedule.cpp`

**Interfaces:**
- Consumes: `toss::Rng`
- Produces:
  ```cpp
  enum class Steps : int { Glide = 0, Semitones = 1, FifthsOctaves = 2, Octaves = 3 };
  struct DropParams { float f0, bounce, height, pitch, glitch; Steps steps; bool reverse; };
  struct Bounce { float time; float amp; float semis; };   // time: seconds after the attack
  constexpr int kMaxBounces = 256;
  float snapSemis(float s, Steps steps);
  int makeSchedule(const DropParams& p, float notBefore, Rng& rng, Bounce* out);   // returns n
  ```

The rules come from the spec:
- **Gaps:** `T1 = height · sqrt(82.4 / f0)`. Planned gap `g_k = T1 · bounce^k`. Stop **before** a planned gap below 0.008 s, at amplitude below 0.001, or at `kMaxBounces`.
- **Order:** forward uses the gaps as planned. Reverse reverses the list.
- **Jitter:** each gap is multiplied by `U(0.97, 1.03)` **independently**. `time_k` is the cumulative sum of the jittered gaps. Never jitter the cumulative time.
- **Amplitude:** forward `bounce^(k+1) · U(0.8, 1.1)`, reverse `bounce^((k+1)/2) · U(0.8, 1.1)`, with k in time order.
- **notBefore:** shift every time by `max(0, notBefore − time_0)`, so no bounce is scheduled before the pitch is known.
- **Pitch path:**
  - `base_k = pitch · (k+1) / n`.
  - Glitch: with probability `glitch` (0–1), draw `j = U(−12, 12)`. On a further coin flip, either add `j` to bounce k only, or add it to a running offset carried by bounce k and every later bounce.
  - Clamp to ±36, then `snapSemis`.
- **`snapSemis`:**
  - Glide returns `s` unchanged.
  - Semitones rounds to the nearest integer.
  - FifthsOctaves snaps to the nearest of ±{0, 7, 12, 19, 24, 31, 36}.
  - Octaves snaps to the nearest of ±{0, 12, 24, 36}.

- [ ] **Step 1: Write `tests/test_schedule.cpp`** (seed 1, `notBefore = 0` unless stated):
  - `first_gap_tracks_pitch`: `height 0.5`: `time_0` is 0.5 ± 3% at f0 82.4 and 0.1403 ± 3% at f0 1046.5.
  - `e2_at_92_has_about_50_bounces`: n is in [48, 52].
  - `gaps_shrink_smoothly`: forward, every gap is `> 0` and `gap_k / gap_{k−1}` is in [0.92·0.94, 0.92·1.07]. This is the regression test for the prototype's "pairs" bug.
  - `last_gap_not_below_8ms`.
  - `reverse_gaps_grow_and_fade`: reverse gaps are increasing (same ratio band, inverted) and amplitudes are non-increasing within jitter: `amp_k ≤ 1.4 · amp_{k−1}`.
  - `notBefore_shifts_reverse`: reverse with `notBefore = 0.07` gives `time_0` in [0.07, 0.0701] and keeps the gaps.
  - `semitones_path`: pitch +12 gives integer semis, non-decreasing, last == 12.
  - `fifths_down`: pitch −19 with FifthsOctaves gives every value in −{0, 7, 12, 19}, last == −19.
  - `octaves`: values in {0, 12, 24}.
  - `glide_not_snapped`: some value is non-integer.
  - `glitch_zero_is_base_path`: identical to the run without glitch.
  - `glitch_one_deviates`: at least 25% of bounces differ from the base path, and all are within ±36 and on the Steps grid.
  - `snap_table`: `snapSemis(4.4, FifthsOctaves) == 7`, `snapSemis(-9.6, Octaves) == -12`, `snapSemis(3.5, Glide) == 3.5`.
  - `bounce_97_bounded`: f0 40, height 1.5, bounce 0.97 gives n ≤ 256 and finite times.

- [ ] **Step 2: Run `make test` and confirm it fails**

- [ ] **Step 3: Implement `Schedule.hpp`**

- [ ] **Step 4: Run `make test`.** Expected: PASS.

- [ ] **Step 5: Commit** — `git commit -m "dsp: bounce schedule with pitch steps and glitch"`

---

### Task 5: Synth coin ring pool

**Files:**
- Create: `plugins/TossACoin/dsp/CoinSynth.hpp`, `tests/test_coin.cpp`

**Interfaces:**
- Consumes: `toss::Rng`
- Produces:
  ```cpp
  float coinFundamental(float noteHz, float semis);  // noteHz · 2^(semis/12) · 4, halved until ≤ 8000
  class CoinSynth {
    static constexpr int kModes = 6;
    void prepare(double fs, int maxRings);   // maxRings starts at 32; Task 9 sets the final value
    void reset();
    void hit(float noteHz, float semis, float amp, Rng& rng);
    float process();                          // one sample, sum of active rings
    int activeRings() const;
  };
  ```

Spec values:
- Mode ratios {1, 1.73, 2.33, 3.91, 4.11, 6.30} on `fc = coinFundamental(...)`. Skip modes above `0.45·fs`.
- Mode i amplitude `U(0.3, 1) / (1 + 0.5 i)` with a random phase.
- `τ0 = 0.35 · (440/fc)^0.3`, mode τ = `τ0 / ratio^0.8`.
- Plus a 2 ms differentiated-noise click.
- Ring output is scaled by `0.35 · amp`.
- Use a two-pole resonator or a rotating damped phasor per mode. Coefficients are computed once in `hit()`.
- A ring frees itself once its envelope is below 1e-4 of its start.
- When all `maxRings` are busy, a new hit **replaces the quietest** ring.
- Rings overlap and are never choked: that overlap is the bell character the spec keeps.

- [ ] **Step 1: Write `tests/test_coin.cpp`** (48 kHz):
  - `fundamental_values`: `coinFundamental(82.4, 0) ≈ 329.6`, `(1046.5, 12) ≈ 4186` (folded from 8372), `(1500, 24) ≤ 8000`.
  - `rings_at_fc`: one hit at 220 Hz gives Goertzel energy over the first 200 ms at 880 Hz ≥ 10 × the energy at 932 Hz.
  - `decays`: RMS over 0–100 ms ≥ 3 × RMS over 400–500 ms.
  - `amp_scales`: a hit with `amp 0.1` has a peak in [0.02, 0.15].
  - `cap_and_steal`: 2000 hits spread over 1 s keep `activeRings() ≤ 32` throughout, and every sample is finite with |y| < 2.
  - `frees_itself`: 3 s after the last hit, `activeRings() == 0` (Review Focus 4).

- [ ] **Step 2: Run `make test` and confirm it fails**

- [ ] **Step 3: Implement `CoinSynth.hpp`**

- [ ] **Step 4: Run `make test`.** Expected: PASS.

- [ ] **Step 5: Commit** — `git commit -m "dsp: modal coin ring pool"`

---

### Task 6: Sampled-mode grains

**Files:**
- Create: `plugins/TossACoin/dsp/Grains.hpp`, `tests/test_grains.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class Grains {
    void prepare(double fs, int maxDrops, int maxPlayers);  // per-drop buffer of 0.120 s
    void reset();
    void clearDrop(int drop);                                // new drop in this slot; fades its players out over 64 samples
    void record(int drop, float x);                          // ignored once the buffer is full
    int  recorded(int drop) const;
    void lockLength(int drop, int samples);                  // grain length L = min(0.12 s, ½·longest gap, recorded)
    void hit(int drop, float semis, float amp, float maxLenSec);
    float process();
    int activePlayers() const;
  };
  ```

The grain is the drop's first `L` samples, with a 1 ms fade-in and an exponential decay of τ = L/4 applied on playback. `hit` resamples by `2^(semis/12)` with linear interpolation, plays at most `maxLenSec` (90% of the time to the next bounce, passed in by the engine), and ends with a 64-sample fade. When all players are busy, it replaces the oldest player.

- [ ] **Step 1: Write `tests/test_grains.cpp`:**
  - `octave_up_doubles_pitch`: record a 200 Hz sine for 0.12 s and hit with semis 12. The output's Goertzel energy at 400 Hz is ≥ 10 × the energy at 200 Hz, and its length is about L/2 (±64 samples).
  - `truncates`: `maxLenSec 0.01` gives no output after 480 + 64 samples.
  - `respects_recorded`: `lockLength` with only 0.05 s recorded never reads beyond sample 2400 (ASAN enforces this).
  - `clearDrop_fades`: no step larger than 0.05 in the output around `clearDrop`.
  - `frees_itself`: `activePlayers() == 0` after 1 s.

- [ ] **Step 2: Run `make test` and confirm it fails**

- [ ] **Step 3: Implement `Grains.hpp`**

- [ ] **Step 4: Run `make test`.** Expected: PASS.

- [ ] **Step 5: Commit** — `git commit -m "dsp: sampled-mode grains"`

---

### Task 7: Coin engine (drops lifecycle)

**Files:**
- Create: `plugins/TossACoin/dsp/CoinEngine.hpp`, `tests/test_engine.cpp`

**Interfaces:**
- Consumes: every unit from Tasks 2–6.
- Produces:
  ```cpp
  struct EngineParams { float thresholdDb, bounce, height, pitch, glitch; Steps steps; bool reverse, sampled; };
  class CoinEngine {
    static constexpr int kMaxDrops = 6;
    void prepare(double fs);
    void reset();
    void setParams(const EngineParams& p);   // per block; a drop keeps the params it started with
    void work(uint32_t frames);              // per block: detector.work(frames * 3 / 4)
    float process(float x);                  // per sample: wet signal (before Level / limit)
    int activeDrops() const; int activeRings() const;
  };
  ```

Lifecycle per drop slot (Free → Detecting → Bouncing → Free):
- On onset, take a free slot or steal the oldest. Stealing cancels its pending hits; its rings keep ringing in the shared pool.
- Store `attackPeak` = max |x| over the next 20 ms.
- Start the detector and record grains.
- When the detector is done, set `f0 = hz()`. If that is 0, use the last detected pitch, or 110 Hz if there has never been one (Claude's call).
- Build the schedule with `notBefore` = the elapsed time since the attack.
- In Sampled mode, call `grains.lockLength(drop, …)` with the longest gap.
- Fire each bounce when its time arrives:
  - Synth: `synth.hit(f0, semis, attackPeak · amp, rng)`.
  - Sampled: `grains.hit(drop, semis, amp, 0.9 · timeToNext)`.
- The slot is Free after its last bounce.
- Only one drop detects at a time. The 250 ms hold-off guarantees this.

- [ ] **Step 1: Write `tests/test_engine.cpp`** (48 kHz, 128-frame blocks, default params unless stated):
  - `soft_note_no_wet`: a `pluck(110, 0.0126)` gives wet exactly 0 at every sample.
  - `first_coin_after_T1`: `pluck(110, 0.1)` gives its first non-zero wet sample at 0.4327 s ± 4% (`T1 = 0.5·sqrt(82.4/110)`).
  - `reverse_starts_soon`: reverse gives its first wet sample within [0.04, 0.12] s.
  - `fallback_pitch`:
    - A hard noise burst first gives its first wet at `T1(110)` ± 4%.
    - After a `pluck(220)` drop has finished, a noise burst gives `T1(220) = 0.306 s` ± 4%.
  - `stress_synth` and `stress_sampled`: plucks at random pitches 41–1500 Hz, peak 0.1, every 250 ms for 10 s, with bounce 0.97. `activeDrops() ≤ 6`, `activeRings() ≤ 32`, every wet sample finite, peak |wet| < 1.
  - `knobs_mid_drop` (Review Focus 3): during `stress_synth`, randomise all params every block. All samples are finite and no sample-to-sample step exceeds 0.5.
  - `silence_frees_everything` (Review Focus 4): 3 s after the last drop, `activeRings() == 0` and `activeDrops() == 0`.
  - `other_rates` (Review Focus 1): at 44.1 kHz and at 96 kHz, `first_coin_after_T1` holds.

- [ ] **Step 2: Run `make test` and confirm it fails**

- [ ] **Step 3: Implement `CoinEngine.hpp`**

- [ ] **Step 4: Run `make test`.** Expected: PASS.

- [ ] **Step 5: Commit** — `git commit -m "dsp: coin engine wiring onset, pitch, schedule and voices"`

---

### Task 8: Plugin glue: parameters, groups, level, soft limit

**Files:**
- Modify: `plugins/TossACoin/TossACoinPlugin.cpp`, `tests/test_bypass.cpp`
- Create: `tests/test_plugin.cpp`

**Interfaces:**
- Consumes: `CoinEngine`, `EngineParams`, `sensitivityToThresholdDb`
- Produces the parameter enum below, in this order, followed by `kParamBypass` and `kNumParams`. Plugin-side indices start at 0.

| enum | symbol | name | unit | min | max | def | hints |
|---|---|---|---|---|---|---|---|
| kParamSensitivity | `sensitivity` | Sensitivity | % | 0 | 100 | 50 | automatable |
| kParamBounce | `bounce` | Bounce | % | 70 | 97 | 92 | automatable |
| kParamHeight | `height` | Height | s | 0.1 | 1.5 | 0.5 | automatable |
| kParamPitch | `pitch` | Pitch | st | −24 | 24 | 12 | automatable |
| kParamSteps | `steps` | Steps | | 0 | 3 | 1 | integer, enum Glide / Semitones / Fifths & octaves / Octaves (restricted) |
| kParamGlitch | `glitch` | Glitch | % | 0 | 100 | 0 | automatable |
| kParamReverse | `reverse` | Reverse | | 0 | 1 | 0 | boolean (latch footswitch) |
| kParamSound | `sound` | Sound | | 0 | 1 | 0 | integer, enum Synth / Sampled (restricted) |
| kParamLevel | `level` | Level | dB | −24 | 6 | 0 | automatable |

- Port groups (`INSTRUCTIONS.md`, "Port groups"): `g1_trigger` Trigger {sensitivity}, `g2_drop` Drop {bounce, height, reverse}, `g3_pitch` Pitch {pitch, steps, glitch}, `g4_output` Output {sound, level}.
- `run()`:
  - Call `setParams` and `work(frames)` once per block. `EngineParams` takes fractions (bounce 0.92, glitch 0–1), so convert the % parameters there.
  - Per sample: `wet = engine.process(in)`, then `y = in + levelLin · softLimit(wet)`, crossfaded against `in` by the template's bypass ramp.
  - Write `y` to both outputs.
  - `softLimit` is the soft-limit pattern from `INSTRUCTIONS.md`, ceiling 1.0.
- `getDescription()`: "Every hard attack drops a coin: it bounces faster and faster, rings like a small bell and steps in pitch. The note you play sets the size of the coin."

- [ ] **Step 1: Adapt `tests/test_bypass.cpp`**
  - Use `kParamLevel = 8` and `kParamBypass = 9`.
  - Replace the gain-specific "+6 dB within 10 ms" assertion with "output equals input exactly until the first coin (sample < 0.40 s)".
  - Keep the dry-at-unity, no-ramp-on-load and ±3 dB default-loudness checks. If default loudness fails, lower the default Level rather than widening the window.

- [ ] **Step 2: Write `tests/test_plugin.cpp`** (through `PluginExporter`):
  - `soft_notes_are_untouched`: a `pluck(110, 0.0126)` gives L == R == input at every sample.
  - `hard_note_adds_coins`: `pluck(110, 0.1)` gives output == input before 0.40 s, and RMS(out − in) > 0 in the 0.40–0.60 s window.
  - `block_sizes` (Review Focus 2): with blocks of 1, 37 and 1024 frames, the first coin lands at the same time ± one block.
  - `hot_input_bounded` (Review Focus 5): plucks with peak 1.0 every 250 ms, Bounce 97, Level +6 give |out| ≤ 1.0 + 2.0 (dry peak + limited wet × 2) at every sample, all finite.
  - `enum_labels`: Steps has 4 values and Sound has 2, labelled as in the table.

- [ ] **Step 3: Run `make test` and confirm the new tests fail**

- [ ] **Step 4: Implement the glue in `TossACoinPlugin.cpp`**

- [ ] **Step 5: Run `make test && make`.** Expected:
  - all tests pass;
  - `grep -o 'pg:group <[^>]*>' bin/tossacoin.lv2/tossacoin.ttl | sort | uniq -c` shows 4 groups with 1/3/3/2 members.

- [ ] **Step 6: Desktop listen check**

Run `./install.sh`, restart MOD Desktop, play the prototype's test clip through it, and compare by ear with `docs/superpowers/specs/2026-11-02-tossacoin-prototype.py` output (render it with the samples, as in the spec). Note anything that sounds different from the prototype for Task 10.

- [ ] **Step 7: Commit** — `git commit -m "plugin: parameters, port groups, level and soft limit"`

---

### Task 9: Dwarf CPU benchmark and voice caps

**Files:**
- Create: `tests/bench_dwarf.cpp`
- Modify: `Makefile` (add a `dwarf-bench` target), `plugins/TossACoin/dsp/CoinEngine.hpp` (final `maxRings` / `kMaxDrops`)

**Interfaces:**
- Consumes: the plugin through `PluginExporter` (same harness as the tests).

`bench_dwarf.cpp` runs at 48 kHz with 128-frame blocks:
- Input: 20 s of `pluck` at random pitches 41–1500 Hz, peak 0.1, every 250 ms. Settings: Bounce 97, Height 1.5, Glide, Glitch 50.
- It runs once in Synth mode and once in Sampled mode, timing every `run()` with `clock_gettime(CLOCK_MONOTONIC)`.
- It prints the average and worst block as % of 2.667 ms, and exits nonzero if the average is above 20% or the worst block is above 50%.

`make dwarf-bench` compiles it inside the `moddwarf-cross` image with the same compiler as `mod-build/build-plugin.sh`:
- Compiler `/root/mod-workdir/moddwarf-new/host/usr/bin/aarch64-modaudio-linux-gnu-g++`, flags `-O3 -ffast-math -std=gnu++14 -DNDEBUG -I$(PLUGIN_DIR) -Idpf/distrho`, sources `tests/bench_dwarf.cpp $(PLUGIN_DIR)/*.cpp dpf/distrho/src/DistrhoPlugin.cpp`.
- Output `build/bench/bench_dwarf`, then `scp -O` to `root@$(DWARF_HOST):/tmp/` and `ssh` to run it.

- [ ] **Step 1: Confirm the Task 1 `make dwarf-image` finished:** `docker image inspect moddwarf-cross` succeeds

- [ ] **Step 2: Write `bench_dwarf.cpp` and the `dwarf-bench` target**

- [ ] **Step 3: Run `make dwarf-bench`** and record the numbers

- [ ] **Step 4: Tune the caps** until the targets hold in both modes:
  - lower `maxRings` (32 → 24 → 16) and/or `kMaxDrops`;
  - if that is not enough, switch the resonators to the fixed-point and recursive techniques in `INSTRUCTIONS.md`.

  Update the `≤ 32` asserts in `test_coin.cpp` and `test_engine.cpp` to the final value.

- [ ] **Step 5: Run `make test && make dwarf-bench`.** Expected: both pass. Then `make dwarf` deploys the plugin, and it appears in the Dwarf's plugin list after a hard browser refresh.

- [ ] **Step 6: Commit** — `git commit -m "Dwarf CPU benchmark; set voice caps"`

---

### Task 10: Sound tuning with Stefan on the Dwarf (human checkpoint)

**Files:**
- Modify: defaults and constants in `TossACoinPlugin.cpp` and `dsp/*.hpp`, and the tests that pin them

- [ ] **Step 1: Stefan plays the deployed build** (hard and soft, low and high, guitar, and bass if he uses it). Adjust live in MOD-UI:
  - the Sensitivity default, so soft playing never triggers and normal hard picking does;
  - the Level default;
  - synth brightness and ring length against the prototype's `9-synth-*` / `8-synth-bounce-92` sound.
- [ ] **Step 2: Bake the agreed values in.** Update the affected tests and run `make test && make dwarf-bench`. Expected: PASS.
- [ ] **Step 3: Commit** — `git commit -m "Tune defaults on the Dwarf with Stefan"`

---

### Task 11: Minstrel-themed modgui

**Files:**
- Modify: `plugins/TossACoin/modgui/{icon-tossacoin.html,stylesheet-tossacoin.css,script-tossacoin.js,render_screenshot.py}`, `plugins/TossACoin/modgui.ttl`, `Makefile` (`modgui` target also copies `modgui/fonts/`)
- Create: `plugins/TossACoin/modgui/knobs/make_coin_knob.py`, `plugins/TossACoin/modgui/knobs/coin.png`, `plugins/TossACoin/modgui/fonts/` (an OFL blackletter or uncial font, licence file included), `screenshot-tossacoin.png`, `thumbnail-tossacoin.png`

- [ ] **Step 1: Mockups.** Render 3 pedal variants (static HTML using the real modgui CSS approach, ≤ 680 px wide) with `render_screenshot.py`:
  - lute-wood face;
  - leather coin purse;
  - parchment-on-wood.

  Each has gold-coin knobs, a parchment title "Toss a Coin" and a feathered-cap motif. Set up Playwright with `uv run --with playwright python -m playwright install chromium`, then `uv run --with playwright python render_screenshot.py`. **Show Stefan the PNGs and wait for his pick.**
- [ ] **Step 2: Coin knob sprite.** `make_coin_knob.py` (Pillow) writes a horizontal film strip of 65 square 128 px frames: a gold coin with a notch rotating −135° → +135°. Wire it up as described in `INSTRUCTIONS.md`, "Custom pedal frames MUST ship their own knob sprite".
- [ ] **Step 3: Implement the chosen design.**
  - 6 knobs: sensitivity, bounce, height, pitch, glitch, level.
  - 3 custom `<select>`s bound via `mod-role` (sitar pattern, `~/Fun/sitar/plugins/Sitar/modgui/`): Steps, Sound, Reverse (Off/On).
  - The required structural elements from `INSTRUCTIONS.md`.
  - `{{{ns}}}` on every resource URL.
  - List every port symbol in `modgui.ttl`.
- [ ] **Step 4: Screenshot and thumbnail.** Regenerate them with `render_screenshot.py` at 2×. The thumbnail size follows the template's thumbnail.
- [ ] **Step 5: Verify.**
  - Run `make && ./install.sh`, then the scanner check from `INSTRUCTIONS.md` ("Reproduce a scanner crash without the GUI"). Expected: "scan OK".
  - MOD Desktop shows the pedal: knobs turn, selects change the ports, and there are no "No such symbol" errors.
  - `make dwarf`, then Stefan checks it on the device.
- [ ] **Step 6: Commit** — `git commit -m "modgui: minstrel-themed pedal with coin knobs"`

---

### Task 12: Manual and README

**Files:**
- Modify: `docs/manual/tossacoin-manual.html`, `README.md`
- Create: `docs/manual/tossacoin-manual.pdf` (generated)

- [ ] **Step 1: Write the manual** using the template demo's structure and the rules in `INSTRUCTIONS.md`, "User manual (PDF)":
  - Sections: cover → what it does → quick start → controls (one card per control, described by ear, numbered pins on the screenshot) → recipes → tips → installing → Q&A → specifications.
  - Recipes: "Penny drop" (defaults), "Gold rush" (Fifths & octaves, Pitch +19), "Cursed coin" (Glitch 40%), "Rewind" (Reverse).
  - No franchise names or art.
- [ ] **Step 2: Run `make manual`.** Expected: `docs/manual/tossacoin-manual.pdf` is A4 with no blank page 2.
- [ ] **Step 3: Write the README** in the Boreas style: screenshot, one-paragraph pitch, install per platform, manual link, building link. One line may note that the name nods to the bard's song.
- [ ] **Step 4: Commit** — `git commit -m "docs: manual and README"`

---

### Task 13: Release and challenge entry

**Files:**
- Modify: `VERSION` (via `make release`)

- [ ] **Step 1: Create the GitHub repo. Ask Stefan before running it, because it's public and outward-facing:** `gh repo create stefdoerr/tossacoin --public --source . --push`
- [ ] **Step 2: Build the Patchstorage bundles:** `make patchstorage-build`. This pulls the three builder images the first time. Expected: three `tossacoin.lv2` bundles under `build/patchstorage/`.
- [ ] **Step 3: Release (confirm with Stefan first):** `make release version=0.1.0`. Expected:
  - the GitHub release has 4 tarballs and the manual;
  - the desktop CI workflow then adds VST3/CLAP (`gh run watch`).
- [ ] **Step 4: Patchstorage.** Stefan runs `! make patchstorage PS_USER=<his username>`, because the password is prompted interactively.
- [ ] **Step 5: Hand over for the entry.** Give Stefan the repo link, a two-line description for the entry, and a suggested 60-second clip outline: dry phrase, then defaults, Fifths & octaves, Reverse, Glitch. He records the clip and posts it in the November Entries thread before 1 December.
