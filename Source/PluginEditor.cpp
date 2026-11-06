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

static void pill (Graphics& g, Rectangle<float> b, const String& text, bool on, float size, Colour onColour = ff::ink)
{
    const float r = b.getHeight() * 0.5f;
    if (on)
    {
        g.setColour (onColour);
        g.fillRoundedRectangle (b, r);
    }
    else
    {
        g.setColour (ff::line);
        g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f);
    }
    g.setColour (on ? ff::bg : ff::ink);
    g.setFont (ff::font (size, 0.06f));
    g.drawText (text, b, Justification::centred, false);
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
    g.drawFittedText (text, card.reduced (6.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.7f);

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
Pills::Pills (AudioProcessorValueTreeState& s, const String& paramId, StringArray l, float size)
    : labels (std::move (l)), textSize (size)
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
    const float gap = 6.0f;
    const float w = ((float) getWidth() - gap * (float) (labels.size() - 1)) / (float) labels.size();
    return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
}

void Pills::paint (Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
        pill (g, cell (i).reduced (1.0f), labels[i], i == selected, textSize);
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

ToggleChip::ToggleChip (AudioProcessorValueTreeState& s, const String& paramId, const String& t, const String& tOn, bool fillOn, float size)
    : text (t), onText (tOn.isEmpty() ? t : tOn), fill (fillOn), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void ToggleChip::paint (Graphics& g)
{
    pill (g, getLocalBounds().toFloat().reduced (1.0f), on ? onText : text, on && fill, textSize);
}

void ToggleChip::mouseDown (const MouseEvent&)
{
    attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void ActionChip::paint (Graphics& g) { pill (g, getLocalBounds().toFloat().reduced (1.0f), text, lit, size); }

// =====================================================================================
// Look and feel and dials
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

Dial::Dial (AudioProcessorValueTreeState& s, const String& paramId, const String& c, bool redHand, bool sec)
    : caption (c), secondary (sec)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.getProperties().set ("red", redHand);
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
    ff::label (g, caption.toUpperCase(), r.removeFromTop (18), Justification::centred, secondary ? ff::faint.brighter (0.4f) : ff::dim);
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

Point<float> LayersDial::centre() const { return { getWidth() * 0.5f, getHeight() * 150.0f / 268.0f }; }
float LayersDial::scale() const { return jmin ((float) getWidth() / 330.0f, (float) getHeight() / 268.0f); }
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
        g.setColour (base.interpolatedWith (heldSlot && mode == ag::ModeNormal ? ff::brass : led, lvl));
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
    if (stop < 0 || stop == currentStop()) return;
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
    if (std::abs (e.getDistanceFromDragStartY()) > 6)
        pick (jlimit (0, 10, roundToInt (dragStart - (float) e.getDistanceFromDragStartY() / 20.0f)));
}

void LayersDial::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    pick (jlimit (0, 10, currentStop() + (w.deltaY > 0 ? 1 : -1)));
}

// =====================================================================================
// Layer lanes
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
        if (st == 2) col = mode == ag::ModeNormal ? ff::brass : modeColour (mode);
        else if (st == 1) col = ff::ink;
        else if (st == 3) col = ff::dim;
        g.setColour (col);
        g.fillRoundedRectangle (bar, 3.0f);
    }

    // a key for the colours
    struct Key { const char* t; Colour c; };
    const Key keys[3] = { { "RISING", ff::ink }, { "HELD", ff::brass }, { "FADING", ff::dim } };
    int x = caption.getX();
    for (const auto& k : keys)
    {
        g.setColour (k.c);
        g.fillRoundedRectangle ((float) x, (float) caption.getCentreY() - 4.0f, 8.0f, 8.0f, 2.0f);
        ff::label (g, k.t, { x + 12, caption.getY(), 60, caption.getHeight() }, Justification::centredLeft, ff::faint.brighter (0.4f), 11.0f);
        x += 72;
    }
}

// =====================================================================================
// Brass pedal and bypass
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

void BrassPedal::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { menu(); return; }
    held = true;
    param->beginChangeGesture();
    set (1.0f);
    repaint();
}

void BrassPedal::mouseDrag (const MouseEvent& e)
{
    // drag up to ease your foot off, back down to press in again
    if (held) set (1.0f + (float) jmin (0, e.getDistanceFromDragStartY()) / 120.0f);
}

void BrassPedal::mouseUp (const MouseEvent&)
{
    if (! held) return;
    held = false;
    set (0.0f);
    param->endChangeGesture();
    repaint();
}

void BrassPedal::menu()
{
    const bool sustain = proc.apvts.getRawParameterValue (ParamIDs::sustainCC)->load() > 0.5f;
    PopupMenu m;
    m.addSectionHeader ("Brass Pedal");
    m.addItem (-1, "Played by: " + proc.pedalSources(), false, false);
    m.addItem (1, "Keyboard sustain pedal plays it", true, sustain);
    m.addItem (2, "Learn a key or touch strip");
    m.addItem (3, "Forget the learned key or strip");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this, sustain] (int r)
    {
        if (r == 1) setParam (proc.apvts.getParameter (ParamIDs::sustainCC), sustain ? 0.0f : 1.0f);
        else if (r == 2) proc.learnPedal();
        else if (r == 3) proc.forgetPedal();
    });
}

