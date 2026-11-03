# Toss a Coin — design

**Date:** 2026-11-02
**Status:** Approved by Stefan (2026-11-03)
**Repo:** `~/Fun/tossacoin` → `github.com/stefdoerr/tossacoin`, forked from
`mod-plugin-template` the same way Boreas was

## Why

This is an entry for the MOD community challenge *"Create an effect that uses or
affects pitch in an unexpected way."* Entries close on 1 December 2026. An entry is a
plugin link (we'll use the GitHub repo), a clip of up to 60 s with the dry signal
first and then the wet, and one or two lines about the idea. Stefan records the clip.
After the challenge it should be a real released plugin like Boreas.

**The idea:** each hard attack drops a coin on the floor. The pitch of the note you
played sets the size of the coin. A low note is a fat coin whose bounces start slow,
and a high note is a small coin that clatters quickly. The bounces speed up like a
real dropped coin, ring like a small bell, and step in pitch as they go. Pitch
controls rhythm, which is the "unexpected" angle the challenge asks for.

## What you hear (decided)

- **The dry signal passes through untouched.** The coins are added on top.
- **Only hard attacks drop a coin.** Soft playing passes through clean. A Sensitivity
  control sets the threshold.
- **The attack is the coin's first impact.** Bounces follow it.
- **Pitch sets the first gap.** Low notes start slow and high notes start quick.
- **Each gap is a fixed fraction of the previous one** (geometric), so the bounces
  pile up and stop. The default fraction is **92%**, which gives a gradual speed-up.
  Stefan picked it by ear over 72/80/86%. It's adjustable.
- **The pitch moves over the drop, up or down.** The note set it steps through is
  selectable. There is no key or scale selection: the pedal can't know the key, so the
  steps are relative to the played note (semitones, fifths and octaves, octaves).
- **Reverse** flips the timing: the bounces start fast and loud, then slow down and
  fade out.
- **Two sounds:**
  - **Synth** (the default) is a tuned metal plate with a **bell-like ring that is
    kept**. Stefan likes it, so successive rings overlap rather than choking each
    other.
  - **Sampled** reuses the player's own attack, chopped and repitched.
- **Glitch** is an option and off by default. Each bounce has a chance to jump to a
  random note. Some jumps snap back on the next bounce and some carry on from the new
  note.

## Controls

"Stefan" means he asked for it. "Claude" means it's my call and he can veto it.

| Control | Range | Default | Behaviour | Source |
|---|---|---|---|---|
| Sensitivity | builder's call (prototype: −24 dBFS) | tuned by ear | Attack threshold. Soft notes must not trigger. | Stefan (idea), Claude (control) |
| Bounce | 70–97 % | **92 %** | Each gap as a fraction of the previous one. | Stefan |
| Height | ~0.1–1.5 s | 0.5 s | First gap for E2 (82.4 Hz). Other notes scale from it as described below. | Claude |
| Pitch | −24…+24 semitones | +12 | Total pitch travel over one drop. The sign sets the direction. | Stefan (direction), Claude (one bipolar knob) |
| Steps | Glide / Semitones / Fifths & octaves / Octaves | Semitones | Snaps the pitch path to allowed offsets from the played note. | Stefan (5ths & octaves), Claude (other options) |
| Glitch | 0–100 % | **0 % (off)** | Chance per bounce of a random jump. | Stefan |
| Reverse | off / on | off | Fast to slow, fading. | Stefan |
| Sound | Synth / Sampled | Synth | Coin sound source. | Stefan |
| Level | builder's call | ~0 dB | Coin volume. The dry signal always stays at unity. | Claude |
| Bypass | — | — | The template's click-free bypass. | template |

Port order is free until the first release and append-only after it, following the
template rule.

## Sound engine (reference values from the prototype)

The prototype `2026-11-02-tossacoin-prototype.py` (offline Python, next to this file)
produced the sounds Stefan approved. It is the **reference for how it should sound**,
not code to port line by line. Its numbers:

**Trigger.** Run one-pole envelopes on |x|: fast (2 ms) and slow (100 ms). A coin
drops on the rising edge where `fast > threshold` and `fast > 2.5 × slow`, with a
250 ms hold-off. The real-time version may differ but must keep the behaviour: hard
attacks trigger, soft notes don't, and a note's own sustain never re-triggers. The
prototype's test clip is about 15–20 dB hotter than a real direct input, so its
−24 dBFS threshold does **not** carry over. Set the default Sensitivity for real DI
levels (hard-pick peaks around −20 dBFS) and retune it on Stefan's own playing.

**Pitch detection.** The prototype runs YIN on a ~46 ms window starting 20 ms after
the onset. The plugin should cover **40 Hz–1.5 kHz** so that bass guitar works too
(Claude's call; the prototype used 60 Hz). The first bounce must never fire before the
pitch is known. Clamp the first gap to the detection latency if needed. This matters
most in **Reverse**, whose opening gaps are 8–15 ms. The drop simply starts once the
pitch is in, up to about 70 ms after the attack for the lowest notes and sooner for
high ones. The ringing dry note covers that gap. There is no provisional pitch and no
later correction (Claude's call). In Sampled mode the grain can't be longer than what
has been recorded by the time the first bounce fires. If no pitch is
found (muted scratch, messy chord), use the last detected pitch (Claude's call). Chords
just yield whatever single pitch the detector finds.

**Schedule (computed once per drop).**
- `T1 = Height × sqrt(82.4 Hz / f0)`. With Height at 0.5 s, E2 gives 0.5 s and C6
  gives 0.14 s.
- Gap `k` is `T1 × Bounce^k`. Jitter **each gap** independently by ±3%.
  - Never jitter the cumulative time. That was a prototype bug that made the bounces
    clump into audible pairs.
- Bounce amplitude `k` is `Bounce^(k+1) × U(0.8, 1.1)`.
- The drop ends when a gap falls below 8 ms or the amplitude falls below about
  −60 dB. There is no fixed bounce cap. The prototype's cap of 40 audibly cut off low
  notes at 92%: E2 at 92% runs about 50 bounces over about 6 s.
- **Reverse** plays the gap list backwards, with amplitude `Bounce^((k+1)/2)` so the
  slow end is still audible. Tune it by ear.

**Pitch path.**
1. Bounce `k` (1-based, `n` bounces in the drop) starts with a semitone offset of
   `Pitch × k / n`.
2. Glitch: with probability `Glitch`, add a jump of `U(−12, +12)` semitones. Half the
   jumps apply only to that bounce. The other half shift all following bounces.
3. Snap the result to the nearest allowed offset, mirrored for negative values:
   - Semitones: integers.
   - Fifths & octaves: {0, 7, 12, 19, 24, 31, 36}.
   - Octaves: {0, 12, 24, 36}.
   - Glide: no snap.

**Synth coin (one hit).**
- Modal "free circular plate". Mode ratios are 1, 1.73, 2.33, 3.91, 4.11, 6.30, on a
  fundamental `fc = f0 × 2^(offset/12) × 4` (two octaves above the note).
- If `fc` is above 8 kHz, drop it by octaves until it isn't. Without this, high notes
  that climb become nearly inaudible (Claude's call).
- Skip modes above 0.45 · fs.
- Mode `i` has amplitude `U(0.3, 1) / (1 + 0.5 i)` and a random phase. Randomizing
  per hit varies the impact point, which keeps the hits from sounding robotic.
- Decay is `τ0 = 0.35 s × (440 / fc)^0.3` and per mode `τ0 / ratio^0.8`.
- Each hit also gets a 2 ms differentiated-noise click.
- Rings overlap freely. That overlap is the bell character.

**Sampled coin (one hit).**
- The grain is the first `min(120 ms, ½ × longest gap)` of the input after the onset,
  with a 1 ms fade-in and an exponential decay (τ = length / 4).
- Each bounce resamples the grain by `2^(offset/12)`, so it gets higher and shorter,
  and truncates it at 90% of the time to the next bounce with a short fade.

**Mixing.** Output is `dry + Level × coins`, mono in and stereo out with the same
signal on both sides (the template layout). A new hard attack starts a new drop while
older drops keep bouncing.

## Look

The name nods to the bard's song "Toss a coin to your Witcher", and Stefan wants the
pedal themed after that bard. The look is a **travelling minstrel**:
- worn lute wood or a leather coin purse for the pedal face,
- gold coins as knobs or accents,
- a parchment label with a calligraphic or blackletter-style "Toss a Coin",
- a feathered-cap motif.

Keep it a **nod, not a copy**, because the plugin is published on GitHub and
Patchstorage. Use no franchise or character names, logos, wolf medallion, or art or
screenshots from the show or games, on the pedal or in the manual. The README may say
which song the name nods to. Claude renders mockups (Boreas's `render_screenshot.py`
renders the modgui) and Stefan picks one.

## Goals

1. **Everything is done by 1 December 2026, the full plugin and the full treatment**
   (Stefan). Order of work (Claude's call):
   - First, a playable Dwarf build, so the CPU risk is retired early and Stefan can
     record his clip.
   - Then sound tuning with Stefan.
   - Then artwork, manual and release.
2. **The full treatment, as with Boreas:**
   - modgui pedal artwork with a screenshot and thumbnail.
   - A beginner PDF manual.
   - A README.
   - `make test` DSP tests.
   - GitHub releases: Dwarf, the three Patchstorage LV2 targets, and desktop
     VST3/CLAP via CI.
   - Publishing on Patchstorage.

## Non-goals

- Key or scale selection.
- Polyphonic or chord pitch detection.
- MIDI in or out.
- Tempo sync.
- Recording the challenge clip (Stefan does that).

## Anti-goals (a failure even if it technically works)

- Coins on every note, so that normal playing becomes a mess.
- Clicks, pops or xruns on the Dwarf, including when many drops overlap.
- Coins pitched so high they're inaudible.
- Mechanical, identical hits. Keep the jitter.
- Bounces that clump or sound "in pairs" instead of like a falling object.

## Constraints

- DPF via `mod-plugin-template`. Follow its `INSTRUCTIONS.md`, especially "precompute
  everything" and fixed-point phase for oscillator banks.
- Identity:
  - `PLUGIN := tossacoin`, `PLUGIN_DIR := plugins/TossACoin`.
  - `BRAND := Stefan`, `LABEL := Toss a Coin`.
  - `PLUGIN_URI_BASE := http://tossacoin.local/plugins`.
  - Homepage `https://github.com/stefdoerr/tossacoin`.
- Real-time safe: no allocation, locks or transcendental setup in `run()`.
- **CPU budget on the Dwarf.** At 92% bounce, a drop has about 50 hits over about
  6 s, each ringing for up to about 1 s, and several drops can overlap. Boreas topped
  out at about 96 oscillators on the Dwarf. The builder must bound the cost and test
  worst-case overlap on the device. One default E2 drop alone has about 29 rings
  overlapping in its last second (about 170 plate modes if each hit rings on its own).
  **Worst case to survive** (Claude's call): hard strums every 250 ms at Bounce 97%,
  while leaving room for an amp sim, cab IR and reverb in the same pedalboard. Steal
  the quietest rings first; they're the least audible, so the overlapping bell ring
  survives. Options include capping concurrent drops or rings
  and stealing the quietest or oldest with a short fade, using recursive two-pole
  resonators, or re-exciting an existing resonator bank when the pitch hasn't changed.

- **On-device testing.**
  - Stefan's Dwarf is connected over USB at `192.168.51.1` (4× Cortex-A35). The
    builder deploys with `make dwarf` and measures CPU on the device directly.
  - The `moddwarf-cross` Docker image is **not built on this machine yet**. The first
    `make dwarf-image` takes 30–60 min, once. The Patchstorage builder images also
    need pulling before release.
  - No DI recording yet. Tune Sensitivity and Level live on the Dwarf with Stefan
    playing.

## Left to the builder

- Onset and pitch-detector implementation and tuning. Keep the latency hidden behind
  the first gap.
- Voice and ring caps, and the stealing policy.
- Exact control ranges, curves and the Sensitivity scale.
- Whether synth level follows attack strength.
- The LV2 category (`lv2:PitchPlugin` seems right for the challenge).
- 4-character IDs.
- How the repo is created from the template.
- Artwork details within the "Look" section, and manual content. Stefan reviews both.

## Reference material

- `2026-11-02-tossacoin-prototype.py` — the offline prototype. Run it with
  `python3 -I <script> <samples_dir> <out_dir>`.
- Test samples: tonejs-instruments guitar-electric / guitar-acoustic WAVs, CC-BY 3.0
  (`github.com/nbrosowsky/tonejs-instruments`), used for testing only.
- Boreas (`~/Fun/boreas`) as the worked example of the full treatment.
