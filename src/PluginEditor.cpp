#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg      (0xff1a1c20);
    const juce::Colour kPanel   (0xff23262c);
    const juce::Colour kTrack   (0xff3a3e47);
    const juce::Colour kAccent  (0xffd9a05b);
    const juce::Colour kText    (0xffe8e6e1);
    const juce::Colour kDim     (0xff8d909a);
}

ElementsLookAndFeel::ElementsLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kDim);
    setColour (juce::ComboBox::backgroundColourId, kPanel);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, kTrack);
    setColour (juce::ComboBox::arrowColourId, kAccent);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
}

void ElementsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    const float thick = juce::jmax (3.0f, d * 0.09f);
    const auto arcR = r.reduced (thick * 0.5f);

    juce::Path track;
    track.addCentredArc (arcR.getCentreX(), arcR.getCentreY(), arcR.getWidth() * 0.5f, arcR.getHeight() * 0.5f, 0, a0, a1, true);
    g.setColour (kTrack);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float angle = a0 + pos * (a1 - a0);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;   // bipolar controls fill from the centre
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    juce::Path val;
    val.addCentredArc (arcR.getCentreX(), arcR.getCentreY(), arcR.getWidth() * 0.5f, arcR.getHeight() * 0.5f, 0,
                       juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? kAccent : kDim);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto c = r.getCentre();
    const float len = d * 0.5f - thick * 2.2f;
    juce::Line<float> line (c.getPointOnCircumference (len * 0.35f, angle),
                            c.getPointOnCircumference (len, angle));
    g.setColour (kText);
    g.drawLine (line, juce::jmax (2.0f, d * 0.045f));
}

void ElementsLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? kAccent : kPanel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? kAccent : kTrack);
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
    g.setColour (on ? juce::Colours::black : kText);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

ElementsEditor::ElementsEditor (ElementsAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    addKnob (contour,      "contour",       "CONTOUR");
    addKnob (bowLevel,     "bow_level",     "BOW LEVEL");
    addKnob (bowTimbre,    "bow_timbre",    "BOW TIMBRE");
    addKnob (blowLevel,    "blow_level",    "BLOW LEVEL");
    addKnob (blowMeta,     "blow_meta",     "BLOW META");
    addKnob (blowTimbre,   "blow_timbre",   "BLOW TIMBRE");
    addKnob (strikeLevel,  "strike_level",  "STRIKE LEVEL");
    addKnob (strikeMeta,   "strike_meta",   "STRIKE META");
    addKnob (strikeTimbre, "strike_timbre", "STRIKE TIMBRE");

    addKnob (geometry,   "geometry",   "GEOMETRY");
    addKnob (brightness, "brightness", "BRIGHTNESS");
    addKnob (damping,    "damping",    "DAMPING");
    addKnob (position,   "position",   "POSITION");
    addKnob (space,      "space",      "SPACE");
    addKnob (transpose,  "transpose",  "TRANSPOSE");
    addKnob (fine,       "fine",       "FINE");
    addKnob (strength,   "strength",   "STRENGTH");
    transpose.slider.setNumDecimalPlacesToDisplay (0);
    fine.slider.setNumDecimalPlacesToDisplay (2);

    addChoice (model, "model",       "RESONATOR MODEL", ElementsAudioProcessor::modelNames());
    addChoice (route, "audio_route", "AUDIO INPUT",     ElementsAudioProcessor::routeNames());
    addChoice (pitchSnap, "pitch_snap", "PITCH SNAP", { "Off", "Steps", "Semitones" });
    pitchSnap.box.setTooltip ("What Transpose + Fine settle on. Steps = octaves and fifths (0, 7, 12, 19, 24 ... up to 48, both directions); "
                              "Semitones = whole semitones. With Pitch Snap on, Fine only matters when it tips the total over a step. "
                              "The Transpose readout shows the offset that plays.");
    // With Pitch Snap on, the Transpose readout shows the offset that actually plays (Transpose + Fine, snapped).
    transpose.slider.textFromValueFunction = [this] (double v)
    {
        const int mode = (int) proc.apvts.getRawParameterValue ("pitch_snap")->load();
        if (mode == ElementsEngine::SnapOff) return juce::String ((int) std::lround (v));
        const float total = (float) std::lround (v) + proc.apvts.getRawParameterValue ("fine")->load();
        const int st = (int) std::lround (ElementsEngine::snapPitch (total, mode));
        return st > 0 ? "+" + juce::String (st) : juce::String (st);
    };
    snapReadoutAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("pitch_snap"),
                                                                  [this] (float) { transpose.slider.updateText(); });
    fineReadoutAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("fine"),
                                                                  [this] (float) { transpose.slider.updateText(); });
    snapReadoutAtt->sendInitialUpdate();
    fineReadoutAtt->sendInitialUpdate();

    addToggle (gateBtn,     "gate_btn",     "GATE");
    addToggle (velStrength, "vel_strength", "VELOCITY > STRENGTH");
    addToggle (freeze,      "freeze",       "REVERB FREEZE");
    addToggle (ominous,     "ominous",      "OMINOUS VOICE");
    addToggle (monoOut,     "mono_out",     "MONO OUT");

    setSize (800, 560);
}