void BrassPedal::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto& eng = proc.getEngine();
    const bool latched = eng.uiLatched.load();
    const bool down = eng.uiDown.load();
    const float depth = jlimit (0.0f, 1.0f, eng.uiDepth.load());
    const bool learning = proc.learningWhat() == AfterglowProcessor::LearnPedal;

    g.setColour (held ? ff::card : ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (down || learning ? ff::brass : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);

    auto inner = r.reduced (14.0f, 9.0f);
    auto top = inner.removeFromTop (22.0f).toNearestInt();
    ff::label (g, learning ? String ("PLAY A KEY") : String ("BRASS PEDAL"), top, Justification::centredLeft, ff::ink, 19.0f);
    ff::label (g, latched ? String ("LATCHED") : (depth > 0.0f ? String (roundToInt (depth * 100.0f)) + "%" : String ("A")),
               top, Justification::centredRight, down ? ff::brass : ff::faint.brighter (0.3f), latched || depth > 0.0f ? 15.0f : 13.0f);

    inner.removeFromTop (6.0f);
    auto track = inner.removeFromTop (10.0f);
    g.setColour (ff::ring);
    g.fillRoundedRectangle (track, 5.0f);
    if (depth > 0.0f)
    {
        g.setColour (ff::brass);
        g.fillRoundedRectangle (track.withWidth (jmax (10.0f, track.getWidth() * depth)), 5.0f);
    }

    inner.removeFromTop (4.0f);
    ff::label (g, down ? String ("LAYERS CATCH WHAT YOU PLAY") : String ("CLICK AND HOLD TO SUSTAIN"),
               inner.toNearestInt(), Justification::centredLeft, ff::dim, 11.0f);
}

BypassSwitch::BypassSwitch (AfterglowProcessor& p)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*p.apvts.getParameter (ParamIDs::bypass), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void BypassSwitch::mouseDown (const MouseEvent&) { attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

void BypassSwitch::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool lit = ! on;
    g.setColour (ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (lit ? ff::ink : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);

    const Point<float> p (r.getX() + 22.0f, r.getCentreY());
    if (lit)
    {
        g.setColour (ff::red.withAlpha (0.22f));
        g.fillEllipse (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f);
    }
    g.setColour (lit ? ff::red : ff::pin);
    g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);

    auto text = r.withTrimmedLeft (40.0f);
    g.setColour (lit ? ff::ink : ff::dim);
    g.setFont (ff::font (on ? 16.0f : 19.0f, 0.08f));
    g.drawText (on ? "BYPASSED" : "ON", text.removeFromTop (r.getHeight() * 0.6f).withTrimmedTop (6.0f), Justification::bottomLeft, false);
    ff::label (g, on ? "LAYERS RING OUT" : "A+B BYPASSES", text.toNearestInt(), Justification::topLeft, ff::dim, 10.0f);
}

// =====================================================================================
// Presets
// =====================================================================================
Rectangle<float> PresetBanks::cell (int i) const
{
    const int bank = i / 4, k = i % 4;
    return { (float) (3 + bank * 100 + k * 24), 3.0f, 20.0f, 20.0f };
}

void PresetBanks::paint (Graphics& g)
{
    static const Colour banks[4] = { ff::bankR, ff::bankY, ff::bankG, ff::bankB };
    const int sel = proc.selectedPreset();
    for (int i = 0; i < AfterglowProcessor::kPresets; ++i)
    {
        auto c = cell (i);
        const Colour col = banks[i / 4];
        const bool flash = i == flashSlot && nowSeconds() < flashUntil;
        if (proc.hasPreset (i) || flash)
        {
            g.setColour (flash ? ff::ink : col);
            g.fillRoundedRectangle (c, 6.0f);
        }
        else
        {
            g.setColour (col.withAlpha (0.45f));
            g.drawRoundedRectangle (c.reduced (1.0f), 6.0f, 2.0f);
        }
        if (i == sel)
        {
            g.setColour (ff::ink);
            g.drawRoundedRectangle (c.expanded (2.5f), 8.0f, 1.5f);
        }
    }
    ff::label (g, flashUntil > nowSeconds() ? String ("SAVED") : String::fromUTF8 ("TAP TO LOAD · HOLD TO SAVE · D STEPS"),
               { 0, 30, getWidth(), 16 }, Justification::centredLeft, ff::faint.brighter (0.4f), 11.0f);
}

void PresetBanks::mouseDown (const MouseEvent& e)
{
    pressed = -1;
    for (int i = 0; i < AfterglowProcessor::kPresets; ++i)
        if (cell (i).expanded (2.0f).contains (e.position)) pressed = i;
    downAt = nowSeconds();
    saved = false;
    startTimer (40);
}

void PresetBanks::timerCallback()
{
    // hold to save
    if (pressed >= 0 && ! saved && nowSeconds() - downAt > 0.7)
    {
        proc.savePreset (pressed);
        saved = true;
        flashSlot = pressed;
        flashUntil = nowSeconds() + 0.9;
    }
    if (pressed < 0 && nowSeconds() > flashUntil)
        stopTimer();
    repaint();
}

void PresetBanks::mouseUp (const MouseEvent&)
{
    if (pressed >= 0 && ! saved) proc.loadPreset (pressed);
    pressed = -1;
    repaint();
}

