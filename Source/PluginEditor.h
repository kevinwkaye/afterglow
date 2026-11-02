#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Flip Fella palette, shared across all of Kevin's builds.
namespace ff
{
const juce::Colour bg        { 0xff050505 };
const juce::Colour face      { 0xff0d0d0d };
const juce::Colour housing   { 0xff060606 };
const juce::Colour card      { 0xff1b1b1b };
const juce::Colour cardHi    { 0xff252525 };
const juce::Colour cardLow   { 0xff141414 };
const juce::Colour ink       { 0xfff3f0e8 };
const juce::Colour dim       { 0xffa29e95 };
const juce::Colour faint     { 0xff5a5852 };
const juce::Colour line      { 0xff3a3a3a };
const juce::Colour ring      { 0xff1a1a1a };
const juce::Colour pin       { 0xff3c3c3c };
const juce::Colour red       { 0xffe8392f };
const juce::Colour brass     { 0xffd4a537 };
const juce::Colour piano     { 0xffe8b923 }; // real piano and piano group
const juce::Colour sost      { 0xff4a90e2 }; // sostenuto
const juce::Colour blob      { 0xff4d5bff };
const juce::Colour blobShade { 0xff2329a8 };
const juce::Colour eye       { 0xffffffff };
const juce::Colour faceInk   { 0xff0b0b12 };
const juce::Colour blush     { 0xfff28c8c };

juce::Font font (float height, float kerning = 0.04f);
void label (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r,
            juce::Justification j = juce::Justification::centredLeft, juce::Colour c = dim, float size = 13.0f);
} // namespace ff

namespace ui
{
// A split-flap card that flips like the Karlsson clock when its value changes.
class FlipCard : public juce::Component
{
public:
    void setValue (const juce::String& v);
    void setTextHeight (float h) { textHeight = h; }
    void advance (double dt);
    void paint (juce::Graphics&) override;

private:
    void drawHalf (juce::Graphics&, juce::Rectangle<float> card, const juce::String& text, bool top, float scale, bool shade);
    juce::String current, previous;
    float progress = 1.0f;
    float textHeight = 42.0f;
};

// Rounded pill buttons bound to a choice parameter, in one row or a grid.
class Pills : public juce::Component
{
public:
    Pills (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels, float textSize = 14.0f, int columns = 0);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray labels;
    int selected = 0, columns = 0;
    float textSize;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A single pill that turns ink-white when its switch is on.
class ToggleChip : public juce::Component
{
public:
    ToggleChip (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String text;
    float textSize;
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// WET / LATCH / HIDE: pick a mode and tap it again to engage, like the PLUS II footswitch.
class FootswitchPills : public juce::Component
{
public:
    explicit FootswitchPills (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int i) const;
    juce::AudioProcessorValueTreeState& state;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment, onAttachment;
    int mode = 0;
    bool on = false;
};

class FlipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
};

// A knob drawn as a small clock face, with its name above and value below.
class Dial : public juce::Component
{
public:
    Dial (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, bool redHand = false);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption;
    bool red = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// The hero control: LAYERS 1 to 10 and infinite. Its ticks are the pedal's LED ring.
class LayersDial : public juce::Component
{
public:
    explicit LayersDial (AfterglowProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Point<float> centre() const;
    float scale() const;
    int stopAt (juce::Point<float>) const;
    int currentStop() const;
    void pick (int stop);
    AfterglowProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    float dragStart = 0.0f;
};

// Ten bars, one per layer slot, moving with each layer's level.
class LayerLanes : public juce::Component
{
public:
    explicit LayerLanes (AfterglowProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    AfterglowProcessor& proc;
};

// Lights up when HOLD NONE and TONE fully down open a piano mode.
class PianoBox : public juce::Component
{
public:
    explicit PianoBox (AfterglowProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    AfterglowProcessor& proc;
};

// The brass pedal: click and hold to sustain, drag up to ease off, two quick
// clicks clear. Shows the real depth, whether from here, MIDI or the latch.
class BrassPedal : public juce::Component
{
public:
    explicit BrassPedal (AfterglowProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void set (float v);
    AfterglowProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    bool held = false;
};

class LearnButton : public juce::Component
{
public:
    explicit LearnButton (AfterglowProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.startLearn(); }

private:
    AfterglowProcessor& proc;
};

// A / LIVE / B. Hold A or B to save into it.
class PresetToggle : public juce::Component, private juce::Timer
{
public:
    explicit PresetToggle (AfterglowProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> cell (int i) const;
    AfterglowProcessor& proc;
    int pressedCell = -1;
    double downAt = 0, savedFlashUntil = 0;
    int savedCell = -1;
    bool saved = false;
};

class BypassButton : public juce::Component
{
public:
    explicit BypassButton (AfterglowProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool bypassed = false;
};

// The bottom bar: Pebble and what the engine is doing.
class PebbleStrip : public juce::Component
{
public:
    explicit PebbleStrip (AfterglowProcessor& p) : proc (p) {}
    void update (double now, double dt);
    void paint (juce::Graphics&) override;

private:
    void paintPebble (juce::Graphics&, juce::Rectangle<float> area);
    AfterglowProcessor& proc;
    double now = 0, lastSound = 0, nextBlink = 2.0, blinkUntil = 0, catchUntil = 0, clearUntil = 0, clipUntil = 0;
    int lastCaptures = 0, lastClears = 0, active = 0, rising = 0, held = 0, mode = 0;
    float hop = 0, level = 0;
    bool asleep = false, blinking = false, down = false, infinite = false, bypassed = false;
    juce::String status, detail;
};
} // namespace ui

class AfterglowEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AfterglowEditor (AfterglowProcessor&);
    ~AfterglowEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void section (juce::Graphics&, juce::Rectangle<int>) const;

    AfterglowProcessor& proc;
    ui::FlipLookAndFeel lnf;
    double lastTick = 0;

    // header
    ui::FootswitchPills footswitch;
    ui::ToggleChip killDry;
    ui::PresetToggle presets;

    // layers
    ui::FlipCard stateCard, countCard;
    ui::LayersDial layersDial;
    ui::Pills catchMode;

    // envelope
    ui::Dial rise, decay;
    ui::Pills holdMode;
    ui::LayerLanes lanes;

    // tone + resonance
    ui::Dial tone, resonance, level, input;
    ui::PianoBox pianoBox;

    // fx loop
    ui::FlipCard fxCard;
    ui::Pills fxPos;
    ui::Dial fxAmount, fxMix;
    ui::Pills fxType;

    // bottom bar
    ui::PebbleStrip strip;
    ui::BrassPedal pedal;
    ui::LearnButton learn;
    juce::TextButton clear { "CLEAR" };
    ui::BypassButton bypass;

    juce::String fxMeaning, holdMeaning;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterglowEditor)
};
