#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using API = juce::AudioParameterInt;

const juce::StringArray& ElementsAudioProcessor::modelNames()
{
    static const juce::StringArray n { "Modal resonator", "String", "Strings (chords)" };
    return n;
}

const juce::StringArray& ElementsAudioProcessor::routeNames()
{
    static const juce::StringArray n { "L = Strike, R = Blow", "Mono to Strike", "Mono to Blow" };
    return n;
}

juce::AudioProcessorValueTreeState::ParameterLayout ElementsAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.f, 1.f, 0.0001f), def,
                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); }));
    };

    // defaults are the firmware's own start-up patch (Part::Init)
    p.push_back (unit ("contour",       "Contour",       1.0f));
    p.push_back (unit ("bow_level",     "Bow Level",     0.0f));
    p.push_back (unit ("bow_timbre",    "Bow Timbre",    0.5f));
    p.push_back (unit ("blow_level",    "Blow Level",    0.0f));
    p.push_back (unit ("blow_meta",     "Blow Meta",     0.5f));
    p.push_back (unit ("blow_timbre",   "Blow Timbre",   0.5f));
    p.push_back (unit ("strike_level",  "Strike Level",  0.8f));
    p.push_back (unit ("strike_meta",   "Strike Meta",   0.5f));
    p.push_back (unit ("strike_timbre", "Strike Timbre", 0.5f));

    p.push_back (unit ("geometry",      "Geometry",      0.2f));
    p.push_back (unit ("brightness",    "Brightness",    0.5f));
    p.push_back (unit ("damping",       "Damping",       0.25f));
    p.push_back (unit ("position",      "Position",      0.3f));
    p.push_back (unit ("space",         "Space",         0.5f));

    p.push_back (std::make_unique<API> (juce::ParameterID { "transpose", 1 }, "Transpose", -36, 36, 0,
                 juce::AudioParameterIntAttributes().withLabel ("st")));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "fine", 1 }, "Fine",
                 juce::NormalisableRange<float> (-2.f, 2.f, 0.01f), 0.f,
                 juce::AudioParameterFloatAttributes().withLabel ("st")));
    p.push_back (unit ("strength",      "Strength",      0.5f));

    p.push_back (std::make_unique<APC> (juce::ParameterID { "model", 1 }, "Resonator Model", modelNames(), 0));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "audio_route", 1 }, "Audio Input", routeNames(), 0));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "vel_strength", 1 }, "Velocity to Strength", true));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "gate_btn", 1 }, "Gate Button", false));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "freeze", 1 }, "Reverb Freeze", false));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "ominous", 1 }, "Ominous Voice", false));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "mono_out", 1 }, "Mono Out", false));
    return { p.begin(), p.end() };
}

ElementsAudioProcessor::ElementsAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool ElementsAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && l.getMainInputChannelSet() == out;
}

void ElementsAudioProcessor::prepareToPlay (double sr, int maxBlock)
{
    engine.prepare (sr);
    held.clear(); gateOn = false;
    gateFlags.assign ((size_t) maxBlock, 0);
    retrigFlags.assign ((size_t) maxBlock, 0);
    noteValues.assign ((size_t) maxBlock, currentNote);
    velValues.assign ((size_t) maxBlock, currentVel);
    scratchL.assign ((size_t) maxBlock, 0.f);
    scratchR.assign ((size_t) maxBlock, 0.f);
    setLatencySamples (engine.latencyInHostSamples());
}

ElementsEngine::Params ElementsAudioProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    ElementsEngine::Params p;
    p.contour = v ("contour");
    p.bowLevel = v ("bow_level");       p.bowTimbre = v ("bow_timbre");
    p.blowLevel = v ("blow_level");     p.blowMeta = v ("blow_meta");       p.blowTimbre = v ("blow_timbre");
    p.strikeLevel = v ("strike_level"); p.strikeMeta = v ("strike_meta");   p.strikeTimbre = v ("strike_timbre");
    p.geometry = v ("geometry");        p.brightness = v ("brightness");    p.damping = v ("damping");
    p.position = v ("position");        p.space = v ("space");
    p.transpose = (int) std::lround (v ("transpose"));
    p.fine = v ("fine");
    p.strength = v ("strength");
    p.model = (int) v ("model");
    p.audioRoute = (int) v ("audio_route");
    p.velocityToStrength = v ("vel_strength") > 0.5f;
    p.manualGate = v ("gate_btn") > 0.5f;
    p.freeze = v ("freeze") > 0.5f;
    p.easterEgg = v ("ominous") > 0.5f;
    p.monoOut = v ("mono_out") > 0.5f;
    return p;
}

void ElementsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();

    if ((size_t) n > scratchL.size())
    {   // host exceeded its promised max block size
        scratchL.resize ((size_t) n); scratchR.resize ((size_t) n);
        gateFlags.resize ((size_t) n); retrigFlags.resize ((size_t) n);
        noteValues.resize ((size_t) n); velValues.resize ((size_t) n);
    }

    const auto params = readParams();

    // MIDI -> gate / pitch / velocity, sample-accurate. Last-note priority; the gate drops when the last key is released.
    std::fill (gateFlags.begin(), gateFlags.begin() + n, (uint8_t) 0);
    std::fill (retrigFlags.begin(), retrigFlags.begin() + n, (uint8_t) 0);
    {
        int pos = 0;
        auto fillTo = [&] (int to)
        {
            for (; pos < to; ++pos)
            {
                gateFlags[(size_t) pos] = gateOn ? 1 : 0;
                noteValues[(size_t) pos] = currentNote;
                velValues[(size_t) pos] = currentVel;
            }
        };
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int at = juce::jlimit (0, n, meta.samplePosition);
            fillTo (at);
            if (msg.isNoteOn())
            {
                const bool legato = gateOn;
                held.erase (std::remove_if (held.begin(), held.end(), [&] (const Held& h) { return h.note == msg.getNoteNumber(); }), held.end());
                held.push_back ({ msg.getNoteNumber(), msg.getFloatVelocity() });
                currentNote = (float) msg.getNoteNumber();
                currentVel = msg.getFloatVelocity();
                gateOn = true;
                if (legato && at < n) retrigFlags[(size_t) at] = 1;
            }
            else if (msg.isNoteOff())
            {
                held.erase (std::remove_if (held.begin(), held.end(), [&] (const Held& h) { return h.note == msg.getNoteNumber(); }), held.end());
                if (held.empty()) gateOn = false;
                else { currentNote = (float) held.back().note; currentVel = held.back().vel; }
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                held.clear(); gateOn = false;
            }
        }
        fillTo (n);
    }
    midi.clear();

    ElementsEngine::Control ctl;
    ctl.gate = gateFlags.data(); ctl.retrig = retrigFlags.data(); ctl.note = noteValues.data(); ctl.vel = velValues.data();

    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getReadPointer (chans > 1 ? 1 : 0);
    engine.process (inL, inR, scratchL.data(), scratchR.data(), n, params, ctl);

    if (chans > 1)
    {
        buffer.copyFrom (0, 0, scratchL.data(), n);
        buffer.copyFrom (1, 0, scratchR.data(), n);
    }
    else
    {
        float* o = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) o[i] = 0.7071f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
    }
}

juce::AudioProcessorEditor* ElementsAudioProcessor::createEditor()
{
    return new ElementsEditor (*this);
}

void ElementsAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void ElementsAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ElementsAudioProcessor(); }