// =====================================================================================
// Footswitches and expression
// =====================================================================================
FootswitchCard::FootswitchCard (AfterglowProcessor& p, int s)
    : proc (p), sw (s),
      learn ("LEARN", [this] { proc.learnFootswitch (sw); }),
      reset ("DEFAULTS", [this] { proc.resetFootswitches(); })
{
    letter.setTextHeight (54.0f);
    letter.setValue (String::charToString ((juce_wchar) ('A' + s)));
    addAndMakeVisible (letter);
    addAndMakeVisible (learn);
    addAndMakeVisible (reset);
    if (s == 0)
    {
        sustain = std::make_unique<ToggleChip> (p.apvts, ParamIDs::sustainCC, "KEYBOARD SUSTAIN OFF", "KEYBOARD SUSTAIN ON", true, 12.0f);
        addAndMakeVisible (*sustain);
    }
}

void FootswitchCard::resized()
{
    letter.setBounds (16, 42, 258, 96);
    if (sustain) sustain->setBounds (16, 320, 170, 28);
    learn.setBounds (16, 390, 124, 34);
    reset.setBounds (150, 390, 124, 34);
}

void FootswitchCard::paint (Graphics& g)
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    ff::label (g, "FOOTSWITCH", { 16, 14, 258, 18 });

    static const char* const tapJobs[4] = { "BRASS PEDAL", "AUX ON / OFF", "KILL DRY", "NEXT PRESET" };
    static const char* const holdJobs[4] = { "SUSTAINS WHILE HELD", "NEXT AUX MODE", "CLEAR LAYERS", "PREVIOUS PRESET" };
    static const char* const notes[4] = { "DOUBLE TAP CLEARS, LIKE THE PEDAL", "WET, LATCH OR HIDE, SET ON MAIN", "LAYERS ONLY, THE DRY SIGNAL GOES", "PRESETS SAVE EVERY KNOB" };
    static const char* const combos[4] = { "A+B  BYPASS, LAYERS RING OUT", "A+B  BYPASS, LAYERS RING OUT",
                                           "C+D  NEXT AFTERGLOW, HOLD 1 S TO KEEP", "C+D  NEXT AFTERGLOW, HOLD 1 S TO KEEP" };
    struct Row { const char* k; String v; };
    const Row rows[3] = { { "TAP", tapJobs[sw] }, { "HOLD", holdJobs[sw] }, { "LISTENS FOR", sends } };
    int y = 150;
    for (const auto& r : rows)
    {
        g.setColour (ff::ring);
        g.fillRect (16, y, 258, 2);
        ff::label (g, r.k, { 16, y + 4, 120, 30 }, Justification::centredLeft, ff::dim, 13.0f);
        ff::label (g, r.v, { 110, y + 4, 164, 30 }, Justification::centredRight, ff::ink, 14.0f);
        y += 38;
    }
    g.setColour (ff::ring);
    g.fillRect (16, y, 258, 2);
    ff::label (g, notes[sw], { 16, y + 8, 258, 18 }, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, combos[sw], { 16, y + 32, 258, 18 }, Justification::centredLeft, ff::dim, 11.0f);
    if (sw == 0)
        ff::label (g, "YOUR KEYBOARD'S PEDAL WORKS IT TOO", { 16, 352, 258, 18 }, Justification::centredLeft, ff::faint.brighter (0.4f), 11.0f);
}

void FootswitchCard::tick()
{
    const auto& st = proc.pedals();
    const String t = proc.pedalsDirect() ? "CC " + String (st.fsCC[(size_t) sw]) + String::fromUTF8 (" · ANY CH")
                                         : "CH " + String (st.fsChannel[(size_t) sw]) + String::fromUTF8 (" · CC ") + String (st.fsCC[(size_t) sw]);
    if (t != sends) { sends = t; repaint(); }
    learn.setLit (proc.learningWhat() == sw);
    learn.setText (proc.learningWhat() == sw ? "PRESS IT NOW" : "LEARN");
    const bool down = (proc.uiFootDown.load() >> sw) & 1;
    letter.setAlpha (down ? 0.6f : 1.0f);
}

void BigButton::set (const String& m, const String& sm, Colour c)
{
    if (m == mainText && sm == smallText && c == borderColour) return;
    mainText = m; smallText = sm; borderColour = c;
    repaint();
}

void BigButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (pressed ? ff::ring : ff::bg);
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 2.0f);
    const float big = 13.0f, small = 9.5f;
    const float top = b.getCentreY() - (big + small + 1.0f) * 0.5f;
    ff::label (g, mainText, Rectangle<float> (b.getX(), top, b.getWidth(), big).toNearestInt(), Justification::centred, ff::ink, big);
    ff::label (g, smallText, Rectangle<float> (b.getX(), top + big + 1.0f, b.getWidth(), small).toNearestInt(), Justification::centred, ff::dim, small);
}

void BigButton::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    pressed = true; repaint();
    if (onDown) onDown();
}

