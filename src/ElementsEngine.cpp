#include "ElementsEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include "elements/dsp/part.h"
#include "elements/dsp/patch.h"

namespace
{
    // The firmware's noise gate on both audio inputs (elements.cc).
    constexpr float kNoiseGateThreshold = 0.0001f;

    // calloc, not new + memset: with a memset before the constructor, GCC treats the old contents as dead and deletes it
    // (lifetime dead-store elimination), which silently brings the junk back.
    template <typename T>
    std::unique_ptr<T, ZeroedDeleter<T>> makeZeroed()
    {
        void* mem = std::calloc (1, sizeof (T));
        if (mem == nullptr) throw std::bad_alloc();
        return std::unique_ptr<T, ZeroedDeleter<T>> (new (mem) T());
    }
}

template <typename T>
void ZeroedDeleter<T>::operator() (T* p) const
{
    if (p != nullptr) { p->~T(); std::free (static_cast<void*> (p)); }
}

ElementsEngine::ElementsEngine()
    : part (makeZeroed<elements::Part>())
{
    reverbBuf.assign (32768, 0);
    part->Init (reverbBuf.data());
}

ElementsEngine::~ElementsEngine() = default;

void ElementsEngine::prepare (double sr)
{
    hostRate = sr;
    rsStrike.init (hostRate, kRate);
    rsBlow.init (hostRate, kRate);
    rsOutL.init (kRate, hostRate);
    rsOutR.init (kRate, hostRate);
    inQueue.clear(); inQueueHead = 0;
    procOutL.clear(); procOutR.clear();
    hostQL.clear(); hostQR.clear(); hostQHead = 0;
    const size_t prime = kBlock * 2 + SincResampler::kTaps;     // keeps the output resampler from starving
    procOutL.assign (prime, 0.0f); procOutR.assign (prime, 0.0f);
    strikeLevel = blowLevel = 0.0f;
    prevBlockGate = false;
    pendGate = 0; pendRetrig = 0; pendNote = lastNote; pendVel = lastVel;
    controlsInit = false;     // snap the control smoothers to the knob positions on the next block
}

int ElementsEngine::latencyInHostSamples() const
{
    const double procFrames = (double) kBlock * 2 + SincResampler::kTaps;
    if (std::abs (hostRate - kRate) < 1e-6) return (int) procFrames;
    return (int) std::ceil (procFrames * hostRate / kRate);
}

void ElementsEngine::applyConfig (const Params& p)
{
    const int model = std::clamp (p.model, 0, 2);
    if (model != curModel)
    {
        part->set_resonator_model ((elements::ResonatorModel) model);
        curModel = model;
    }
    if (! eggInit || p.easterEgg != curEgg)
    {
        part->set_easter_egg (p.easterEgg);
        curEgg = p.easterEgg; eggInit = true;
    }
}

