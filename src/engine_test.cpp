// Offline sanity test of ElementsEngine: every resonator model, every exciter, the audio inputs, the easter-egg voice, at several
// host sample rates. Checks that output is finite, bounded and not silent, plus two behavioural checks (pitch of the string
// model, and that a legato note-on retriggers). It says nothing about how the result sounds.
#include "ElementsEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>

struct Result { bool ok; double energy, peak; };

enum Drive { GateNotes, AudioOnly };

static Result run (double sr, ElementsEngine::Params p, Drive drive, bool noiseIn, double seconds = 4.0, std::vector<float>* capture = nullptr)
{
    ElementsEngine e; e.prepare (sr);
    const int N = 512;
    std::vector<float> in (N), outL (N), outR (N), note (N, 57.0f), vel (N, 0.8f);
    std::vector<uint8_t> gate (N, 0), retrig (N, 0);
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> dist (-1.f, 1.f);
    const int blocks = (int) (sr * seconds / N);
    const long period = (long) (sr * 0.5);              // a note every half second, held for 60 % of it
    long sampleCount = 0; bool bad = false; double peak = 0, energy = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < N; ++i, ++sampleCount)
        {
            const bool burst = noiseIn && (sampleCount % period) < (long) (sr * 0.01);
            in[(size_t) i] = burst ? 0.8f * dist (rng) : 0.0f;
            gate[(size_t) i] = (drive == GateNotes && (sampleCount % period) < (long) (period * 0.6)) ? 1 : 0;
            retrig[(size_t) i] = 0;
            note[(size_t) i] = 48.0f + (float) ((sampleCount / period) % 4) * 3.0f;
            vel[(size_t) i] = 0.3f + 0.2f * (float) ((sampleCount / period) % 4);
        }
        ElementsEngine::Control c; c.gate = gate.data(); c.retrig = retrig.data(); c.note = note.data(); c.vel = vel.data();
        e.process (in.data(), in.data(), outL.data(), outR.data(), N, p, c);
        for (int i = 0; i < N; ++i)
        {
            const float l = outL[(size_t) i], r = outR[(size_t) i];
            if (! std::isfinite (l) || ! std::isfinite (r)) bad = true;
            peak = std::max (peak, (double) std::max (std::fabs (l), std::fabs (r)));
            energy += l * l + r * r;
            if (capture) capture->push_back (0.5f * (l + r));
        }
    }
    return { ! bad && energy > 0.05 && peak < 4.0, energy, peak };
}

// Fundamental frequency by autocorrelation over a window.
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

