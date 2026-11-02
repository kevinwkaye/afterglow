#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

// =====================================================================================
// Palette helpers
// =====================================================================================
namespace ff
{
Font font (float height, float kerning)
{
    static Typeface::Ptr typeface = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf,
                                                                       BinaryData::RobotoCondensedBold_ttfSize);
    return Font (FontOptions (typeface).withHeight (height).withKerningFactor (kerning));
}

void label (Graphics& g, const String& text, Rectangle<int> r, Justification j, Colour c, float size)
{
    g.setColour (c);
    g.setFont (font (size, 0.08f));
    g.drawText (text, r, j, false);
}
} // namespace ff

namespace ui
{
static double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }
static const String dot = String::fromUTF8 (" \xc2\xb7 ");

static void setParam (RangedAudioParameter* p, float plain)
{
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
    p->endChangeGesture();
}

static float plainOf (const RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }

static Colour modeColour (int mode)
{
    if (mode == ag::ModeSostenuto) return ff::sost;
    if (mode != ag::ModeNormal) return ff::piano;
    return ff::ink;
}

// =====================================================================================
// FlipCard
// =====================================================================================
void FlipCard::setValue (const String& v)
{
    if (v == current) return;
    previous = current;
    current = v;
    progress = previous.isEmpty() ? 1.0f : 0.0f;
    repaint();
}

void FlipCard::advance (double dt)
{
    if (progress < 1.0f)
    {
        progress = jmin (1.0f, progress + (float) (dt / 0.26));
        repaint();
    }
}

void FlipCard::drawHalf (Graphics& g, Rectangle<float> card, const String& text, bool top, float scale, bool shade)
{
    const float hinge = card.getCentreY();
    const float halfH = card.getHeight() * 0.5f;
    if (scale <= 0.001f) return;

    Graphics::ScopedSaveState save (g);
    auto clip = top ? Rectangle<float> (card.getX(), hinge - halfH * scale, card.getWidth(), halfH * scale)
                    : Rectangle<float> (card.getX(), hinge, card.getWidth(), halfH * scale);
    g.reduceClipRegion (clip.getSmallestIntegerContainer());
    g.addTransform (AffineTransform::scale (1.0f, scale, 0.0f, hinge));

    if (top) g.setGradientFill (ColourGradient (ff::cardHi, 0, card.getY(), ff::card, 0, hinge, false));
    else     g.setGradientFill (ColourGradient (ff::card, 0, hinge, ff::cardLow, 0, card.getBottom(), false));
    g.fillRoundedRectangle (card, 7.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (textHeight, 0.02f));
    g.drawText (text, card, Justification::centred, false);

    if (shade)
    {
        g.setColour (Colours::black.withAlpha (0.45f * (1.0f - scale)));
        g.fillRoundedRectangle (card, 7.0f);
    }
}

void FlipCard::paint (Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour (ff::housing);
    g.fillRoundedRectangle (outer, 10.0f);
    auto card = outer.reduced (6.0f);

    if (progress >= 1.0f)
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, current, false, 1.0f, false);
    }
    else
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, previous, false, 1.0f, false);
        const float eased = 1.0f - std::pow (1.0f - progress, 2.0f);
        if (eased < 0.5f) drawHalf (g, card, previous, true, 1.0f - eased * 2.0f, true);
        else              drawHalf (g, card, current, false, (eased - 0.5f) * 2.0f, true);
    }

    g.setColour (Colours::black);
    g.fillRect (card.getX(), card.getCentreY() - 1.5f, card.getWidth(), 3.0f);
    g.setColour (ff::pin);
    g.fillRoundedRectangle (outer.getX() + 1.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
    g.fillRoundedRectangle (outer.getRight() - 4.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
}

// =====================================================================================
// Pills and chips
// =====================================================================================
static void drawPill (Graphics& g, Rectangle<float> b, const String& text, bool on, float textSize)
{
    const float r = b.getHeight() * 0.5f;
    if (on)
    {
        g.setColour (ff::ink);
        g.fillRoundedRectangle (b, r);
    }
    else
    {
        g.setColour (ff::line);
        g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f);
    }
    g.setColour (on ? ff::bg : ff::ink);
    g.setFont (ff::font (textSize, 0.06f));
    g.drawText (text, b, Justification::centred, false);
}

Pills::Pills (AudioProcessorValueTreeState& s, const String& paramId, StringArray l, float size, int cols)
    : labels (std::move (l)), columns (cols), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v)
    {
        selected = (int) std::lround (v);
        repaint();
    });
    attachment->sendInitialUpdate();
}

Rectangle<float> Pills::cell (int i) const
{
    const float gap = 8.0f;
    const int cols = columns > 0 ? columns : labels.size();
    const int rows = (labels.size() + cols - 1) / cols;
    const float w = ((float) getWidth() - gap * (float) (cols - 1)) / (float) cols;
    const float h = ((float) getHeight() - gap * (float) (rows - 1)) / (float) rows;
    return { (float) (i % cols) * (w + gap), (float) (i / cols) * (h + gap), w, h };
}

void Pills::paint (Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
        drawPill (g, cell (i).reduced (1.0f), labels[i], i == selected, textSize);
}

void Pills::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position))
        {
            attachment->setValueAsCompleteGesture ((float) i);
            return;
        }
}

ToggleChip::ToggleChip (AudioProcessorValueTreeState& s, const String& paramId, const String& t, float size)
    : text (t), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void ToggleChip::paint (Graphics& g)
{
    drawPill (g, getLocalBounds().toFloat().reduced (1.0f), text, on, textSize);
}

