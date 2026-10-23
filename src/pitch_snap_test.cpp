// Pitch Snap (our addition): Transpose + Fine settle on octaves and fifths (Steps) or whole semitones (Semitones).
//  1. the snapping function: exact over the whole range; Off returns its input unchanged
//  2. through the engine: with Pitch Snap on, the sound is bit for bit the plain Transpose setting at the snapped value, and
//     differs from the unsnapped one (so the pitch is exactly what that setting always gave)
//  3. the pitch itself, measured on the string model: it lands on the snapped note
//  4. every model stays finite and audible in all three snap modes while Transpose and Fine sweep
// It says nothing about how anything sounds.
#include "ElementsEngine.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <functional>

using E = ElementsEngine;

// Renders `seconds` of notes (a gated note every half second, cycling pitch). `params` is called before every block.
// Returns the mean of L and R.
static std::vector<float> render (double sr, double seconds, const std::function<E::Params (int block)>& params)
{
    E e; e.prepare (sr);
    const int N = 512;
    std::vector<float> in (N, 0.0f), oL (N), oR (N), note (N, 57.0f), vel (N, 0.8f), all;
    std::vector<uint8_t> gate (N, 0), retrig (N, 0);
    const int blocks = (int) (sr * seconds / N);
    const long period = (long) (sr * 0.5);
    long count = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < N; ++i, ++count)
        {
            gate[(size_t) i] = (count % period) < (long) (period * 0.6) ? 1 : 0;
            note[(size_t) i] = 48.0f + (float) ((count / period) % 4) * 3.0f;
            vel[(size_t) i] = 0.3f + 0.2f * (float) ((count / period) % 4);
        }
        E::Control c; c.gate = gate.data(); c.retrig = retrig.data(); c.note = note.data(); c.vel = vel.data();
        e.process (in.data(), in.data(), oL.data(), oR.data(), N, params (b), c);
        for (int i = 0; i < N; ++i) all.push_back (0.5f * (oL[(size_t) i] + oR[(size_t) i]));
    }
    return all;
}

// Fundamental frequency by autocorrelation over a window (the same method engine_test uses).
static double pitchOf (const std::vector<float>& x, double sr, size_t from, size_t len, double lo, double hi)
{
    size_t minLag = (size_t) (sr / hi), maxLag = (size_t) (sr / lo);
    double best = -1e30; size_t bestLag = 0;
    for (size_t lag = minLag; lag <= maxLag; ++lag)
    {
        double acc = 0, e0 = 0, e1 = 0;
        for (size_t i = 0; i < len; ++i) { const double a = x[from + i], b = x[from + i + lag]; acc += a * b; e0 += a * a; e1 += b * b; }
        const double norm = acc / (std::sqrt (e0 * e1) + 1e-12);
        if (norm > best) { best = norm; bestLag = lag; }
    }
    return bestLag ? sr / (double) bestLag : 0.0;
}

// One pluck of the string model on MIDI 57 (220 Hz), then the measured pitch.
static double stringPitch (double sr, int transpose, float fine, int snap, double lo, double hi)
{
    E::Params p; p.model = 1; p.geometry = 0.0f; p.space = 0.3f; p.damping = 0.5f; p.velocityToStrength = false;
    p.transpose = transpose; p.fine = fine; p.pitchSnap = snap;
    E e; e.prepare (sr);
    const int N = 512; const long total = (long) (sr * 1.5);
    std::vector<float> in (N, 0.f), L (N), R (N), note (N, 57.f), vel (N, 0.8f); std::vector<uint8_t> gate (N, 0), retrig (N, 0);
    std::vector<float> cap; long count = 0;
    while (count < total)
    {
        for (int i = 0; i < N; ++i, ++count) gate[(size_t) i] = count < (long) (sr * 0.05) ? 1 : 0;
        E::Control c; c.gate = gate.data(); c.retrig = retrig.data(); c.note = note.data(); c.vel = vel.data();
        e.process (in.data(), in.data(), L.data(), R.data(), N, p, c);
        for (int i = 0; i < N; ++i) cap.push_back (0.5f * (L[(size_t) i] + R[(size_t) i]));
    }
    return pitchOf (cap, sr, (size_t) (sr * 0.3), (size_t) (sr * 0.4), lo, hi);
}