void MiniFoot::paint (Graphics& g)
{
    const bool down = (proc.uiFootDown.load() >> sw) & 1;
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? ff::ink : ff::bg);
    g.fillRoundedRectangle (b, 7.0f);
    // lit while its job is on: A the Brass Pedal, B the Aux, C Kill Dry, D a preset loaded
    auto& s = proc.apvts;
    bool lit = false;
    Colour col = ff::line;
    if (sw == 0) { lit = proc.getEngine().uiDown.load(); col = ff::brass; }
    if (sw == 1) { lit = s.getRawParameterValue (ParamIDs::fsOn)->load() > 0.5f; col = ff::coral; }
    if (sw == 2) { lit = s.getRawParameterValue (ParamIDs::killDry)->load() > 0.5f; col = ff::red; }
    if (sw == 3)
    {
        static const Colour banks[4] = { ff::bankR, ff::bankY, ff::bankG, ff::bankB };
        const int sel = proc.selectedPreset();
        lit = sel >= 0;
        if (lit) col = banks[jlimit (0, 3, sel / 4)];
    }
    g.setColour (down ? ff::ink : (lit ? col : ff::line));
    g.drawRoundedRectangle (b.reduced (lit ? 1.0f : 0.0f), 7.0f, lit ? 3.0f : 2.0f);
    ff::label (g, String::charToString ((juce_wchar) ('A' + sw)), getLocalBounds(), Justification::centred, down ? ff::bg : ff::ink, 14.0f);
}

void FootInputLabel::paint (Graphics& g)
{
    // a dot that lights when the footswitch is plugged in and heard directly
    g.setColour (proc.pedalsDirect() ? ff::bankG : ff::ring);
    g.fillEllipse (0.0f, (float) getHeight() * 0.5f - 4.0f, 8.0f, 8.0f);
    ff::label (g, "FOOTSWITCHES  v", getLocalBounds().withTrimmedLeft (14), Justification::centredLeft, ff::dim, 12.0f);
}

void FootInputLabel::mouseDown (const MouseEvent&)
{
    PopupMenu m;
    m.addSectionHeader ("Footswitch input");
    m.addItem (-1, proc.footInputStatus(), false, false);
    const auto chosen = proc.footDevice();
    m.addItem (10, "Find my footswitch automatically", true, chosen.isEmpty());
    const auto devices = DirectMidiInput::deviceNames();
    for (int i = 0; i < devices.size() && i < 40; ++i)
        m.addItem (100 + i, "Use " + devices[i], true, chosen == devices[i]);
    m.addItem (11, "Only from a MIDI track in Ableton", true, chosen == "none");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this, devices] (int r)
    {
        if (r == 10) proc.setFootDevice ({});
        else if (r == 11) proc.setFootDevice ("none");
        else if (r >= 100 && r - 100 < devices.size()) proc.setFootDevice (devices[r - 100]);
        repaint();
    });
}

void ExpressionMeter::paint (Graphics& g)
{
    const auto& t = names::expTargets();
    const int target = jlimit (0, (int) t.size() - 1, roundToInt (proc.apvts.getRawParameterValue (ParamIDs::expTarget)->load()));
    String text = String::fromUTF8 ("EXP · ") + String (t[(size_t) target].label).toUpperCase() + "  v";
    if (proc.isCalibrating()) text = "ROCK HEEL TO TOE";
    else if (proc.learningWhat() == AfterglowProcessor::LearnExpression) text = "MOVE THE PEDAL";
    ff::label (g, text, { 0, 0, getWidth(), 16 }, Justification::centredLeft, proc.isCalibrating() ? ff::ink : ff::dim, 11.0f);
    const float v = proc.expressionLevel();
    const float x0 = 8.0f, w = (float) getWidth() - 16.0f, y = 24.0f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (x0, y, w, 8.0f, 4.0f);
    const bool fresh = nowSeconds() - proc.lastExpressionMove() < 1.5;
    const Colour c = target == 0 ? ff::brass : ff::ink;
    g.setColour (fresh ? c : ff::dim);
    g.fillRoundedRectangle (x0, y, w * v, 8.0f, 4.0f);
    g.fillEllipse (x0 + w * v - 8.0f, y - 4.0f, 16.0f, 16.0f);
}

void ExpressionMeter::mouseDown (const MouseEvent&)
{
    PopupMenu targets;
    const auto& t = names::expTargets();
    const int target = roundToInt (proc.apvts.getRawParameterValue (ParamIDs::expTarget)->load());
    for (int i = 0; i < (int) t.size(); ++i) targets.addItem (100 + i, t[(size_t) i].label, true, i == target);
    PopupMenu curves;
    const char* const curveNames[3] = { "Linear", "Slow start", "Fast start" };
    for (int i = 0; i < 3; ++i) curves.addItem (200 + i, curveNames[i], true, proc.pedals().curve == i);
    PopupMenu map;
    map.addItem (300, "Off", true, proc.pedals().pedalMapChannel == 0);
    for (int ch = 1; ch <= 16; ++ch) map.addItem (300 + ch, "Channel " + String (ch), true, proc.pedals().pedalMapChannel == ch);

    PopupMenu m;
    m.addSubMenu ("Expression moves", targets);
    m.addItem (1, "Calibrate heel and toe");
    m.addItem (2, "Learn expression pedal");
    m.addSubMenu ("Curve", curves);
    m.addSeparator();
    m.addSubMenu ("Original pedal MIDI map", map);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r == 1) proc.calibrateExpression();
        else if (r == 2) proc.learnExpression();
        else if (r >= 100 && r < 200) setParam (proc.apvts.getParameter (ParamIDs::expTarget), (float) (r - 100));
        else if (r >= 200 && r < 300) { proc.pedals().curve = r - 200; proc.pedals().save(); }
        else if (r >= 300) { proc.pedals().pedalMapChannel = r - 300; proc.pedals().save(); }
        repaint();
    });
}