void ToggleChip::mouseDown (const MouseEvent&)
{
    attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

FootswitchPills::FootswitchPills (AudioProcessorValueTreeState& s) : state (s)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    modeAttachment = std::make_unique<ParameterAttachment> (*s.getParameter (ParamIDs::fsMode), [this] (float v) { mode = (int) std::lround (v); repaint(); });
    onAttachment = std::make_unique<ParameterAttachment> (*s.getParameter (ParamIDs::fsOn), [this] (float v) { on = v > 0.5f; repaint(); });
    modeAttachment->sendInitialUpdate();
    onAttachment->sendInitialUpdate();
}

Rectangle<float> FootswitchPills::cell (int i) const
{
    const float gap = 8.0f;
    const float w = ((float) getWidth() - gap * 2.0f) / 3.0f;
    return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
}

void FootswitchPills::paint (Graphics& g)
{
    static const char* names[3] = { "WET", "LATCH", "HIDE" };
    for (int i = 0; i < 3; ++i)
    {
        auto b = cell (i).reduced (1.0f);
        const bool sel = i == mode;
        drawPill (g, b, names[i], sel && on, 14.0f);
        if (sel && ! on)
        {
            // chosen but not engaged: brighter ring and a dark LED
            g.setColour (ff::ink);
            g.drawRoundedRectangle (b.reduced (1.0f), b.getHeight() * 0.5f, 2.0f);
            g.setColour (ff::pin);
            g.fillEllipse (b.getX() + 10.0f, b.getCentreY() - 3.0f, 6.0f, 6.0f);
        }
        else if (sel)
        {
            g.setColour (ff::red);
            g.fillEllipse (b.getX() + 10.0f, b.getCentreY() - 3.0f, 6.0f, 6.0f);
        }
    }
}

void FootswitchPills::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < 3; ++i)
        if (cell (i).contains (e.position))
        {
            if (i == mode)
                onAttachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
            else
            {
                modeAttachment->setValueAsCompleteGesture ((float) i);
                onAttachment->setValueAsCompleteGesture (1.0f);
            }
            return;
        }
}

// =====================================================================================
// Look and feel
// =====================================================================================
void FlipLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const float size = (float) jmin (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float r = size * 0.5f - 1.0f;

    g.setColour (ff::ring);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - r * 0.9f, c.y - r * 0.9f, r * 1.8f, r * 1.8f);

    g.setColour (ff::ink);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        const Point<float> p1 (c.x + std::sin (a) * r * 0.80f, c.y - std::cos (a) * r * 0.80f);
        const Point<float> p2 (c.x + std::sin (a) * r * 0.92f, c.y - std::cos (a) * r * 0.92f);
        g.drawLine ({ p1, p2 }, i == 5 && (bool) s.getProperties()["bipolar"] ? 3.5f : 2.0f);
    }

    const bool red = s.getProperties()["red"];
    g.setColour (red ? ff::red : ff::ink);
    const float a = start + pos * (end - start);
    Path hand;
    hand.addRoundedRectangle (-3.0f, -r * 0.68f, 6.0f, r * 0.68f + r * 0.15f, 2.0f);
    g.fillPath (hand, AffineTransform::rotation (a).translated (c));
    g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 1.8f, c.y - 1.8f, 3.6f, 3.6f);
}

void FlipLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const float rad = r.getHeight() * 0.5f;
    g.setColour (down ? ff::card : ff::face);
    g.fillRoundedRectangle (r, rad);
    g.setColour (highlighted || down ? ff::ink : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), rad, 2.0f);
}

void FlipLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setColour (ff::ink);
    g.setFont (ff::font (14.0f, 0.08f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred, false);
}

// =====================================================================================
// Dial
// =====================================================================================
Dial::Dial (AudioProcessorValueTreeState& s, const String& paramId, const String& c, bool redHand) : caption (c), red (redHand)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.getProperties().set ("red", red);
    slider.getProperties().set ("bipolar", paramId == ParamIDs::resonance);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.onValueChange = [this] { repaint(); };
}

void Dial::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (20).withTrimmedBottom (24).withSizeKeepingCentre (80, 80));
}

void Dial::paint (Graphics& g)
{
    auto r = getLocalBounds();
    ff::label (g, caption.toUpperCase(), r.removeFromTop (18), Justification::centred);
    ff::label (g, param->getCurrentValueAsText().toUpperCase(), r.removeFromBottom (20), Justification::centred, ff::ink, 15.0f);
}

// =====================================================================================
// LayersDial: 1 to 10 and infinite round a Karlsson face, grouped Clean, Chords, Wall
// =====================================================================================
static constexpr float kStopStep = 27.0f;
static float stopDegrees (int i) { return -135.0f + kStopStep * (float) i; }

