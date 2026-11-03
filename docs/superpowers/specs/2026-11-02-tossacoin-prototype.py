"""THROWAWAY SPIKE: coin-drop effect, offline. Renders WAVs to compare by ear.

python3 -I coindrop.py <samples_dir> <out_dir>
"""
import sys, os
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter

FS = 44100
rng = np.random.default_rng(1)


def load(path):
    fs, x = wavfile.read(path)
    assert fs == FS, (path, fs)
    x = x.astype(np.float32) / 32768.0
    return x if x.ndim == 1 else x.mean(axis=1)


def build_clip(sdir, gap_s=3.0):
    # (file, hard?) — soft notes are -18 dB and must NOT trigger
    seq = [("electric/E2", 1), ("electric/A3", 0), ("electric/C4", 1), ("electric/A4", 0),
           ("electric/A5", 1), ("electric/C6", 1), ("acoustic/E2", 1), ("acoustic/E4", 0),
           ("acoustic/A4", 1)]
    gap = int(gap_s * FS)
    out = np.zeros(gap * len(seq) + FS, np.float32)
    for i, (name, hard) in enumerate(seq):
        x = load(os.path.join(sdir, name + ".wav"))[:gap]
        x = x / np.sqrt(np.mean(x[:int(0.3 * FS)] ** 2)) * (0.2 if hard else 0.2 * 10 ** (-18 / 20))
        x[-2000:] *= np.linspace(1, 0, 2000)
        o = FS // 2 + i * gap
        out[o:o + len(x)] += x
    return out


def onepole(x, sec):
    a = np.exp(-1.0 / (sec * FS))
    return lfilter([1 - a], [1, -a], x)


def onsets(x, thr_db=-24, ratio=2.5, holdoff=0.25):
    ax = np.abs(x)
    fast, slow = onepole(ax, 0.002), onepole(ax, 0.1)
    hit = (fast > 10 ** (thr_db / 20)) & (fast > ratio * slow)
    res, last = [], -1e9
    for i in np.flatnonzero(hit[1:] & ~hit[:-1]) + 1:
        if i - last > holdoff * FS:
            res.append(i); last = i
    return res


def yin(seg, fmin=60, fmax=1400, thr=0.15):
    W, tmax, tmin = 1024, int(FS / fmin), int(FS / fmax)
    d = np.array([np.sum((seg[:W] - seg[t:t + W]) ** 2) for t in range(tmax + 2)])
    c = d[1:] * np.arange(1, len(d)) / np.maximum(np.cumsum(d[1:]), 1e-12)
    c = np.concatenate([[1.0], c])
    tau = None
    for t in range(tmin, tmax):
        if c[t] < thr and c[t] <= c[t + 1]:
            tau = t; break
    if tau is None:
        tau = tmin + int(np.argmin(c[tmin:tmax]))
    a, b, cc = c[tau - 1], c[tau], c[tau + 1]
    den = a - 2 * b + cc
    return FS / (tau + (0.5 * (a - cc) / den if den else 0.0))


NOTES = "C C# D D# E F F# G G# A A# B".split()
def name(f):
    m = int(round(69 + 12 * np.log2(f / 440)))
    return f"{NOTES[m % 12]}{m // 12 - 1}"


def schedule(f0, e=0.72, step=1.0, direction=1, reverse=False, travel=None, allowed=None, glitch=0.0):
    """Bounce times (s after attack), amplitudes, pitch ratios.
    Low note -> slow first bounce, high note -> fast (sqrt mapping, E2 = 0.5 s).
    Gaps shrink geometrically by e each bounce until < 8 ms."""
    T1 = 0.5 * (82.4 / f0) ** 0.5
    gaps = []
    g = T1
    while g > 0.008 and len(gaps) < 40:
        gaps.append(g); g *= e
    n = len(gaps)
    if reverse:
        gaps = gaps[::-1]
        amps = [e ** (0.5 * (k + 1)) for k in range(n)]   # fast+loud -> slow+fading
    else:
        amps = [e ** (k + 1) for k in range(n)]
    amps = [a * rng.uniform(0.8, 1.1) for a in amps]       # coins aren't perfect
    times = np.cumsum(np.array(gaps) * rng.uniform(0.97, 1.03, n))   # jitter each gap, not absolute time
    semis = [direction * step * (k + 1) for k in range(n)]
    if travel is not None:                       # spread total travel over the drop
        semis = [travel * (k + 1) / n for k in range(n)]
    if glitch:                                   # chance per bounce to jump up to +-1 octave;
        off = 0.0                                # half the jumps snap back, half carry on from there
        for k in range(n):
            if rng.uniform() < glitch:
                j = rng.uniform(-12, 12)
                if rng.uniform() < 0.5:
                    semis[k] += off + j; continue
                off += j
            semis[k] += off
    if allowed:                                  # snap to allowed notes (mirrored for downward)
        grid = sorted(set(allowed) | {-a for a in allowed})
        semis = [min(grid, key=lambda a: abs(a - s)) for s in semis]
    ratios = [2 ** (s / 12) for s in semis]
    return times, amps, ratios, gaps