// =====================================================================================
// Pebble
// =====================================================================================
void Pebble::update (double t, double dt)
{
    now = t;
    const auto& eng = proc.getEngine();
    level = eng.uiInput.load();
    active = eng.uiActive.load();
    held = eng.uiHeld.load();
    mode = eng.uiMode.load();
    down = eng.uiDown.load();
    const bool bypassed = proc.apvts.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f;
    const bool layersInf = std::lround (proc.apvts.getRawParameterValue (ParamIDs::layers)->load()) == 10;
    const bool decayInf = ag::Times::decayInfinite (proc.apvts.getRawParameterValue (ParamIDs::decay)->load());
    infinite = active > 0 && (layersInf || decayInf);

    if (level > 0.004f || active > 0) lastSound = t;
    asleep = bypassed || t - lastSound > 6.0;

    const int caps = eng.uiCaptures.load();
    if (caps != lastCaptures) { if (caps > lastCaptures) { catchUntil = t + 0.35; hop = jmax (hop, 0.7f); } lastCaptures = caps; }
    const int clears = eng.uiClears.load();
    if (clears != lastClears) { if (clears > lastClears) clearUntil = t + 0.7; lastClears = clears; }
    if (eng.uiPeak.load() > 0.98f) clipUntil = t + 0.8;

    // taps its foot whenever a footswitch goes down
    const int pulse = proc.uiFootPulse.load();
    if (pulse != lastPulse) { lastPulse = pulse; hop = 1.0f; }
    hop *= (float) std::exp (-dt / 0.12);
    nodding *= (float) std::exp (-dt / 0.35);
    // leans with the expression pedal while it moves
    const bool expFresh = t - proc.lastExpressionMove() < 2.0;
    const float leanTarget = expFresh ? (proc.expressionLevel() - 0.5f) * 16.0f : 0.0f;
    lean += (leanTarget - lean) * (float) (1.0 - std::exp (-dt / 0.1));

    if (t > nextBlink)
    {
        blinkUntil = t + 0.13;
        nextBlink = t + 2.5 + Random::getSystemRandom().nextDouble() * 3.0;
    }
    blinking = t < blinkUntil;
    repaint();
}

void Pebble::paint (Graphics& g)
{
    auto a = getLocalBounds().toFloat();
    const float s = a.getWidth() / 150.0f;
    const bool clearing = now < clearUntil;
    const bool catching = now < catchUntil;
    const bool clipping = now < clipUntil;
    const bool holding = down && held > 0 && ! asleep;
    const float bob = asleep ? 0.0f : 1.5f * (float) std::sin (now * 2.4);
    const float breathe = asleep ? 1.0f + 0.025f * (float) std::sin (now * 1.6) : (holding ? 1.05f : 1.0f);
    const float dy = bob - hop * 10.0f + 8.0f * nodding * (float) std::sin (nodding * 12.0f);
    const float tilt = degreesToRadians (lean);
    const auto tf = AffineTransform::translation (-100.0f, -100.0f).scaled (s * breathe, s / breathe)
                        .rotated (tilt, 0.0f, 50.0f * s).translated (a.getCentreX(), a.getCentreY() + 12.0f * s);
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
    else if (holding)
    {
        Path ringPath; ringPath.addEllipse (100 - 66, 100 - 56 + dy, 132, 112);
        stroke (ringPath, 5.0f, ff::brass.withAlpha (0.65f));
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
            g.drawText ("z", Rectangle<float> (a.getRight() - 16.0f + ph * 8.0f, a.getY() + 8.0f - ph * 14.0f, 16.0f, 16.0f),
                        Justification::centred, false);
        }
    }
}
} // namespace ui

// =====================================================================================
// Editor
// =====================================================================================
using namespace ui;