int main()
{
    int fails = 0;

    // 1. the function
    {
        static const float steps[] = { 0, 7, 12, 19, 24, 31, 36, 43, 48 };
        bool ok = true; float prev = -100.f; int distinct = 0; float last = 1e9f;
        for (int i = -4800; i <= 4800; ++i)
        {
            const float k = (float) i * 0.01f;
            const float st = E::snapPitch (k, E::SnapSteps);
            const float off = E::snapPitch (k, E::SnapOff);
            float bestDist = 1e9f; for (float s : steps) bestDist = std::min (bestDist, std::fabs (std::fabs (k) - s));
            bool onStep = false; for (float s : steps) onStep = onStep || std::fabs (std::fabs (st) - s) < 1e-6f;
            ok = ok && onStep && st >= prev - 1e-6f && std::fabs (std::fabs (st) - std::fabs (k)) <= bestDist + 1e-4f
                    && (st == 0.f || (st < 0.f) == (k < 0.f))
                    && std::fabs (E::snapPitch (k, E::SnapSemitones) - std::round (k)) < 1e-6f
                    && std::memcmp (&k, &off, sizeof (float)) == 0;
            prev = st; if (st != last) { ++distinct; last = st; }
        }
        ok = ok && distinct == 17;
        printf ("snap function over -48..+48: Steps visit exactly 17 values (%d seen), the nearest one, never backwards; Semitones round; Off returns the same bits  %s\n",
                distinct, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 2. the sound is bit for bit the plain setting at the snapped value
    {
        struct C { int transpose; float fine; int snap; int refTranspose; };
        const C cs[] = {
            {  4,  0.00f, E::SnapSteps,     7 },     // 4    -> 7
            {  5,  1.20f, E::SnapSteps,     7 },     // 6.2  -> 7
            { -9,  0.00f, E::SnapSteps,    -7 },     // -9   -> -7
            { 11,  1.50f, E::SnapSteps,    12 },     // 12.5 -> 12
            { 30, -0.50f, E::SnapSteps,    31 },     // 29.5 -> 31
            { -3,  0.40f, E::SnapSemitones, -3 },    // -2.6 -> -3
            {  4,  0.60f, E::SnapSemitones,  5 },    //  4.6 -> 5
            {  0, -1.70f, E::SnapSemitones, -2 } };
        bool ok = true;
        for (int model = 0; model < 3; ++model)
            for (const auto& c : cs)
            {
                auto make = [&] (int t, float f, int snap)
                { return [=] (int) { E::Params p; p.model = model; p.transpose = t; p.fine = f; p.pitchSnap = snap; return p; }; };
                const auto a = render (48000.0, 2.0, make (c.transpose, c.fine, c.snap));
                const auto r = render (48000.0, 2.0, make (c.refTranspose, 0.0f, E::SnapOff));
                const auto u = render (48000.0, 2.0, make (c.transpose, c.fine, E::SnapOff));
                double e = 0; for (float v : r) e += (double) v * v;
                const bool same = a == r && e > 0.05;
                const bool moved = a != u;
                if (! same || ! moved) { ok = false; printf ("  model %d transpose %+d fine %+.2f snap %d: bit-identical to plain=%d (energy %.2f), differs from unsnapped=%d\n", model, c.transpose, c.fine, c.snap, (int) same, e, (int) moved); }
            }
        printf ("pitch snap through the engine: 8 settings x 3 models are bit for bit the plain Transpose at the snapped value, and differ from the unsnapped one  %s\n", ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 3. the pitch itself (string model, MIDI 57 = 220 Hz); the autocorrelation method resolves about 30 cents, and the search window
    //    is +-1.4x around the expected note (it can mistake a pitch for its octave otherwise). Steps are >= 5 semitones apart.
    {
        struct C { int transpose; float fine; int snap; double offset; };
        const C cs[] = { { 4, 0.0f, E::SnapSteps, 7 },   { 11, 1.0f, E::SnapSteps, 12 }, { -9, 0.0f, E::SnapSteps, -7 }, { -10, 0.0f, E::SnapSteps, -12 },
                         { 19, 0.0f, E::SnapSteps, 19 }, { 3, 0.0f, E::SnapSteps, 0 },
                         { 4, 0.6f, E::SnapSemitones, 5 }, { 2, 0.7f, E::SnapSemitones, 3 }, { -4, -0.8f, E::SnapSemitones, -5 },
                         { 4, 0.6f, E::SnapOff, 4.6 } };
        bool ok = true; double worst = 0;
        for (double sr : { 44100.0, 48000.0 })
            for (const auto& c : cs)
            {
                const double want = 220.0 * std::pow (2.0, c.offset / 12.0);
                const double got = stringPitch (sr, c.transpose, c.fine, c.snap, want / 1.4, want * 1.4);   // window excludes the octaves, which autocorrelation can confuse
                const double cents = 1200.0 * std::log2 (got / want);
                worst = std::max (worst, std::fabs (cents));
                if (std::fabs (cents) >= 30.0) { ok = false; printf ("  transpose %+d fine %+.1f snap %d sr %.0f: want %.2f Hz got %.2f Hz (%+.1f cents)\n", c.transpose, c.fine, c.snap, sr, want, got, cents); }
            }
        printf ("pitch snap, measured on the string model: %d settings x 2 sample rates land on the snapped note; worst error %.1f cents (limit 30)  %s\n",
                (int) (sizeof (cs) / sizeof (cs[0])), worst, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 4. every model, every snap mode, Transpose and Fine sweeping
    {
        bool ok = true;
        for (int snap : { (int) E::SnapOff, (int) E::SnapSteps, (int) E::SnapSemitones })
            for (int model = 0; model < 3; ++model)
                for (int ominous = 0; ominous < 2; ++ominous)
                {
                    const auto a = render (48000.0, 2.0, [=] (int b)
                    {
                        E::Params p; p.model = model; p.easterEgg = ominous != 0; p.pitchSnap = snap;
                        p.transpose = (int) std::lround (36.0 * std::sin ((double) b * 0.11));
                        p.fine = 2.0f * (float) std::sin ((double) b * 0.23);
                        return p;
                    });
                    bool fin = true; double e = 0, pk = 0;
                    for (float v : a) { fin = fin && std::isfinite (v); e += (double) v * v; pk = std::max (pk, (double) std::fabs (v)); }
                    if (! (fin && e > 0.05 && pk < 4.0)) { ok = false; printf ("  snap %d model %d ominous %d: finite=%d energy=%.2f peak=%.2f\n", snap, model, ominous, (int) fin, e, pk); }
                }
        printf ("all 3 models (and the ominous voice) x 3 snap modes stay finite and audible while Transpose and Fine sweep  %s\n", ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    printf ("%d failed\n", fails);
    return fails;
}