def sampled_hits(x, on, f0, times, amps, ratios, gaps):
    L = int(min(0.12, 0.5 * max(gaps)) * FS)
    g = x[on:on + L].astype(np.float64)
    env = np.exp(-np.arange(L) / (L / 4)); env[:44] *= np.linspace(0, 1, 44)
    g *= env
    out = []
    for k, (t, a, r) in enumerate(zip(times, amps, ratios)):
        pos = np.arange(0, L - 1, r)
        h = np.interp(pos, np.arange(L), g)
        nxt = gaps[k + 1] if k + 1 < len(gaps) else 0.2
        h = h[:max(64, int(0.9 * nxt * FS))].copy()
        h[-64:] *= np.linspace(1, 0, 64)
        out.append((t, a * h))
    return out


PLATE = [1.0, 1.73, 2.33, 3.91, 4.11, 6.30]   # free circular plate mode ratios


def coin_hit(f, amp, octave=2):
    f = f * 2 ** octave
    tau0 = 0.35 * (440 / f) ** 0.3
    n = int(min(1.0, 5 * tau0) * FS)
    t = np.arange(n) / FS
    y = np.zeros(n)
    for i, r in enumerate(PLATE):
        fr = f * r
        if fr > 0.45 * FS:
            continue
        y += rng.uniform(0.3, 1.0) / (1 + 0.5 * i) * np.sin(2 * np.pi * fr * t + rng.uniform(0, 6.28)) \
             * np.exp(-t / (tau0 / r ** 0.8))
    m = int(0.002 * FS)
    click = np.diff(rng.normal(size=m + 1)) * np.exp(-np.arange(m) / (m / 4))
    y[:m] += click
    return amp * 0.35 * y


def render(x, mode, direction=1, reverse=False, wet=0.8, **kw):
    out = np.concatenate([x, np.zeros(3 * FS)]).astype(np.float64)
    for on in onsets(x):
        f0 = yin(x[on + int(0.02 * FS):on + int(0.02 * FS) + 2048])
        times, amps, ratios, gaps = schedule(f0, direction=direction, reverse=reverse, **kw)
        if mode == "sampled":
            hits = sampled_hits(x, on, f0, times, amps, ratios, gaps)
        else:
            hits = [(t, coin_hit(f0 * r, a)) for t, a, r in zip(times, amps, ratios)]
        for t, h in hits:
            s = on + int(t * FS)
            out[s:s + len(h)] += wet * h[:len(out) - s]
        if mode == "sampled" and direction == 1 and not reverse:
            print(f"  onset {on / FS:5.2f}s  {f0:7.1f} Hz ({name(f0)})  first gap {gaps[0] * 1000:4.0f} ms  {len(gaps)} bounces")
    return out


def save(path, y):
    y = y / max(1e-9, np.abs(y).max()) * 0.89
    wavfile.write(path, FS, (y * 32767).astype(np.int16))
    print("wrote", path)


if __name__ == "__main__":
    sdir, odir = sys.argv[1], sys.argv[2]
    os.makedirs(odir, exist_ok=True)
    x = build_clip(sdir)
    save(f"{odir}/0-dry.wav", x)
    for mode in ("sampled", "synth"):
        save(f"{odir}/1-{mode}-up.wav", render(x, mode))
        save(f"{odir}/2-{mode}-down.wav", render(x, mode, direction=-1))
        save(f"{odir}/3-{mode}-reverse.wav", render(x, mode, reverse=True))
    FO = [0, 7, 12, 19, 24]
    save(f"{odir}/4-synth-fifths-up-19.wav", render(x, "synth", travel=19, allowed=FO))
    save(f"{odir}/5-synth-octaves-up-24.wav", render(x, "synth", travel=24, allowed=[0, 12, 24]))
    save(f"{odir}/6-synth-fifths-down-19.wav", render(x, "synth", travel=-19, allowed=FO))
    save(f"{odir}/7-sampled-fifths-up-19.wav", render(x, "sampled", travel=19, allowed=FO))
    x6 = build_clip(sdir, gap_s=6.0)
    save(f"{odir}/8-dry-6s.wav", x6)
    for e in (0.80, 0.86, 0.92):
        save(f"{odir}/8-synth-bounce-{int(e * 100)}.wav", render(x6, "synth", e=e, travel=12, allowed=list(range(13))))
    save(f"{odir}/9-synth-glitch-semitones.wav", render(x6, "synth", e=0.92, travel=12, allowed=list(range(37)), glitch=0.2))
    save(f"{odir}/9-synth-glitch-fifths.wav", render(x6, "synth", e=0.92, travel=12, allowed=FO + [31, 36], glitch=0.2))