LayersDial::LayersDial (AfterglowProcessor& p) : proc (p)
{
    param = p.apvts.getParameter (ParamIDs::layers);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

Point<float> LayersDial::centre() const { return { getWidth() * 0.5f, getHeight() * 152.0f / 290.0f }; }
float LayersDial::scale() const { return jmin ((float) getWidth() / 316.0f, (float) getHeight() / 290.0f); }
int LayersDial::currentStop() const { return jlimit (0, 10, roundToInt (plainOf (param))); }

void LayersDial::paint (Graphics& g)
{
    const float s = scale();
    const auto c = centre();
    auto at = [&] (float deg, float r) { const float a = degreesToRadians (deg); return Point<float> (c.x + std::sin (a) * r * s, c.y - std::cos (a) * r * s); };

    const auto& eng = proc.getEngine();
    const int stop = currentStop();
    const int mode = eng.uiMode.load();
    const Colour led = modeColour (mode);

    // group arcs round the rim
    struct Arc { const char* name; int a, b; float labelDeg; Justification j; };
    const Arc arcs[3] = { { "CLEAN", 0, 2, -108.0f, Justification::centredRight },
                          { "CHORDS", 3, 6, -14.0f, Justification::centred },
                          { "WALL", 7, 9, 85.0f, Justification::centredLeft } };
    for (const auto& arc : arcs)
    {
        const bool lit = stop >= arc.a && stop <= arc.b;
        Path p;
        p.addCentredArc (c.x, c.y, 116.0f * s, 116.0f * s, 0.0f, degreesToRadians (stopDegrees (arc.a) - 9.0f),
                         degreesToRadians (stopDegrees (arc.b) + 9.0f), true);
        g.setColour (lit ? ff::ink : ff::line);
        g.strokePath (p, PathStrokeType (3.0f * s, PathStrokeType::curved, PathStrokeType::rounded));

        const auto lp = at (arc.labelDeg, 134.0f);
        Rectangle<float> box (lp.x - 40 * s, lp.y - 9 * s, 80 * s, 18 * s);
        if (arc.j == Justification::centredRight) box = box.withX (lp.x - 72 * s);
        if (arc.j == Justification::centredLeft) box = box.withX (lp.x - 8 * s);
        g.setColour (lit ? ff::ink : ff::dim);
        g.setFont (ff::font (13.0f * s, 0.08f));
        g.drawText (arc.name, box, arc.j, false);
    }

    g.setColour (mode == ag::ModeNormal ? ff::ring : led.withAlpha (0.35f));
    g.fillEllipse (c.x - 106 * s, c.y - 106 * s, 212 * s, 212 * s);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 100 * s, c.y - 100 * s, 200 * s, 200 * s);

    // the ticks are the pedal's LED ring: each layer slot lights with its level
    const int slots = stop == 10 ? 10 : stop + 1;
    for (int i = 0; i < 11; ++i)
    {
        const float deg = stopDegrees (i);
        float lvl = 0.0f;
        if (i < 10) lvl = jlimit (0.0f, 1.0f, eng.uiLevel[i].load() * 1.4f);
        else if (stop == 10 && eng.uiActive.load() >= 10) lvl = 0.6f + 0.4f * (float) std::sin (nowSeconds() * 4.0);
        const bool inUse = i < slots || (i == 10 && stop == 10);
        const Colour base = inUse ? ff::faint : ff::line.darker (0.4f);
        const bool heldSlot = i < 10 && eng.uiState[i].load() == 2;
        g.setColour (base.interpolatedWith (heldSlot && mode == ag::ModeNormal ? ff::ink : led, lvl));
        g.drawLine (Line<float> (at (deg, 90), at (deg, 102)), (4.0f + 2.0f * lvl) * s);

        const auto lp = at (deg, 74);
        g.setColour (i == stop ? ff::ink : ff::dim);
        g.setFont (ff::font ((i == 10 ? 22.0f : 15.0f) * s, 0.0f));
        g.drawText (i == 10 ? String::fromUTF8 ("\xe2\x88\x9e") : String (i + 1), Rectangle<float> (lp.x - 16 * s, lp.y - 10 * s, 32 * s, 20 * s),
                    Justification::centred, false);
    }

    Path needle;
    needle.addRectangle (-1.4f * s, -60.0f * s, 2.8f * s, 78.0f * s);
    g.setColour (ff::red);
    g.fillPath (needle, AffineTransform::rotation (degreesToRadians (stopDegrees (stop))).translated (c));
    g.fillEllipse (c.x - 7 * s, c.y - 7 * s, 14 * s, 14 * s);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 2.6f * s, c.y - 2.6f * s, 5.2f * s, 5.2f * s);
}

int LayersDial::stopAt (Point<float> p) const
{
    const auto c = centre();
    const float dist = p.getDistanceFrom (c) / scale();
    if (dist < 30.0f || dist > 150.0f) return -1;
    const float deg = radiansToDegrees (std::atan2 (p.x - c.x, c.y - p.y));
    const int i = roundToInt ((deg + 135.0f) / kStopStep);
    return (i >= 0 && i <= 10) ? i : -1;
}

void LayersDial::pick (int stop)
{
    if (stop < 0) return;
    setParam (param, (float) stop);
    repaint();
}

void LayersDial::mouseDown (const MouseEvent& e)
{
    dragStart = (float) currentStop();
    pick (stopAt (e.position));
}

void LayersDial::mouseDrag (const MouseEvent& e)
{
    const int stop = jlimit (0, 10, roundToInt (dragStart - (float) e.getDistanceFromDragStartY() / 20.0f));
    if (std::abs (e.getDistanceFromDragStartY()) > 6 && stop != currentStop())
        pick (stop);
}

void LayersDial::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    pick (jlimit (0, 10, currentStop() + (w.deltaY > 0 ? 1 : -1)));
}

