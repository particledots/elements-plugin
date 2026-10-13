#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ElementsEngine.h"

class ElementsAudioProcessor : public juce::AudioProcessor
{
public:
    ElementsAudioProcessor();
    ~ElementsAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 15.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    static const juce::StringArray& modelNames();
    static const juce::StringArray& routeNames();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    ElementsEngine::Params readParams() const;

    ElementsEngine engine;

    // MIDI state: a last-note-priority stack of held keys, as a mono synth would keep.
    struct Held { int note; float vel; };
    std::vector<Held> held;
    bool gateOn = false;
    float currentNote = 48.0f;
    float currentVel = -1.0f;       // < 0 until the first note-on

    std::vector<uint8_t> gateFlags, retrigFlags;
    std::vector<float> noteValues, velValues, scratchL, scratchR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElementsAudioProcessor)
};