int main()
{
    int fails = 0, runs = 0;
    auto report = [&] (const char* what, double sr, const Result& r)
    {
        ++runs; fails += ! r.ok;
        printf ("%-48s sr=%6.0f energy=%10.2f peak=%5.2f  %s\n", what, sr, r.energy, r.peak, r.ok ? "OK" : "FAIL");
    };

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // resonator models x velocity on/off, default strike exciter, gate + notes
        for (int model = 0; model < 3; ++model)
            for (bool velOn : { true, false })
            {
                ElementsEngine::Params p; p.model = model; p.velocityToStrength = velOn;
                char name[96]; snprintf (name, sizeof name, "model=%d velocity->strength=%d strike", model, (int) velOn);
                report (name, sr, run (sr, p, GateNotes, false));
            }
        // bow only and blow only, each model
        for (int model = 0; model < 3; ++model)
        {
            { ElementsEngine::Params p; p.model = model; p.strikeLevel = 0.0f; p.bowLevel = 0.9f;
              char name[96]; snprintf (name, sizeof name, "model=%d bow only", model);
              report (name, sr, run (sr, p, GateNotes, false)); }
            { ElementsEngine::Params p; p.model = model; p.strikeLevel = 0.0f; p.blowLevel = 0.9f;
              char name[96]; snprintf (name, sizeof name, "model=%d blow only", model);
              report (name, sr, run (sr, p, GateNotes, false)); }
        }
        // external audio as the exciter, three routings. The firmware adds the strike input straight into the resonator (works with
        // no gate), but the blow input is multiplied by the contour envelope, so it needs a gate (see the README).
        for (int route = 0; route < 3; ++route)
        {
            ElementsEngine::Params p; p.strikeLevel = 0.0f; p.audioRoute = route;
            const bool needsGate = route == ElementsEngine::MonoToBlow;
            char name[96]; snprintf (name, sizeof name, "audio exciter, route=%d, %s", route, needsGate ? "with gate" : "no gate");
            report (name, sr, run (sr, p, needsGate ? GateNotes : AudioOnly, true));
        }
        // manual gate button only
        { ElementsEngine::Params p; p.manualGate = true; report ("manual gate button", sr, run (sr, p, AudioOnly, false)); }
        // easter-egg voice
        { ElementsEngine::Params p; p.easterEgg = true; report ("ominous voice", sr, run (sr, p, GateNotes, false)); }
        // space: dry, full reverb, freeze
        for (float s : { 0.0f, 1.0f })
        {
            ElementsEngine::Params p; p.space = s;
            char name[96]; snprintf (name, sizeof name, "space=%.0f", s);
            report (name, sr, run (sr, p, GateNotes, false));
        }
        { ElementsEngine::Params p; p.freeze = true; report ("reverb freeze", sr, run (sr, p, GateNotes, false)); }
        // extremes of the knobs, every model
        for (int model = 0; model < 3; ++model)
            for (float v : { 0.0f, 1.0f })
            {
                ElementsEngine::Params p; p.model = model;
                p.contour = p.bowLevel = p.bowTimbre = p.blowLevel = p.blowMeta = p.blowTimbre = v;
                p.strikeLevel = 1.0f; p.strikeMeta = p.strikeTimbre = v;
                p.geometry = p.brightness = p.damping = p.position = v; p.space = v;
                p.transpose = v > 0.5f ? 36 : -36; p.fine = v > 0.5f ? 2.0f : -2.0f; p.strength = v;
                char name[96]; snprintf (name, sizeof name, "model=%d all knobs at %.0f, transpose extreme", model, v);
                report (name, sr, run (sr, p, GateNotes, false));
            }
    }

    // ---- behavioural check 1: pitch. String model, geometry 0, MIDI 57 (A3 = 220 Hz) should ring near 220 Hz. ----
    // Informational for the modal model (its partials are inharmonic by design, so only the string model is asserted).
    {
        const double sr = 48000.0;
        ElementsEngine::Params p; p.model = 1; p.geometry = 0.0f; p.space = 0.3f; p.damping = 0.5f; p.velocityToStrength = false;
        ElementsEngine e; e.prepare (sr);
        const int N = 512; const long total = (long) (sr * 1.5);
        std::vector<float> in (N, 0.f), L (N), R (N), note (N, 57.f), vel (N, 0.8f); std::vector<uint8_t> gate (N, 0), retrig (N, 0);
        std::vector<float> cap; long count = 0;
        while (count < total)
        {
            for (int i = 0; i < N; ++i, ++count) gate[(size_t) i] = count < (long) (sr * 0.05) ? 1 : 0;
            ElementsEngine::Control c; c.gate = gate.data(); c.retrig = retrig.data(); c.note = note.data(); c.vel = vel.data();
            e.process (in.data(), in.data(), L.data(), R.data(), N, p, c);
            for (int i = 0; i < N; ++i) cap.push_back (0.5f * (L[(size_t) i] + R[(size_t) i]));
        }
        const double f = pitchOf (cap, sr, (size_t) (sr * 0.3), (size_t) (sr * 0.4), 80.0, 800.0);
        const double cents = 1200.0 * std::log2 (f / 220.0);
        const bool ok = std::fabs (cents) < 30.0;
        ++runs; fails += ! ok;
        printf ("%-48s measured %.2f Hz (%+.1f cents vs 220 Hz)  %s\n", "string model pitch, MIDI 57", f, cents, ok ? "OK" : "FAIL");
    }

    // ---- behavioural check 2: a legato note-on (retrig flag while the gate stays high) must make a new attack. ----
    {
        const double sr = 48000.0;
        auto burstEnergy = [&] (bool retrigOn)
        {
            ElementsEngine::Params p; p.damping = 0.9f; p.velocityToStrength = false; p.space = 0.0f;
            ElementsEngine e; e.prepare (sr);
            const int N = 512; const long total = (long) (sr * 1.0), second = (long) (sr * 0.5);
            std::vector<float> in (N, 0.f), L (N), R (N), note (N, 57.f), vel (N, 0.8f); std::vector<uint8_t> gate (N, 1), retrig (N, 0);
            long count = 0; double eBefore = 0, eAfter = 0;
            while (count < total)
            {
                for (int i = 0; i < N; ++i, ++count) retrig[(size_t) i] = (retrigOn && count == second) ? 1 : 0;
                ElementsEngine::Control c; c.gate = gate.data(); c.retrig = retrig.data(); c.note = note.data(); c.vel = vel.data();
                e.process (in.data(), in.data(), L.data(), R.data(), N, p, c);
                for (int i = 0; i < N; ++i)
                {
                    const long t = count - N + i; const double s = (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i];
                    if (t >= second - (long) (sr * 0.1) && t < second) eBefore += s;
                    if (t >= second + (long) (sr * 0.02) && t < second + (long) (sr * 0.12)) eAfter += s;
                }
            }
            return std::pair<double, double> (eBefore, eAfter);
        };
        const auto with = burstEnergy (true), without = burstEnergy (false);
        const bool ok = with.second > 2.0 * without.second + 1e-9;
        ++runs; fails += ! ok;
        printf ("%-48s energy after note-on: retrig=%.4f no-retrig=%.4f  %s\n", "legato retrigger makes a new attack", with.second, without.second, ok ? "OK" : "FAIL");
    }

    printf ("%d runs, %d failed\n", runs, fails);
    return fails;
}