void ElementsEngine::runBlocks (const Params& p)
{
    using namespace elements;
    const size_t B = kBlock;

    float strikeIn[kMaxBlockSize], blowIn[kMaxBlockSize], mainOut[kMaxBlockSize], auxOut[kMaxBlockSize];

    while (inQueue.size() - inQueueHead >= B)
    {
        // ---- gather this block's audio and events ----
        uint8_t anyGate = 0, anyRetrig = 0;
        float note = lastNote, vel = lastVel;
        for (size_t i = 0; i < B; ++i)
        {
            const Frame& f = inQueue[inQueueHead + i];
            anyGate |= f.gate; anyRetrig |= f.retrig;
            note = f.note; vel = f.vel;

            float error = f.strike * f.strike - strikeLevel;
            strikeLevel += error * (error > 0.0f ? 0.1f : 0.0001f);
            float gain = strikeLevel <= kNoiseGateThreshold ? (1.0f / kNoiseGateThreshold) * strikeLevel : 1.0f;
            strikeIn[i] = gain * f.strike;

            error = f.blow * f.blow - blowLevel;
            blowLevel += error * (error > 0.0f ? 0.1f : 0.0001f);
            gain = blowLevel <= kNoiseGateThreshold ? (1.0f / kNoiseGateThreshold) * blowLevel : 1.0f;
            blowIn[i] = gain * f.blow;
        }
        inQueueHead += B;
        lastNote = note; lastVel = vel;

        // ---- control smoothing, as the firmware's CV scaler does once per block ----
        // Stage 1: every panel pot goes through a 0.01 one-pole. Stage 2 (BIND): a per-control coefficient and a clamp.
        auto stage1 = [this] (Smooth& s, float target)
        {
            if (! controlsInit) { s.pot = target; s.bound = target; }
            else s.pot += 0.01f * (target - s.pot);
        };
        auto bind = [this] (Smooth& s, float lpCoeff, float hi)
        {
            const float v = std::clamp (s.pot, 0.0f, hi);
            if (! controlsInit || lpCoeff == 1.0f) s.bound = v; else s.bound += (v - s.bound) * lpCoeff;
            return s.bound;
        };

        stage1 (sContour, p.contour);     stage1 (sBow, p.bowLevel);          stage1 (sBlow, p.blowLevel);       stage1 (sStrike, p.strikeLevel);
        stage1 (sBowT, p.bowTimbre);      stage1 (sBlowM, p.blowMeta);        stage1 (sBlowT, p.blowTimbre);
        stage1 (sStrikeM, p.strikeMeta);  stage1 (sStrikeT, p.strikeTimbre);
        stage1 (sGeom, p.geometry);       stage1 (sBright, p.brightness);     stage1 (sDamp, p.damping);         stage1 (sPos, p.position);
        stage1 (sSpace, p.freeze ? 2.0f : std::clamp (p.space, 0.0f, 1.0f));

        Patch* patch = part->mutable_patch();
        patch->exciter_envelope_shape = sContour.pot;
        patch->exciter_bow_level      = sBow.pot;
        patch->exciter_blow_level     = sBlow.pot;
        patch->exciter_strike_level   = sStrike.pot;
        patch->exciter_bow_timbre     = bind (sBowT,    1.0f,   0.9995f);
        patch->exciter_blow_meta      = bind (sBlowM,   0.05f,  0.9995f);
        patch->exciter_blow_timbre    = bind (sBlowT,   1.0f,   0.9995f);
        patch->exciter_strike_meta    = bind (sStrikeM, 0.05f,  0.9995f);
        patch->exciter_strike_timbre  = bind (sStrikeT, 1.0f,   0.995f);
        patch->resonator_geometry     = bind (sGeom,    0.05f,  0.9995f);
        patch->resonator_brightness   = bind (sBright,  1.0f,   0.9995f);
        patch->resonator_damping      = bind (sDamp,    1.0f,   0.9995f);
        patch->resonator_position     = bind (sPos,     0.01f,  0.9995f);
        patch->space                  = bind (sSpace,   0.01f,  2.0f);
        controlsInit = true;

        // ---- performance state ----
        // Gate: any sample of the block with the key down (or the GATE button). A legato note-on forces the gate low for one
        // block so the exciters and the envelope see a new rising edge, as they would from a retriggering keyboard.
        bool gate = anyGate != 0 || p.manualGate;
        if (anyRetrig != 0 && prevBlockGate) gate = false;
        prevBlockGate = gate;

        PerformanceState ps;
        ps.gate = gate;
        ps.note = note + (float) p.transpose + std::clamp (p.fine, -2.0f, 2.0f);   // the DSP takes a real MIDI note number
        ps.modulation = 0.0f;                                                       // the FM input has no equivalent here
        const float strength = (p.velocityToStrength && vel >= 0.0f) ? vel : p.strength;
        ps.strength = std::clamp (strength, 0.0f, 1.0f);

        part->Process (ps, blowIn, strikeIn, mainOut, auxOut, B);   // note the firmware's argument order: blow first, then strike

        // main is the right channel, aux the left one (Part's mixdown: main += centre - side, aux += centre + side)
        for (size_t i = 0; i < B; ++i)
        {
            float l = auxOut[i], r = mainOut[i];
            if (! std::isfinite (l)) l = 0.0f;
            if (! std::isfinite (r)) r = 0.0f;
            if (p.monoOut) { l = r = 0.7071f * (l + r); }
            procOutL.push_back (std::clamp (l, -4.0f, 4.0f));
            procOutR.push_back (std::clamp (r, -4.0f, 4.0f));
        }
    }
    if (inQueueHead > 4096)
    {
        inQueue.erase (inQueue.begin(), inQueue.begin() + (long) inQueueHead);
        inQueueHead = 0;
    }
}

void ElementsEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                              int n, const Params& p, const Control& c)
{
    applyConfig (p);

    // 1. route the audio inputs, then host rate -> 32 kHz. Events are OR-ed over the span of host samples each 32 kHz sample
    //    covers, so a note-on is never dropped when the host runs faster than 32 kHz.
    strikeHost.assign ((size_t) n, 0.0f);
    blowHost.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        switch (p.audioRoute)
        {
            case MonoToStrike: strikeHost[(size_t) i] = 0.5f * (inL[i] + inR[i]); break;
            case MonoToBlow:   blowHost[(size_t) i]   = 0.5f * (inL[i] + inR[i]); break;
            default:           strikeHost[(size_t) i] = inL[i]; blowHost[(size_t) i] = inR[i]; break;
        }
    }
    tmpStrike.clear(); tmpBlow.clear();
    rsStrike.process (strikeHost.data(), (size_t) n, tmpStrike);
    rsBlow.process (blowHost.data(), (size_t) n, tmpBlow);
    const size_t produced = std::min (tmpStrike.size(), tmpBlow.size());

    size_t j = 0;
    uint8_t spanGate = 0, spanRetrig = 0;
    bool seen = false;
    auto takeEvents = [&] (size_t upTo)
    {
        for (; j < upTo; ++j)
        {
            seen = true;
            if (c.gate)   { spanGate |= c.gate[j]; pendGate = c.gate[j]; }
            if (c.retrig) spanRetrig |= c.retrig[j];
            if (c.note)   pendNote = c.note[j];
            if (c.vel)    pendVel = c.vel[j];
        }
    };
    for (size_t i = 0; i < produced; ++i)
    {
        const size_t end = (i + 1 == produced) ? (size_t) n : (i + 1) * (size_t) n / produced;
        spanGate = 0; seen = false;
        takeEvents (end);
        pendRetrig |= spanRetrig; spanRetrig = 0;
        // an empty span (host slower than 32 kHz) repeats the last known gate state; a retrigger flag is never dropped
        inQueue.push_back ({ tmpStrike[i], tmpBlow[i], seen ? spanGate : pendGate, pendRetrig, pendNote, pendVel });
        pendRetrig = 0;
    }
    takeEvents ((size_t) n);     // samples not yet covered by a produced frame: keep their events for the next call
    pendRetrig |= spanRetrig;

    // 2. run the module DSP
    runBlocks (p);

    // 3. 32 kHz -> host rate
    rsOutL.process (procOutL.data(), procOutL.size(), hostQL);
    rsOutR.process (procOutR.data(), procOutR.size(), hostQR);
    procOutL.clear(); procOutR.clear();

    const size_t avail = std::min (hostQL.size(), hostQR.size()) - hostQHead;
    const size_t take = std::min (avail, (size_t) n);
    for (size_t i = 0; i < take; ++i) { outL[i] = hostQL[hostQHead + i]; outR[i] = hostQR[hostQHead + i]; }
    for (size_t i = take; i < (size_t) n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }
    hostQHead += take;
    if (hostQHead > 8192)
    {
        hostQL.erase (hostQL.begin(), hostQL.begin() + (long) hostQHead);
        hostQR.erase (hostQR.begin(), hostQR.begin() + (long) hostQHead);
        hostQHead = 0;
    }
}