AfterglowEditor::AfterglowEditor (AfterglowProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      inputMode (p.apvts, ParamIDs::inputMode, { "GUITAR", "SYNTH", "BASS" }),
      viewMain ("MAIN", [this] { setView (View::Main); }, 13.0f),
      viewPedal ("PEDAL", [this] { setView (View::Pedal); }, 13.0f),
      layersDial (p),
      catchMode (p.apvts, ParamIDs::catchMode, { "SINGLE", "GROUP", "ALL" }),
      clear ("CLEAR", [this] { proc.clearLayers(); }, 13.0f),
      rise (p.apvts, ParamIDs::rise, "Rise"),
      decay (p.apvts, ParamIDs::decay, "Decay"),
      tone (p.apvts, ParamIDs::tone, "Tone"),
      resonance (p.apvts, ParamIDs::resonance, "Resonance"),
      holdMode (p.apvts, ParamIDs::holdMode, { "NONE", "FIRST", "ALL" }),
      level (p.apvts, ParamIDs::level, "Level", true),
      input (p.apvts, ParamIDs::input, "Input gain", false, true),
      lanes (p),
      fxType (p.apvts, ParamIDs::fxType, { "VERB", "DELAY", "CHORUS", "SHIMMER" }, 11.0f),
      fxPos (p.apvts, ParamIDs::fxPos, { "PRE", "LOOP", "POST" }, 12.0f),
      fxAmount (p.apvts, ParamIDs::fxAmount, "Amount"),
      fxMix (p.apvts, ParamIDs::fxMix, "Mix"),
      killDry (p.apvts, ParamIDs::killDry, "KILL DRY", "DRY KILLED", true, 12.0f),
      auxOn (p.apvts, ParamIDs::fsOn, "OFF", "ON", true, 12.0f),
      auxMode (p.apvts, ParamIDs::fsMode, { "WET", "LATCH", "HIDE" }, 12.0f),
      pebble (p),
      pedal (p),
      banks (p),
      footInput (p),
      expression (p),
      bypass (p)
{
    setLookAndFeel (&lnf);
    mainPage = { &stateCard, &countCard, &layersDial, &catchMode, &clear,
                 &rise, &decay, &tone, &resonance, &holdMode,
                 &level, &input, &lanes,
                 &fxCard, &fxType, &fxPos, &fxAmount, &fxMix, &killDry, &auxOn, &auxMode };
    for (auto* c : mainPage) addAndMakeVisible (c);
    for (auto* c : std::initializer_list<Component*> { &inputMode, &viewMain, &viewPedal, &pebble, &pedal, &banks,
                                                       &footInput, &expression, &bypass, &keepButton, &contextCard })
        addAndMakeVisible (c);
    for (int k = 0; k < 4; ++k)
    {
        footCards[(size_t) k] = std::make_unique<FootswitchCard> (p, k);
        addChildComponent (*footCards[(size_t) k]);
        minis[(size_t) k] = std::make_unique<MiniFoot> (p, k);
        addAndMakeVisible (*minis[(size_t) k]);
    }

    stateCard.setTextHeight (34.0f);
    countCard.setTextHeight (34.0f);
    fxCard.setTextHeight (30.0f);

    // Keeper: click to log the moment, right-click for notes, export and the folder
    keepButton.set ("KEEP", "HOLD C+D", ff::line);
    keepButton.onDown = [this] { proc.keepNow(); };
    keepButton.onRightClick = [this] { keeperMenu(); };
    contextCard.setTextHeight (12.0f);
    contextCard.setMouseCursor (MouseCursor::PointingHandCursor);
    contextCard.setValue (proc.keeperLive() ? "LIVE" : "STUDIO");
    contextCard.onClick = [this] (const MouseEvent&)
    {
        proc.setKeeperLive (! proc.keeperLive());
        contextCard.setValue (proc.keeperLive() ? "LIVE" : "STUDIO");
        proc.say (proc.keeperLive() ? "LIVE" : "STUDIO");
    };
    addChildComponent (noteEditor);
    noteEditor.setTextToShowWhenEmpty ("Optional, Enter to save", ff::faint);
    noteEditor.setFont (FontOptions (13.0f));
    noteEditor.setIndents (10, 7);
    noteEditor.setColour (TextEditor::backgroundColourId, ff::bg);
    noteEditor.setColour (TextEditor::textColourId, ff::ink);
    noteEditor.setColour (TextEditor::outlineColourId, ff::line);
    noteEditor.setColour (TextEditor::focusedOutlineColourId, ff::ink);
    noteEditor.setColour (CaretComponent::caretColourId, ff::ink);
    noteEditor.setInputRestrictions (200);
    noteEditor.onReturnKey = [this] { closeNote (true); };
    noteEditor.onEscapeKey = [this] { closeNote (false); };
    noteEditor.onTextChange = [this] { noteUntil = Time::getMillisecondCounterHiRes() * 0.001 + 20.0; };

    setView (View::Main);
    setSize (1260, 660);
    lastTick = Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

AfterglowEditor::~AfterglowEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AfterglowEditor::setView (View v)
{
    view = v;
    for (auto* c : mainPage) c->setVisible (v == View::Main);
    for (auto& c : footCards) c->setVisible (v == View::Pedal);
    viewMain.setLit (v == View::Main);
    viewPedal.setLit (v == View::Pedal);
    repaint();
}

void AfterglowEditor::keeperMenu()
{
    PopupMenu m;
    m.addItem (1, "Add a note to Keeper No. " + String (proc.lastKeeper()), proc.lastKeeper() > 0);
    m.addItem (2, "Export for Claude");
    m.addItem (3, "Open the Keepers folder");
    m.addSeparator();
    m.addItem (4, "Learn a Keeper footswitch");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&keepButton), [this] (int r)
    {
        if (r == 1)
        {
            noteNumber = proc.lastKeeper();
            noteEditor.clear();
            noteEditor.setVisible (true);
            noteEditor.grabKeyboardFocus();
            noteUntil = Time::getMillisecondCounterHiRes() * 0.001 + 20.0;
            repaint (136, 540, 190, 100);
        }
        else if (r == 2) proc.exportKeepers();
        else if (r == 3) proc.openKeepersFolder();
        else if (r == 4) proc.learnKeeper();
    });
}

