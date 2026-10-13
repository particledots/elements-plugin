// JUCE-free wrapper around the Mutable Instruments Elements DSP (MIT licensed, Emilie Gillet).
#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include "Resampler.h"

namespace elements { class Part; }

// The module firmware keeps its DSP objects in globals, i.e. zeroed memory, and some of them rely on that (members that are
// never explicitly initialised, e.g. Part::panic_ and Part::easter_egg_). A plain `new` returns whatever junk the heap held,
// which can crash or silence the plug-in depending on what the process happened to allocate earlier. These objects are
// therefore always built in zero-filled memory.
template <typename T> struct ZeroedDeleter { void operator() (T*) const; };

class ElementsEngine
{
public:
    // Where the plug-in's audio input goes. Left = strike input and right = blow input is how the module's two jacks are wired.
    enum AudioRoute { LeftStrikeRightBlow = 0, MonoToStrike = 1, MonoToBlow = 2 };

    struct Params
    {
        // Exciter section (0..1 unless noted). Names follow the firmware's Patch struct.
        float contour = 1.0f;                       // exciter_envelope_shape
        float bowLevel = 0.0f, bowTimbre = 0.5f;
        float blowLevel = 0.0f, blowMeta = 0.5f, blowTimbre = 0.5f;
        float strikeLevel = 0.8f, strikeMeta = 0.5f, strikeTimbre = 0.5f;
        // Resonator section
        float geometry = 0.2f, brightness = 0.5f, damping = 0.25f, position = 0.3f;
        float space = 0.5f;                         // 0..1 on the panel
        bool  freeze = false;                       // the firmware's reverb freeze (space >= 1.75, reachable on hardware only with CV)
        int   model = 0;                            // 0 modal, 1 string, 2 strings
        bool  easterEgg = false;                    // the firmware's "ominous voice"
        // Pitch
        int   transpose = 0;                        // semitones, added to the MIDI note
        float fine = 0.0f;                          // semitones, -2..2
        // Performance
        float strength = 0.5f;                      // 0..1, 0.5 is unity gain in the firmware's accent table
        bool  velocityToStrength = true;
        bool  manualGate = false;                   // the module's GATE button (OR-ed with the MIDI gate)
        int   audioRoute = LeftStrikeRightBlow;
        bool  monoOut = false;
    };

    // Per-host-sample MIDI-derived control. Pointers may be null.
    //   gate[i]    : 1 while a note is held
    //   retrig[i]  : 1 at the sample where a note-on arrives while another note is still held (legato retrigger)
    //   note[i]    : MIDI note number in effect
    //   vel[i]     : velocity 0..1 of the current note, or < 0 if no MIDI note has been played yet
    struct Control { const uint8_t* gate = nullptr; const uint8_t* retrig = nullptr; const float* note = nullptr; const float* vel = nullptr; };

    ElementsEngine();
    ~ElementsEngine();

    void prepare (double hostSampleRate);

    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const Params& p, const Control& c);

    int latencyInHostSamples() const;

    static constexpr double kRate = 32000.0;
    static constexpr size_t kBlock = 16;

private:
    struct Frame { float strike, blow; uint8_t gate, retrig; float note, vel; };

    void applyConfig (const Params& p);
    void runBlocks (const Params& p);

    std::unique_ptr<elements::Part, ZeroedDeleter<elements::Part>> part;
    std::vector<uint16_t> reverbBuf;

    double hostRate = 48000.0;

    uint8_t pendGate = 0, pendRetrig = 0;      // events waiting for the next 32 kHz sample
    float pendNote = 48.0f, pendVel = -1.0f;
    std::vector<float> strikeHost, blowHost, tmpStrike, tmpBlow;

    // firmware-style control smoothing (per 16-sample block). "pot" = the firmware's 0.01 pot low-pass, "bound" = the second stage.
    struct Smooth { float pot = 0.0f, bound = 0.0f; };
    Smooth sContour, sBow, sBowT, sBlow, sBlowM, sBlowT, sStrike, sStrikeM, sStrikeT, sGeom, sBright, sDamp, sPos, sSpace;
    bool controlsInit = false;
    float strikeLevel = 0.0f, blowLevel = 0.0f;   // input noise gates
    float lastNote = 48.0f, lastVel = -1.0f;
    bool prevBlockGate = false;
    int curModel = -1;
    bool curEgg = false, eggInit = false;

    SincResampler rsStrike, rsBlow, rsOutL, rsOutR;
    std::vector<float> hostQL, hostQR;
    size_t hostQHead = 0;
    std::vector<Frame> inQueue;
    size_t inQueueHead = 0;
    std::vector<float> procOutL, procOutR;
};
