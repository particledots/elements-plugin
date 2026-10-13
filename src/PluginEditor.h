#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Simple flat look for rotary knobs.
class ElementsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ElementsLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class ElementsEditor : public juce::AudioProcessorEditor
{
public:
    explicit ElementsEditor (ElementsAudioProcessor&);
    ~ElementsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAtt> att;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAtt> att;
    };

    void addKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void addChoice (Choice&, const juce::String& paramId, const juce::String& name, const juce::StringArray& items);
    void addToggle (Toggle&, const juce::String& paramId, const juce::String& text);

    ElementsAudioProcessor& proc;
    ElementsLookAndFeel laf;

    // exciter row
    Knob contour, bowLevel, bowTimbre, blowLevel, blowMeta, blowTimbre, strikeLevel, strikeMeta, strikeTimbre;
    // resonator row
    Knob geometry, brightness, damping, position, space, transpose, fine, strength;
    Choice model, route;
    Toggle gateBtn, velStrength, freeze, ominous, monoOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElementsEditor)
};