// =====================================================================================
// Layer lanes and piano box
// =====================================================================================
void LayerLanes::paint (Graphics& g)
{
    auto r = getLocalBounds();
    ff::label (g, "LAYER LANES", r.removeFromTop (18));
    auto caption = r.removeFromBottom (22);
    r.removeFromTop (4);
    auto box = r.toFloat();
    g.setColour (ff::housing);
    g.fillRoundedRectangle (box, 10.0f);

    const auto& eng = proc.getEngine();
    const int mode = eng.uiMode.load();
    const int stop = jlimit (0, 10, (int) std::lround (proc.apvts.getRawParameterValue (ParamIDs::layers)->load()));
    const int slots = stop == 10 ? 10 : stop + 1;
    auto inner = box.reduced (12.0f, 12.0f);
    const float gap = 6.0f;
    const float w = (inner.getWidth() - gap * 9.0f) / 10.0f;
    for (int i = 0; i < 10; ++i)
    {
        const float lvl = jlimit (0.0f, 1.0f, eng.uiLevel[i].load() * 1.3f);
        const int st = eng.uiState[i].load();
        const float h = jmax (4.0f, lvl * inner.getHeight());
        Rectangle<float> bar (inner.getX() + (float) i * (w + gap), inner.getBottom() - h, w, h);
        Colour col = i < slots ? ff::line : ff::line.darker (0.6f);
        if (st == 2) col = mode == ag::ModeNormal ? ff::red : modeColour (mode);
        else if (st == 1) col = ff::ink;
        else if (st == 3) col = ff::dim;
        g.setColour (col);
        g.fillRoundedRectangle (bar, 3.0f);
    }

    const int hold = (int) std::lround (proc.apvts.getRawParameterValue (ParamIDs::holdMode)->load());
    static const char* meanings[3] = { "EVERY LAYER RISES THEN FADES", "FIRST HELD, OTHERS RISE THEN FADE", "ALL HELD WHILE THE PEDAL IS DOWN" };
    ff::label (g, mode != ag::ModeNormal ? String ("DAMPERS FALL WHEN YOU LIFT") : String (meanings[jlimit (0, 2, hold)]),
               caption, Justification::centred, ff::faint, 12.0f);
}

void PianoBox::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const int mode = proc.getEngine().uiMode.load();
    const bool active = mode != ag::ModeNormal;
    g.setColour (ff::housing);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (active ? modeColour (mode) : ff::line);
    g.drawRoundedRectangle (b.reduced (1.0f), 10.0f, 2.0f);

    auto r = b.reduced (14.0f, 10.0f).toNearestInt();
    static const char* names[4] = { "PIANO MODES", "REAL PIANO", "PIANO GROUP", "PIANO SOSTENUTO" };
    ff::label (g, names[jlimit (0, 3, mode)], r.removeFromTop (24), Justification::centredLeft, active ? modeColour (mode) : ff::ink, 18.0f);
    if (active)
    {
        static const char* what[4] = { "", "NEW NOTES RING, LIFT TO DAMP", "A SET NUMBER OF NOTES RING", "HOLDS A CHORD THAT RINGS AGAIN" };
        ff::label (g, what[mode], r.removeFromTop (20), Justification::centredLeft, ff::dim, 13.0f);
        ff::label (g, "TONE SETS THE DAMPER SPEED", r.removeFromTop (20), Justification::centredLeft, ff::faint, 12.0f);
    }
    else
    {
        ff::label (g, "HOLD NONE + TONE FULLY DOWN", r.removeFromTop (20), Justification::centredLeft, ff::dim, 13.0f);
        ff::label (g, "CATCH PICKS REAL, GROUP OR SOSTENUTO", r.removeFromTop (20), Justification::centredLeft, ff::faint, 12.0f);
    }
}

// =====================================================================================
// Brass pedal, learn, presets, bypass
// =====================================================================================
BrassPedal::BrassPedal (AfterglowProcessor& p) : proc (p)
{
    param = p.apvts.getParameter (ParamIDs::pedal);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void BrassPedal::set (float v)
{
    param->setValueNotifyingHost (param->convertTo0to1 (jlimit (0.0f, 1.0f, v)));
}

void BrassPedal::mouseDown (const MouseEvent&)
{
    held = true;
    param->beginChangeGesture();
    set (1.0f);
    repaint();
}

void BrassPedal::mouseDrag (const MouseEvent& e)
{
    // drag up to ease your foot off, back down to press in again
    set (1.0f + (float) jmin (0, e.getDistanceFromDragStartY()) / 150.0f);
}

void BrassPedal::mouseUp (const MouseEvent&)
{
    held = false;
    set (0.0f);
    param->endChangeGesture();
    repaint();
}

void BrassPedal::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto& eng = proc.getEngine();
    const bool latched = eng.uiLatched.load();
    const bool down = eng.uiDown.load();
    const float depth = jlimit (0.0f, 1.0f, eng.uiDepth.load());

    g.setColour (held ? ff::card : ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (down ? ff::brass : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);

    auto inner = r.reduced (16.0f, 9.0f);
    auto top = inner.removeFromTop (20.0f).toNearestInt();
    ff::label (g, "BRASS PEDAL", top, Justification::centredLeft, ff::ink, 17.0f);
    ff::label (g, latched ? String ("LATCHED") : String (roundToInt (depth * 100.0f)) + "%", top, Justification::centredRight, down ? ff::brass : ff::dim, 17.0f);

    inner.removeFromTop (6.0f);
    auto track = inner.removeFromTop (12.0f);
    g.setColour (ff::line);
    g.fillRoundedRectangle (track, 6.0f);
    if (depth > 0.0f)
    {
        g.setColour (ff::brass);
        g.fillRoundedRectangle (track.withWidth (jmax (12.0f, track.getWidth() * depth)), 6.0f);
    }

    inner.removeFromTop (5.0f);
    ff::label (g, "CLICK AND HOLD" + dot + "DRAG UP TO EASE OFF" + dot + "DOUBLE TAP CLEARS" + dot + proc.pedalSources(),
               inner.toNearestInt(), Justification::centredLeft, ff::faint, 11.0f);
}

void LearnButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float r = b.getHeight() * 0.5f;
    const int st = proc.learnState();
    const String text = st == 1 ? "LISTENING" : (st == 2 ? "LEARNED" : "LEARN");
    g.setColour (st == 1 ? ff::ink : ff::line);
    g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f);
    const float pulse = st == 1 ? 0.4f + 0.6f * (float) std::abs (std::sin (nowSeconds() * 5.0)) : 1.0f;
    g.setColour (ff::red.withAlpha (pulse));
    const float tw = GlyphArrangement::getStringWidth (ff::font (14.0f, 0.08f), text);
    g.fillEllipse (b.getCentreX() - tw * 0.5f - 10.0f, b.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (14.0f, 0.08f));
    g.drawText (text, b.withTrimmedLeft (12.0f), Justification::centred, false);
}