ElementsEditor::~ElementsEditor() { setLookAndFeel (nullptr); }

void ElementsEditor::addKnob (Knob& k, const juce::String& id, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    k.slider.setPopupDisplayEnabled (false, false, nullptr);
    k.slider.setNumDecimalPlacesToDisplay (2);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
}

void ElementsEditor::addChoice (Choice& c, const juce::String& id, const juce::String& name, const juce::StringArray& items)
{
    c.box.addItemList (items, 1);
    c.label.setText (name, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredLeft);
    c.label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    addAndMakeVisible (c.box);
    addAndMakeVisible (c.label);
    c.att = std::make_unique<ComboAtt> (proc.apvts, id, c.box);
}

void ElementsEditor::addToggle (Toggle& t, const juce::String& id, const juce::String& text)
{
    t.button.setButtonText (text);
    addAndMakeVisible (t.button);
    t.att = std::make_unique<ButtonAtt> (proc.apvts, id, t.button);
}

void ElementsEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    g.drawText ("UNOFFICIAL ELEMENTS", 20, 10, 400, 26, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("modal synthesis voice - community port, not a Mutable Instruments product", 20, 34, 560, 16, juce::Justification::centredLeft);

    g.setColour (kPanel);
    g.fillRoundedRectangle (12.0f, 58.0f, (float) getWidth() - 24.0f, 168.0f, 10.0f);   // exciter
    g.fillRoundedRectangle (12.0f, 234.0f, (float) getWidth() - 24.0f, 168.0f, 10.0f);  // resonator + pitch
    g.fillRoundedRectangle (12.0f, 410.0f, (float) getWidth() - 24.0f, 138.0f, 10.0f);  // choices + buttons

    g.setColour (kAccent);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText ("EXCITER", 24, 62, 120, 14, juce::Justification::centredLeft);
    g.drawText ("RESONATOR / PITCH / PERFORMANCE", 24, 238, 320, 14, juce::Justification::centredLeft);
}

void ElementsEditor::resized()
{
    const int W = getWidth();
    auto place = [&] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16));
        k.slider.setBounds (r);
    };

    Knob* exciterRow[9] = { &contour, &bowLevel, &bowTimbre, &blowLevel, &blowMeta, &blowTimbre, &strikeLevel, &strikeMeta, &strikeTimbre };
    const int colE = (W - 24) / 9;
    for (int i = 0; i < 9; ++i)
        place (*exciterRow[i], juce::Rectangle<int> (12 + i * colE, 80, colE, 142).reduced (3, 0));

    Knob* resRow[8] = { &geometry, &brightness, &damping, &position, &space, &transpose, &fine, &strength };
    const int colR = (W - 24) / 8;
    for (int i = 0; i < 8; ++i)
        place (*resRow[i], juce::Rectangle<int> (12 + i * colR, 256, colR, 142).reduced (6, 0));

    auto placeChoice = [&] (Choice& c, juce::Rectangle<int> r)
    {
        c.label.setBounds (r.removeFromTop (16));
        c.box.setBounds (r.removeFromTop (28));
    };
    placeChoice (model, juce::Rectangle<int> (24, 420, 220, 48));
    placeChoice (route, juce::Rectangle<int> (260, 420, 220, 48));
    placeChoice (pitchSnap, juce::Rectangle<int> (496, 420, 180, 48));

    gateBtn.button.setBounds     (24, 488, 90, 36);
    velStrength.button.setBounds (124, 488, 170, 36);
    freeze.button.setBounds      (304, 488, 130, 36);
    ominous.button.setBounds     (444, 488, 130, 36);
    monoOut.button.setBounds     (584, 488, 100, 36);
}