void AfterglowEditor::closeNote (bool save)
{
    if (save) proc.addKeeperNote (noteNumber, noteEditor.getText());
    if (save && noteEditor.getText().trim().isNotEmpty()) proc.say ("NOTED");
    noteEditor.setVisible (false);
    noteEditor.clear();
    noteUntil = 0.0;
    repaint (136, 540, 190, 100);
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
    ff::label (g, "SPECTRAL SUSTAIN, PIANO PEDAL", { 184, 20, 280, 40 });
    ff::label (g, "INPUT", { 470, 20, 50, 40 });

    if (view == View::Main)
    {
        section (g, { 20, 76, 400, 440 });
        section (g, { 440, 76, 250, 440 });
        section (g, { 710, 76, 250, 440 });
        section (g, { 980, 76, 260, 440 });

        ff::label (g, "LAYERS", { 36, 92, 200, 16 });
        ff::label (g, "CATCH", { 36, 452, 200, 16 });
        ff::label (g, "SHAPE", { 456, 92, 200, 16 });
        ff::label (g, "HOLD", { 456, 386, 200, 16 });
        ff::label (g, holdHint, { 456, 446, 218, 18 }, Justification::centredLeft, ff::faint.brighter (0.4f), 12.0f);
        const int mode = proc.getEngine().uiMode.load();
        static const char* pianoNames[4] = { "HOLD NONE + TONE DOWN: PIANO", "PIANO MODE: REAL PIANO", "PIANO MODE: PIANO GROUP", "PIANO MODE: SOSTENUTO" };
        ff::label (g, pianoNames[jlimit (0, 3, mode)], { 456, 466, 218, 18 }, Justification::centredLeft,
                   mode == ag::ModeNormal ? ff::faint : ui::modeColour (mode), 12.0f);
        ff::label (g, "LEVEL", { 726, 92, 200, 16 });
        ff::label (g, "FX LOOP", { 996, 92, 100, 16 });
        ff::label (g, posHint, { 1080, 92, 144, 16 }, Justification::centredRight, ff::ink);
        ff::label (g, "AUX", { 996, 436, 60, 16 });
        ff::label (g, auxHint, { 1040, 436, 184, 16 }, Justification::centredRight, ff::faint.brighter (0.4f), 11.0f);
    }

    section (g, { 20, 532, 1220, 112 });
    if (noteEditor.isVisible())
        ff::label (g, "KEPT No. " + String (noteNumber) + String::fromUTF8 (" · ADD A NOTE?"), { 140, 546, 180, 16 }, Justification::centredLeft, ff::ink, 12.0f);
    else
    {
        ff::label (g, "PEBBLE", { 140, 542, 180, 16 });
        ff::label (g, mood, { 140, 558, 180, 28 }, Justification::centredLeft, ff::ink, 21.0f);
        ff::label (g, moodSub, { 140, 584, 180, 16 }, Justification::centredLeft, ff::dim, 11.0f);
    }
    ff::label (g, "USER PRESETS", { 562, 546, 380, 18 });
}

void AfterglowEditor::resized()
{
    // header
    inputMode.setBounds (520, 24, 240, 32);
    viewMain.setBounds (1094, 24, 70, 32);
    viewPedal.setBounds (1170, 24, 70, 32);

    // layers
    stateCard.setBounds (36, 116, 262, 66);
    countCard.setBounds (306, 116, 98, 66);
    layersDial.setBounds (36, 186, 368, 262);
    catchMode.setBounds (36, 472, 270, 32);
    clear.setBounds (314, 472, 90, 32);

    // shape
    rise.setBounds (456, 116, 110, 128);
    decay.setBounds (570, 116, 110, 128);
    tone.setBounds (456, 252, 110, 128);
    resonance.setBounds (570, 252, 110, 128);
    holdMode.setBounds (456, 406, 218, 32);

    // level and lanes
    level.setBounds (726, 116, 110, 128);
    input.setBounds (840, 116, 110, 128);
    lanes.setBounds (726, 262, 218, 238);

    // fx loop
    fxCard.setBounds (996, 116, 228, 60);
    fxType.setBounds (996, 186, 228, 30);
    fxAmount.setBounds (996, 222, 110, 128);
    fxMix.setBounds (1114, 222, 110, 128);
    fxPos.setBounds (996, 358, 228, 30);
    killDry.setBounds (996, 396, 228, 30);
    auxMode.setBounds (996, 458, 160, 30);
    auxOn.setBounds (1164, 458, 60, 30);

    // pedal page: one card per footswitch
    for (int k = 0; k < 4; ++k) footCards[(size_t) k]->setBounds (20 + k * 310, 76, 290, 440);

    // the strip, left to right: Pebble and Keeper, the Brass Pedal, presets, footswitches, bypass
    pebble.setBounds (32, 540, 96, 98);
    noteEditor.setBounds (140, 566, 180, 30);
    keepButton.setBounds (140, 604, 96, 32);
    contextCard.setBounds (244, 604, 76, 32);
    pedal.setBounds (332, 552, 216, 76);
    banks.setBounds (562, 568, 400, 50);
    footInput.setBounds (972, 542, 132, 20);
    for (int k = 0; k < 4; ++k) minis[(size_t) k]->setBounds (972 + k * 34, 566, 30, 30);
    expression.setBounds (972, 602, 132, 36);
    bypass.setBounds (1114, 556, 112, 64);
}

void AfterglowEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;
    auto& s = proc.apvts;
    const auto& eng = proc.getEngine();

    for (auto* c : { &stateCard, &countCard, &fxCard, &contextCard })
        c->advance (dt);
    for (auto& c : footCards) c->tick();

    // flip cards: what the engine is doing, how many layers, which effect
    const int mode = eng.uiMode.load();
    const bool down = eng.uiDown.load();
    const int active = eng.uiActive.load();
    const bool bypassed = s.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f;
    String state;
    if (bypassed) state = "BYPASSED";
    else if (mode == ag::ModeSostenuto) state = "SOSTENUTO";
    else if (mode != ag::ModeNormal) state = down ? "RINGING" : "PIANO";
    else if (down) state = eng.uiHeld.load() > 0 ? (eng.uiLatched.load() ? "LATCHED" : "HOLDING") : "CATCHING";
    else state = active > 0 ? "FADING" : "READY";
    stateCard.setValue (state);

    const int stop = jlimit (0, 10, (int) std::lround (s.getRawParameterValue (ParamIDs::layers)->load()));
    countCard.setValue (stop == 10 && active == 0 ? String::fromUTF8 ("\xe2\x88\x9e") : String (active).paddedLeft ('0', 2));

    static const char* fxNames[4] = { "REVERB", "DELAY", "CHORUS", "SHIMMER" };
    fxCard.setValue (fxNames[jlimit (0, 3, (int) std::lround (s.getRawParameterValue (ParamIDs::fxType)->load()))]);

    static const char* posMeanings[3] = { "BEFORE THE LAYERS", "ON THE LAYERS", "ON EVERYTHING" };
    static const char* holdMeanings[3] = { "LAYERS RISE, THEN FADE", "FIRST LAYER HELD, THE REST FADE", "ALL HELD WHILE THE PEDAL IS DOWN" };
    static const char* auxMeanings[3] = { "KILLS THE DRY", "LATCHES THE PEDAL", "HIDES LAYERS WHILE DOWN" };
    const String ph = posMeanings[jlimit (0, 2, (int) std::lround (s.getRawParameterValue (ParamIDs::fxPos)->load()))];
    const String hh = holdMeanings[jlimit (0, 2, (int) std::lround (s.getRawParameterValue (ParamIDs::holdMode)->load()))];
    const String ah = String ("B: ") + auxMeanings[jlimit (0, 2, (int) std::lround (s.getRawParameterValue (ParamIDs::fsMode)->load()))];

    // Pebble's status lines
    String m, sub;
    const int caps = eng.uiRising.load();
    if (now - proc.uiMessageAt < 2.5 && proc.uiMessage.isNotEmpty()) { m = proc.uiMessage; sub = "PEBBLE IS ON IT"; }
    else if (bypassed) { m = "NAPPING"; sub = "BYPASSED"; }
    else if (mode == ag::ModeSostenuto) { m = down ? "SOSTENUTO" : "PIANO"; sub = down ? "THE CHORD RINGS AGAIN" : "PRESS TO HOLD A CHORD"; }
    else if (mode != ag::ModeNormal) { m = down ? "DAMPERS UP" : "PIANO"; sub = down ? "NOTES RING ON" : "LIFT TO DAMP"; }
    else if (down && eng.uiHeld.load() > 0) { m = eng.uiLatched.load() ? "LATCHED" : "HOLDING"; sub = String (active) + (active == 1 ? " LAYER" : " LAYERS"); }
    else if (down) { m = eng.uiGroupClosed.load() ? "GROUP FULL" : "CATCHING"; sub = "PLAY A NOTE"; }
    else if (active > 0) { m = "FADING"; sub = String (active) + (active == 1 ? " LAYER" : " LAYERS") + (caps > 0 ? dot + String (caps) + " BLOOMING" : String()); }
    else { m = "LISTENING"; sub = "PRESS THE BRASS PEDAL"; }

    if (ph != posHint || hh != holdHint || ah != auxHint || m != mood || sub != moodSub)
    {
        posHint = ph; holdHint = hh; auxHint = ah; mood = m; moodSub = sub;
        repaint();
    }
    if (mode != lastMode) { lastMode = mode; repaint (440, 440, 260, 60); }

    // Keeper: flip to KEPT, Pebble nods, offer a note for ten seconds without taking the keyboard
    const int kp = proc.uiKeepPulse.load();
    if (kp != lastKeepPulse)
    {
        lastKeepPulse = kp;
        keptUntil = now + 1.8;
        contextCard.setValue ("KEPT");
        pebble.nod();
        noteNumber = 0;
        noteEditor.clear();
        noteEditor.setVisible (true);
        noteUntil = now + 10.0;
        repaint (136, 540, 190, 100);
    }
    if (noteNumber == 0 && proc.lastKeeper() > 0 && noteEditor.isVisible()) { noteNumber = proc.lastKeeper(); repaint (136, 540, 190, 100); }
    if (keptUntil > 0.0 && now > keptUntil) { keptUntil = 0.0; contextCard.setValue (proc.keeperLive() ? "LIVE" : "STUDIO"); }
    if (noteEditor.isVisible() && now > noteUntil && ! noteEditor.hasKeyboardFocus (true)) closeNote (false);
    const bool learningKeep = proc.learningWhat() == AfterglowProcessor::LearnKeeper;
    keepButton.set (learningKeep ? "PRESS IT" : "KEEP", learningKeep ? "YOUR SWITCH" : "HOLD C+D", learningKeep ? ff::ink : ff::line);

    pebble.update (now, dt);
    layersDial.repaint();
    lanes.repaint();
    pedal.repaint();
    banks.repaint();
    bypass.repaint();
    expression.repaint();
    footInput.repaint();
    for (auto& mf : minis) mf->repaint();
}