PresetToggle::PresetToggle (AfterglowProcessor& p) : proc (p)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

Rectangle<float> PresetToggle::cell (int i) const
{
    const float w[3] = { 40.0f, 52.0f, 40.0f };
    float x = 0;
    for (int k = 0; k < i; ++k) x += w[k] + 4.0f;
    return { x, 0.0f, w[i], (float) getHeight() - 8.0f };
}

void PresetToggle::paint (Graphics& g)
{
    const int sel = (int) std::lround (proc.apvts.getRawParameterValue (ParamIDs::presetSlot)->load());
    static const char* labels[3] = { "A", "LIVE", "B" };
    for (int i = 0; i < 3; ++i)
    {
        auto b = cell (i);
        const bool on = i == sel;
        const bool flash = i == savedCell && nowSeconds() < savedFlashUntil;
        g.setColour (flash ? ff::red : (on ? ff::ink : ff::card));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (Colours::black.withAlpha (on ? 0.3f : 0.85f));
        g.fillRect (b.getX(), b.getCentreY() - 0.75f, 7.0f, 1.5f);
        g.fillRect (b.getRight() - 7.0f, b.getCentreY() - 0.75f, 7.0f, 1.5f);
        g.setColour (on && ! flash ? ff::bg : ff::ink);
        g.setFont (ff::font (i == 1 ? 13.0f : 15.0f, 0.06f));
        g.drawText (flash ? "SAVED" : labels[i], b, Justification::centred, false);

        if (i != 1 && proc.hasPresetSlot (i))
        {
            g.setColour (ff::dim);
            g.fillEllipse (b.getCentreX() - 2.0f, b.getBottom() + 3.0f, 4.0f, 4.0f);
        }
    }
}

void PresetToggle::mouseDown (const MouseEvent& e)
{
    pressedCell = -1;
    for (int i = 0; i < 3; ++i)
        if (cell (i).contains (e.position)) pressedCell = i;
    downAt = nowSeconds();
    saved = false;
    startTimer (40);
}

void PresetToggle::timerCallback()
{
    if (pressedCell >= 0 && pressedCell != 1 && ! saved && nowSeconds() - downAt > 0.8)
    {
        proc.savePresetSlot (pressedCell);
        saved = true;
        savedCell = pressedCell;
        savedFlashUntil = nowSeconds() + 0.9;
    }
    if (pressedCell < 0 && nowSeconds() > savedFlashUntil)
        stopTimer();
    repaint();
}

void PresetToggle::mouseUp (const MouseEvent&)
{
    if (pressedCell >= 0 && ! saved)
        setParam (proc.apvts.getParameter (ParamIDs::presetSlot), (float) pressedCell);
    pressedCell = -1;
    repaint();
}

