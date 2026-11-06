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
const juce::Colour teal      { 0xff3fd6c8 };
const juce::Colour coral     { 0xffff7a59 };
const juce::Colour amber     { 0xfff2c230 };
const juce::Colour brass     { 0xffd4a537 };
const juce::Colour piano     { 0xffe8b923 }; // real piano and piano group
const juce::Colour sost      { 0xff4a90e2 }; // sostenuto
const juce::Colour blob      { 0xff4d5bff };
const juce::Colour blobShade { 0xff2329a8 };
const juce::Colour eye       { 0xffffffff };
const juce::Colour faceInk   { 0xff0b0b12 };
const juce::Colour blush     { 0xfff28c8c };
// the four preset banks
const juce::Colour bankR     { 0xffe8392f };
const juce::Colour bankY     { 0xfff2c230 };
const juce::Colour bankG     { 0xff4fcf6a };
const juce::Colour bankB     { 0xff4d8dff };

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
    void mouseDown (const juce::MouseEvent& e) override { if (onClick) onClick (e); }
    std::function<void (const juce::MouseEvent&)> onClick;

private:
    void drawHalf (juce::Graphics&, juce::Rectangle<float> card, const juce::String& text, bool top, float scale, bool shade);
    juce::String current, previous;
    float progress = 1.0f;
    float textHeight = 34.0f;
};

// Rounded pill buttons bound to a choice parameter.
class Pills : public juce::Component
{
public:
    Pills (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray labels;
    int selected = 0;
    float textSize;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A single pill that turns ink-white when its switch is on. It can show different words on and off.
class ToggleChip : public juce::Component
{
public:
    ToggleChip (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text,
                const juce::String& onText = {}, bool fillWhenOn = true, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isOn() const { return on; }

private:
    juce::String text, onText;
    bool fill;
    float textSize;
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A plain pill button that runs a function.
class ActionChip : public juce::Component
{
public:
    ActionChip (const juce::String& t, std::function<void()> fn, float textSize = 12.0f)
        : text (t), action (std::move (fn)), size (textSize) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override { if (action) action(); }
    void setLit (bool l) { if (l != lit) { lit = l; repaint(); } }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }

private:
    juce::String text;
    std::function<void()> action;
    float size;
    bool lit = false;
};

class FlipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// A knob drawn as a small clock face, with its name above and value below.
class Dial : public juce::Component
{
public:
    Dial (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, bool redHand = false, bool secondary = false);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption;
    bool secondary = false;
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

// The brass pedal: click and hold to sustain, drag up to ease off. Shows the real
// depth, whichever source is pressing it. Right-click to teach it a key or a strip.
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
    void menu();
    AfterglowProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    bool held = false;
};

// The Bypass footswitch.
class BypassSwitch : public juce::Component
{
public:
    explicit BypassSwitch (AfterglowProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Sixteen user presets in red, yellow, green and blue banks.
class PresetBanks : public juce::Component, private juce::Timer
{
public:
    explicit PresetBanks (AfterglowProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> cell (int i) const;
    AfterglowProcessor& proc;
    int pressed = -1, flashSlot = -1;
    double downAt = 0, flashUntil = 0;
    bool saved = false;
};

enum class View { Main, Pedal };

// One footswitch on the Pedal page: its jobs, what it listens for, Learn and Defaults.
class FootswitchCard : public juce::Component
{
public:
    FootswitchCard (AfterglowProcessor&, int sw);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    AfterglowProcessor& proc;
    int sw;
    FlipCard letter;
    ActionChip learn, reset;
    std::unique_ptr<ToggleChip> sustain;
    juce::String sends;
};

// The Keep button: two lines, centred, and a right-click menu.
class BigButton : public juce::Component
{
public:
    std::function<void()> onDown, onRightClick;
    void set (const juce::String& main, const juce::String& small, juce::Colour border);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); }

private:
    juce::String mainText, smallText;
    juce::Colour borderColour = ff::line;
    bool pressed = false;
};

// Small A to D squares that act exactly like the footswitches, and light when one is pressed.
class MiniFoot : public juce::Component
{
public:
    MiniFoot (AfterglowProcessor& p, int s) : proc (p), sw (s) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.footDown (sw); }
    void mouseUp (const juce::MouseEvent&) override { proc.footUp (sw); }

private:
    AfterglowProcessor& proc;
    int sw;
};

// "FOOTSWITCHES": a dot that lights when the footswitch is heard, and a menu for where it's read from.
class FootInputLabel : public juce::Component
{
public:
    explicit FootInputLabel (AfterglowProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    AfterglowProcessor& proc;
};

// The expression pedal's live position. Click for its target, Learn, Calibrate and the pedal's MIDI map.
class ExpressionMeter : public juce::Component
{
public:
    explicit ExpressionMeter (AfterglowProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    AfterglowProcessor& proc;
};

// Pebble from Flip Fella. Eyes pop when a layer is caught, it holds its breath while
// layers are held, rings stack above it as layers build, spiral eyes at infinity, a halo
// in the piano modes, startled when cleared. Taps its foot on the footswitches, leans
// with the expression pedal and nods for Keeper.
class Pebble : public juce::Component
{
public:
    explicit Pebble (AfterglowProcessor& p) : proc (p) {}
    void update (double now, double dt);
    void paint (juce::Graphics&) override;
    void nod() { nodding = 1.0f; }

private:
    AfterglowProcessor& proc;
    double now = 0, lastSound = 0, nextBlink = 2.0, blinkUntil = 0, catchUntil = 0, clearUntil = 0, clipUntil = 0;
    int lastCaptures = 0, lastClears = 0, lastPulse = 0, active = 0, held = 0, mode = 0;
    float hop = 0, lean = 0, nodding = 0, level = 0;
    bool asleep = false, blinking = false, down = false, infinite = false;
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
    void setView (ui::View);
    void keeperMenu();
    void closeNote (bool save);

    AfterglowProcessor& proc;
    ui::FlipLookAndFeel lnf;
    double lastTick = 0;
    ui::View view = ui::View::Main;
    std::vector<juce::Component*> mainPage;

    // header
    ui::Pills inputMode;
    ui::ActionChip viewMain, viewPedal;

    // pedal page
    std::array<std::unique_ptr<ui::FootswitchCard>, 4> footCards;

    // layers
    ui::FlipCard stateCard, countCard;
    ui::LayersDial layersDial;
    ui::Pills catchMode;
    ui::ActionChip clear;

    // shape
    ui::Dial rise, decay, tone, resonance;
    ui::Pills holdMode;

    // level and lanes
    ui::Dial level, input;
    ui::LayerLanes lanes;

    // fx loop
    ui::FlipCard fxCard;
    ui::Pills fxType, fxPos;
    ui::Dial fxAmount, fxMix;
    ui::ToggleChip killDry, auxOn;
    ui::Pills auxMode;

    // strip
    ui::Pebble pebble;
    ui::BrassPedal pedal;
    ui::PresetBanks banks;
    std::array<std::unique_ptr<ui::MiniFoot>, 4> minis;
    ui::FootInputLabel footInput;
    ui::ExpressionMeter expression;
    ui::BypassSwitch bypass;

    // Keeper
    ui::BigButton keepButton;
    ui::FlipCard contextCard;
    juce::TextEditor noteEditor;
    int noteNumber = 0, lastKeepPulse = 0, lastMode = -1;
    double keptUntil = 0.0, noteUntil = 0.0;

    juce::String holdHint, posHint, auxHint, mood, moodSub;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterglowEditor)
};