BypassButton::BypassButton (AfterglowProcessor& p)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*p.apvts.getParameter (ParamIDs::bypass), [this] (float v) { bypassed = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void BypassButton::mouseDown (const MouseEvent&) { attachment->setValueAsCompleteGesture (bypassed ? 0.0f : 1.0f); }

void BypassButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = ! bypassed;
    g.setColour (ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (on ? ff::ink : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);

    const Point<float> led (r.getX() + 25.0f, r.getCentreY());
    if (on)
    {
        g.setColour (ff::red.withAlpha (0.22f));
        g.fillEllipse (led.x - 12.0f, led.y - 12.0f, 24.0f, 24.0f);
        g.setColour (ff::red);
    }
    else
    {
        g.setColour (ff::pin);
    }
    g.fillEllipse (led.x - 7.0f, led.y - 7.0f, 14.0f, 14.0f);

    g.setColour (on ? ff::ink : ff::dim);
    g.setFont (ff::font (20.0f, 0.08f));
    g.drawText (on ? "ON" : "BYPASSED", r.withTrimmedLeft (46.0f), Justification::centredLeft, false);

    auto cap = r.withTrimmedRight (16.0f);
    g.setColour (ff::dim);
    g.setFont (ff::font (12.0f, 0.06f));
    g.drawText ("TAP BYPASS", cap.withTrimmedBottom (cap.getHeight() * 0.5f).withTrimmedTop (8.0f), Justification::centredRight, false);
    g.drawText ("TAILS RING OUT", cap.withTrimmedTop (cap.getHeight() * 0.5f).withTrimmedBottom (8.0f), Justification::centredRight, false);
}

// =====================================================================================
// Pebble
// =====================================================================================
void PebbleStrip::update (double t, double dt)
{
    now = t;
    const auto& eng = proc.getEngine();
    level = eng.uiInput.load();
    active = eng.uiActive.load();
    rising = eng.uiRising.load();
    held = eng.uiHeld.load();
    mode = eng.uiMode.load();
    down = eng.uiDown.load();
    bypassed = proc.apvts.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f;
    const bool layersInf = std::lround (proc.apvts.getRawParameterValue (ParamIDs::layers)->load()) == 10;
    const bool decayInf = ag::Times::decayInfinite (proc.apvts.getRawParameterValue (ParamIDs::decay)->load());
    infinite = active > 0 && (layersInf || decayInf);

    if (level > 0.004f || active > 0) lastSound = t;
    asleep = bypassed || t - lastSound > 6.0;

    const int caps = eng.uiCaptures.load();
    if (caps != lastCaptures) { if (caps > lastCaptures) { catchUntil = t + 0.35; hop = 1.0f; } lastCaptures = caps; }
    const int clears = eng.uiClears.load();
    if (clears != lastClears) { if (clears > lastClears) clearUntil = t + 0.7; lastClears = clears; }
    if (eng.uiPeak.load() > 0.98f) clipUntil = t + 0.8;
    hop *= (float) std::exp (-dt / 0.12);

    if (t > nextBlink)
    {
        blinkUntil = t + 0.13;
        nextBlink = t + 2.5 + Random::getSystemRandom().nextDouble() * 3.0;
    }
    blinking = t < blinkUntil;

    const bool latched = eng.uiLatched.load();
    if (bypassed) status = "BYPASSED";
    else if (mode == ag::ModeSostenuto) status = down ? "SOSTENUTO" : "PIANO";
    else if (mode != ag::ModeNormal) status = down ? "DAMPERS UP" : "PIANO";
    else if (down && held > 0) status = latched ? "LATCHED" : "HOLDING";
    else if (down) status = eng.uiGroupClosed.load() ? "GROUP FULL" : "CATCHING";
    else if (active > 0) status = "FADING";
    else status = "LISTENING";

    if (active == 0) detail = down ? "PLAY A NOTE" : "PRESS THE PEDAL";
    else detail = String (active) + (active == 1 ? " LAYER" : " LAYERS") + (rising > 0 ? dot + String (rising) + " BLOOMING" : String());
    repaint();
}

void PebbleStrip::paintPebble (Graphics& g, Rectangle<float> a)
{
    // Pebble from Flip Fella. Its eyes pop when a layer is caught, it holds its breath
    // while layers are held, rings stack above it as layers build, spiral eyes at
    // infinity, a halo in the piano modes, startled when cleared, red when clipping.
    const float s = a.getWidth() / 140.0f;
    const bool clearing = now < clearUntil;
    const bool catching = now < catchUntil;
    const bool clipping = now < clipUntil;
    const bool holding = down && held > 0 && ! asleep;
    const float bob = asleep ? 0.0f : 1.5f * (float) std::sin (now * 2.4);
    const float breathe = asleep ? 1.0f + 0.025f * (float) std::sin (now * 1.6) : (holding ? 1.05f : 1.0f);
    const float dy = bob - hop * 12.0f;
    const auto tf = AffineTransform::translation (-100.0f, -100.0f).scaled (s * breathe, s / breathe)
                        .translated (a.getCentreX(), a.getCentreY() + 10.0f * s);
    auto ell = [&] (float cx, float cy, float rx, float ry, Colour col)
    {
        Path p; p.addEllipse (cx - rx, cy - ry, rx * 2, ry * 2);
        g.setColour (col); g.fillPath (p, tf);
    };
    auto stroke = [&] (Path& p, float w, Colour col = ff::faceInk)
    {
        g.setColour (col);
        g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded), tf);
    };

    if (mode != ag::ModeNormal && ! asleep)
    {
        Path halo; halo.addEllipse (100 - 70, 100 - 60 + dy, 140, 120);
        Path dashed;
        const float dashes[] = { 10.0f, 8.0f };
        PathStrokeType (5.0f).createDashedStroke (dashed, halo, dashes, 2);
        g.setColour (modeColour (mode));
        g.fillPath (dashed, tf);
    }

    // rings above the head, one per few layers
    const int rings = asleep ? 0 : jmin (3, (active + 2) / 3);
    for (int k = 0; k < rings; ++k)
    {
        Path arc;
        const float y = 40.0f - (float) k * 13.0f + dy;
        arc.startNewSubPath (72.0f - (float) k * 7.0f, y);
        arc.quadraticTo (100.0f, y - 14.0f - (float) k * 3.0f, 128.0f + (float) k * 7.0f, y);
        stroke (arc, 5.0f, ff::ink.withAlpha (1.0f - 0.3f * (float) k));
    }

    ell (107, 109 + dy, 58, 48, ff::blobShade);
    ell (100, 100 + dy, 58, 48, ff::blob);

    if (holding || clipping)
    {
        ell (64, 112 + dy, 9.0f, 9.0f, (clipping ? ff::red : ff::blush).withAlpha (0.85f));
        ell (136, 112 + dy, 9.0f, 9.0f, (clipping ? ff::red : ff::blush).withAlpha (0.85f));
    }

    // eyes
    if (asleep || (blinking && ! clearing && ! catching))
    {
        for (float ex : { 80.0f, 120.0f })
        {
            Path lid; lid.startNewSubPath (ex - 12, 94 + dy); lid.quadraticTo (ex, 102 + dy, ex + 12, 94 + dy);
            stroke (lid, 6.0f);
        }
    }
    else if (clearing || catching)
    {
        for (float ex : { 80.0f, 120.0f })
        {
            ell (ex, 90 + dy, 17.0f, 18.0f, ff::eye);
            ell (ex, 90 + dy, clearing ? 4.0f : 7.0f, clearing ? 4.0f : 7.0f, ff::faceInk);
        }
    }
    else if (holding)
    {
        for (float ex : { 80.0f, 120.0f })
        {
            Path squeeze; squeeze.startNewSubPath (ex - 12, 98 + dy); squeeze.quadraticTo (ex, 86 + dy, ex + 12, 98 + dy);
            stroke (squeeze, 6.0f);
        }
    }
    else if (infinite)
    {
        for (float ex : { 80.0f, 120.0f })
        {
            const float spin = (float) now * 2.0f;
            Path spiral;
            for (int k = 0; k <= 40; ++k)
            {
                const float t = (float) k / 40.0f;
                const float ang = spin + t * MathConstants<float>::twoPi * 2.0f;
                const Point<float> pt (ex + std::cos (ang) * 13.0f * t, 93 + dy + std::sin (ang) * 13.0f * t);
                if (k == 0) spiral.startNewSubPath (pt); else spiral.lineTo (pt);
            }
            ell (ex, 93 + dy, 15.0f, 15.0f, ff::eye);
            stroke (spiral, 3.0f);
        }
    }
    else
    {
        for (float ex : { 80.0f, 120.0f })
        {
            ell (ex, 92 + dy, 14.0f, 14.0f, ff::eye);
            ell (ex, 94 + dy, 7.0f, 7.0f, ff::faceInk);
        }
    }

    // mouth
    if (clearing || catching)
        ell (100, 125 + dy, 7.0f, 8.0f, ff::faceInk);
    else if (holding || asleep)
    {
        Path m; m.startNewSubPath (93, 124 + dy); m.lineTo (107, 124 + dy);
        stroke (m, 6.0f);
    }
    else
    {
        Path m; m.startNewSubPath (90, 120 + dy); m.quadraticTo (100, 130 + dy, 110, 120 + dy);
        stroke (m, 6.0f);
    }

    if (clipping)
    {
        Path drop;
        drop.startNewSubPath (152, 70 + dy);
        drop.quadraticTo (162, 86 + dy, 152, 90 + dy);
        drop.quadraticTo (142, 86 + dy, 152, 70 + dy);
        g.setColour (Colour (0xff9fd3ff));
        g.fillPath (drop, tf);
    }

    if (clearing)
    {
        // layers popping like bubbles
        const float t = (float) ((clearUntil - now) / 0.7);
        g.setColour (ff::ink.withAlpha (t));
        for (int k = 0; k < 3; ++k)
        {
            const float bx = a.getX() + a.getWidth() * (0.2f + 0.3f * (float) k);
            const float by = a.getY() + 4.0f + (1.0f - t) * 6.0f;
            const float br = (3.0f + 4.0f * (1.0f - t)) * s * 1.6f;
            g.drawEllipse (bx - br, by - br, br * 2, br * 2, 2.0f);
        }
    }

    if (asleep)
    {
        g.setFont (ff::font (15.0f, 0.0f));
        for (int k = 0; k < 3; ++k)
        {
            const float ph = (float) std::fmod (now * 0.5 + k / 3.0, 1.0);
            g.setColour (ff::ink.withAlpha (1.0f - ph));
            g.drawText ("z", Rectangle<float> (a.getRight() - 14.0f + ph * 8.0f, a.getY() + 10.0f - ph * 18.0f, 16.0f, 16.0f),
                        Justification::centred, false);
        }
    }
}

void PebbleStrip::paint (Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (ff::face);
    g.fillRoundedRectangle (r.toFloat(), 12.0f);

    auto inner = r.reduced (18, 10);
    paintPebble (g, inner.removeFromLeft (80).toFloat());
    inner.removeFromLeft (18);
    auto info = inner.removeFromLeft (130).withTrimmedTop (6);
    ff::label (g, "SUSTAIN", info.removeFromTop (18));
    ff::label (g, status, info.removeFromTop (30), Justification::centredLeft, ff::ink, 22.0f);
    ff::label (g, detail, info.removeFromTop (18));
}
} // namespace ui

// =====================================================================================
// Editor
// =====================================================================================
using namespace ui;

AfterglowEditor::AfterglowEditor (AfterglowProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      footswitch (p.apvts),
      killDry (p.apvts, ParamIDs::killDry, "KILL DRY", 14.0f),
      presets (p),
      layersDial (p),
      catchMode (p.apvts, ParamIDs::catchMode, { "SINGLE", "GROUP", "ALL" }),
      rise (p.apvts, ParamIDs::rise, "Rise"),
      decay (p.apvts, ParamIDs::decay, "Decay"),
      holdMode (p.apvts, ParamIDs::holdMode, { "NONE", "FIRST", "ALL" }),
      lanes (p),
      tone (p.apvts, ParamIDs::tone, "Tone"),
      resonance (p.apvts, ParamIDs::resonance, "Resonance", true),
      level (p.apvts, ParamIDs::level, "Level"),
      input (p.apvts, ParamIDs::input, "Input"),
      pianoBox (p),
      fxPos (p.apvts, ParamIDs::fxPos, { "PRE", "LOOP", "POST" }),
      fxAmount (p.apvts, ParamIDs::fxAmount, "Amount"),
      fxMix (p.apvts, ParamIDs::fxMix, "Mix"),
      fxType (p.apvts, ParamIDs::fxType, { "VERB", "DELAY", "CHORUS", "SHIMMER" }, 14.0f, 2),
      strip (p),
      pedal (p),
      learn (p),
      bypass (p)
{
    setLookAndFeel (&lnf);
    lnf.setColour (TextButton::buttonColourId, ff::bg);
    lnf.setColour (TextButton::textColourOffId, ff::ink);

    stateCard.setTextHeight (40.0f);
    countCard.setTextHeight (40.0f);
    fxCard.setTextHeight (40.0f);

    clear.onClick = [this] { proc.clearLayers(); };

    for (auto* c : std::initializer_list<Component*> { &footswitch, &killDry, &presets, &stateCard, &countCard, &layersDial, &catchMode,
                                                       &rise, &decay, &holdMode, &lanes, &tone, &resonance, &level, &input, &pianoBox,
                                                       &fxCard, &fxPos, &fxAmount, &fxMix, &fxType, &strip, &pedal, &learn, &clear, &bypass })
        addAndMakeVisible (c);

    setSize (1260, 648);
    lastTick = Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

AfterglowEditor::~AfterglowEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AfterglowEditor::section (Graphics& g, Rectangle<int> r) const
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (r.toFloat(), 12.0f);
}

void AfterglowEditor::paint (Graphics& g)
{
    g.fillAll (ff::bg);
    g.setColour (ff::ink);
    g.setFont (ff::font (26.0f, 0.04f));
    g.drawText ("AFTERGLOW", 20, 20, 170, 40, Justification::centredLeft, false);
    ff::label (g, "SPECTRAL SUSTAIN, PIANO PEDAL", { 196, 20, 300, 40 });
    ff::label (g, "FOOTSWITCH", { 626, 20, 90, 40 });

    section (g, { 20, 76, 340, 440 });
    section (g, { 376, 76, 250, 440 });
    section (g, { 642, 76, 290, 440 });
    section (g, { 948, 76, 292, 440 });

    ff::label (g, "LAYERS", { 36, 92, 200, 16 });
    ff::label (g, "CATCH", { 36, 456, 200, 16 });
    ff::label (g, "ENVELOPE", { 392, 92, 200, 16 });
    ff::label (g, "HOLD", { 392, 250, 200, 16 });
    ff::label (g, "TONE + RESONANCE", { 658, 92, 250, 16 });
    ff::label (g, "FX LOOP", { 964, 92, 100, 16 });
    ff::label (g, fxMeaning, { 1040, 92, 184, 16 }, Justification::centredRight, ff::ink);
    ff::label (g, "TYPE", { 964, 382, 100, 16 });
}

void AfterglowEditor::resized()
{
    // header
    footswitch.setBounds (718, 24, 240, 32);
    killDry.setBounds (970, 24, 116, 32);
    presets.setBounds (1100, 24, 140, 40);

    // layers
    stateCard.setBounds (36, 116, 226, 66);
    countCard.setBounds (268, 116, 76, 66);
    layersDial.setBounds (36, 190, 308, 262);
    catchMode.setBounds (36, 474, 308, 32);

    // envelope
    rise.setBounds (386, 112, 115, 128);
    decay.setBounds (501, 112, 115, 128);
    holdMode.setBounds (392, 268, 218, 32);
    lanes.setBounds (392, 316, 218, 184);

    // tone + resonance
    tone.setBounds (652, 112, 135, 128);
    resonance.setBounds (787, 112, 135, 128);
    level.setBounds (652, 248, 135, 128);
    input.setBounds (787, 248, 135, 128);
    pianoBox.setBounds (658, 392, 258, 108);

    // fx loop
    fxCard.setBounds (964, 116, 260, 66);
    fxPos.setBounds (964, 192, 260, 32);
    fxAmount.setBounds (958, 236, 135, 128);
    fxMix.setBounds (1093, 236, 135, 128);
    fxType.setBounds (964, 402, 260, 98);

    // bottom bar
    strip.setBounds (20, 532, 1220, 96);
    pedal.setBounds (262, 546, 538, 68);
    learn.setBounds (812, 558, 90, 44);
    clear.setBounds (912, 558, 80, 44);
    bypass.setBounds (1004, 548, 220, 64);
}

void AfterglowEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;
    auto& s = proc.apvts;
    const auto& eng = proc.getEngine();

    for (auto* c : { &stateCard, &countCard, &fxCard })
        c->advance (dt);

    // flip cards: what the engine is doing, how many layers, which effect
    const int mode = eng.uiMode.load();
    const bool down = eng.uiDown.load();
    const int active = eng.uiActive.load();
    String state;
    if (s.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f) state = "BYPASS";
    else if (mode == ag::ModeSostenuto) state = "SOSTENUTO";
    else if (mode != ag::ModeNormal) state = down ? "RINGING" : "PIANO";
    else if (down) state = eng.uiHeld.load() > 0 ? "HOLDING" : "CATCHING";
    else state = active > 0 ? "FADING" : "READY";
    stateCard.setValue (state);

    const int stop = jlimit (0, 10, (int) std::lround (s.getRawParameterValue (ParamIDs::layers)->load()));
    countCard.setValue (stop == 10 && active == 0 ? String::fromUTF8 ("\xe2\x88\x9e") : String (active).paddedLeft ('0', 2));

    static const char* fxNames[4] = { "REVERB", "DELAY", "CHORUS", "SHIMMER" };
    fxCard.setValue (fxNames[jlimit (0, 3, (int) std::lround (s.getRawParameterValue (ParamIDs::fxType)->load()))]);

    static const char* meanings[3] = { "BEFORE THE ENGINE", "LAYERS ONLY", "WHOLE MIX" };
    const String m = meanings[jlimit (0, 2, (int) std::lround (s.getRawParameterValue (ParamIDs::fxPos)->load()))];
    if (m != fxMeaning) { fxMeaning = m; repaint (1040, 88, 190, 24); }

    strip.update (now, dt);
    layersDial.repaint();
    lanes.repaint();
    pianoBox.repaint();
    pedal.repaint();
    learn.repaint();
}
