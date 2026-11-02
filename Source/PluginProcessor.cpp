#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AfterglowProcessor::AfterglowProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AfterglowState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    startTimerHz (60);
}

AfterglowProcessor::~AfterglowProcessor()
{
    stopTimer();
}

void AfterglowProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
}

bool AfterglowProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo())
        return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

ag::EngineParams AfterglowProcessor::readParams()
{
    ag::EngineParams p;
    p.levelDb = f (ParamIDs::level);
    p.inputDb = f (ParamIDs::input);
    p.layers = jlimit (1, ag::kInfinite, idx (ParamIDs::layers) + 1);
    p.rise = f (ParamIDs::rise);
    p.decay = f (ParamIDs::decay);
    p.tone = f (ParamIDs::tone);
    p.resonance = f (ParamIDs::resonance);
    p.catchMode = idx (ParamIDs::catchMode);
    p.holdMode = idx (ParamIDs::holdMode);
    p.fxPos = idx (ParamIDs::fxPos);
    p.fxType = idx (ParamIDs::fxType);
    p.fxAmount = f (ParamIDs::fxAmount);
    p.fxMix = f (ParamIDs::fxMix);
    p.killDry = on (ParamIDs::killDry);
    p.fsMode = idx (ParamIDs::fsMode);
    p.fsOn = on (ParamIDs::fsOn);
    p.bypass = on (ParamIDs::bypass);
    p.pedal = std::max (f (ParamIDs::pedal), midiDepth());
    return p;
}

void AfterglowProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || getTotalNumOutputChannels() < 2)
        return;

    if (getTotalNumInputChannels() == 1) // a mono input feeds both sides
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    // Process in pieces between MIDI events so a sustain pedal press lands on time
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = jlimit (0, n, meta.samplePosition);
        if (at > pos)
        {
            engine.process (L + pos, R + pos, at - pos, readParams());
            pos = at;
        }
        handleMidi (meta.getMessage());
    }
    if (pos < n)
        engine.process (L + pos, R + pos, n - pos, readParams());
}

// The PLUS Pedal II's MIDI chart (CC 8 to 19), plus the sustain pedal and MIDI learn.
void AfterglowProcessor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn() || m.isNoteOff())
    {
        if (m.isNoteOn() && learning.exchange (false))
        {
            pedalNote.store (m.getNoteNumber());
            learnedFlag.store (1);
        }
        if (m.getNoteNumber() == pedalNote.load())
            notePedal.store (m.isNoteOn() ? 1.0f : 0.0f);
        return;
    }
    if (! m.isController())
        return;

    const int cc = m.getControllerNumber();
    const int v = m.getControllerValue();
    const float v01 = (float) v / 127.0f;

    if (cc == 64 || cc == 14) { midiPedal.store (v01); return; }
    if (cc == pedalCC.load()) { stripPedal.store (v01); return; }

    const bool chart = cc >= 8 && cc <= 19;
    if (! chart && learning.load() && cc != 0 && cc != 32 && cc < 120)
    {
        learning.store (false);
        pedalCC.store (cc);
        stripPedal.store (v01);
        learnedFlag.store (2);
        return;
    }

    auto three = [] (int x) { return x < 43 ? 0.0f : (x < 86 ? 1.0f : 2.0f); };
    switch (cc)
    {
        case 8:  queueValue (ParamIDs::level, -24.0f + 30.0f * v01); break;
        case 9:
        {
            static const int edges[10] = { 12, 24, 35, 47, 59, 70, 82, 94, 105, 117 };
            int i = 0;
            while (i < 10 && v >= edges[i]) ++i;
            queueValue (ParamIDs::layers, (float) i);
            break;
        }
        case 10: queueValue (ParamIDs::rise, v01); break;
        case 11: queueValue (ParamIDs::decay, v01); break;
        case 12: queueValue (ParamIDs::tone, v01); break;
        case 13: queueValue (ParamIDs::resonance, v == 64 ? 0.0f : (v < 64 ? -(float) (64 - v) / 64.0f : (float) (v - 64) / 63.0f)); break;
        case 15: queueValue (ParamIDs::catchMode, three (v)); break;
        case 16: queueValue (ParamIDs::holdMode, three (v)); break;
        case 17: queueValue (ParamIDs::killDry, v >= 64 ? 1.0f : 0.0f); break;
        case 18: queueValue (ParamIDs::fxPos, 2.0f - three (v)); break; // the chart runs Post, Loop, Pre
        case 19:
            if (v < 32) queueValue (ParamIDs::fsOn, 0.0f);
            else
            {
                queueValue (ParamIDs::fsMode, (float) ((v - 32) / 32));
                queueValue (ParamIDs::fsOn, 1.0f);
            }
            break;
        default: break;
    }
}

void AfterglowProcessor::queueValue (const char* id, float plainValue)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        pending[(size_t) scope.startIndex1] = { id, plainValue };
}

void AfterglowProcessor::setNow (const String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

int AfterglowProcessor::learnState() const
{
    if (learning.load()) return 1;
    if (Time::getMillisecondCounterHiRes() * 0.001 < learnFlashUntil) return 2;
    return 0;
}

String AfterglowProcessor::pedalSources() const
{
    const String dot = String::fromUTF8 (" \xc2\xb7 ");
    String s = "CC64" + dot + "NOTE " + MidiMessage::getMidiNoteName (pedalNote.load(), true, true, 3).toUpperCase();
    if (pedalCC.load() >= 0) s << dot << "CC" << pedalCC.load() << " HALF PEDAL";
    return s;
}

// ---- message thread housekeeping ----
void AfterglowProcessor::timerCallback()
{
    // MIDI CC changes
    const auto scope = fifo.read (fifo.getNumReady());
    auto apply = [this] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
            setNow (pending[(size_t) i].id, pending[(size_t) i].value);
    };
    apply (scope.startIndex1, scope.blockSize1);
    apply (scope.startIndex2, scope.blockSize2);

    // A learned pedal key or strip saves with the set
    if (const int learned = learnedFlag.exchange (0))
    {
        apvts.state.setProperty ("pedalNote", pedalNote.load(), nullptr);
        apvts.state.setProperty ("pedalCC", pedalCC.load(), nullptr);
        learnFlashUntil = Time::getMillisecondCounterHiRes() * 0.001 + 1.5;
        ignoreUnused (learned);
    }

    // Preset toggle: leaving Live keeps your live settings, coming back restores them
    const int slot = idx (ParamIDs::presetSlot);
    if (slot != lastSlot)
    {
        auto store = apvts.state.getOrCreateChildWithName ("Snapshots", nullptr);
        if (lastSlot == 1)
        {
            for (int i = store.getNumChildren(); --i >= 0;)
                if (store.getChild (i).getProperty ("name").toString() == "Live")
                    store.removeChild (i, nullptr);
            auto live = snapshotOf();
            live.setProperty ("name", "Live", nullptr);
            store.appendChild (live, nullptr);
        }
        const char* name = slot == 0 ? "A" : (slot == 2 ? "B" : "Live");
        for (const auto& child : store)
            if (child.getProperty ("name").toString() == name)
            {
                applySnapshot (child);
                break;
            }
        lastSlot = slot;
    }
}

bool AfterglowProcessor::inSnapshot (const String& id)
{
    static const StringArray excluded { ParamIDs::presetSlot, ParamIDs::bypass, ParamIDs::pedal, ParamIDs::fsOn };
    return ! excluded.contains (id);
}

ValueTree AfterglowProcessor::snapshotOf() const
{
    ValueTree t ("Snapshot");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (inSnapshot (rp->getParameterID()))
                t.setProperty (Identifier (rp->getParameterID()), rp->convertFrom0to1 (rp->getValue()), nullptr);
    return t;
}

void AfterglowProcessor::applySnapshot (const ValueTree& t)
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            const Identifier id (rp->getParameterID());
            if (inSnapshot (rp->getParameterID()) && t.hasProperty (id))
                setNow (rp->getParameterID(), (float) t.getProperty (id));
        }
}

void AfterglowProcessor::savePresetSlot (int slot)
{
    if (slot != 0 && slot != 2)
        return;
    const String name = slot == 0 ? "A" : "B";
    auto store = apvts.state.getOrCreateChildWithName ("Snapshots", nullptr);
    for (int i = store.getNumChildren(); --i >= 0;)
        if (store.getChild (i).getProperty ("name").toString() == name)
            store.removeChild (i, nullptr);
    auto snap = snapshotOf();
    snap.setProperty ("name", name, nullptr);
    store.appendChild (snap, nullptr);
}

bool AfterglowProcessor::hasPresetSlot (int slot) const
{
    const String name = slot == 0 ? "A" : (slot == 2 ? "B" : "Live");
    const auto store = apvts.state.getChildWithName ("Snapshots");
    for (const auto& child : store)
        if (child.getProperty ("name").toString() == name)
            return true;
    return false;
}

AudioProcessorEditor* AfterglowProcessor::createEditor()
{
    return new AfterglowEditor (*this);
}

void AfterglowProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("pedalNote", pedalNote.load(), nullptr);
    state.setProperty ("pedalCC", pedalCC.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AfterglowProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = ValueTree::fromXml (*xml);
    pedalNote.store ((int) tree.getProperty ("pedalNote", 36));
    pedalCC.store ((int) tree.getProperty ("pedalCC", -1));
    apvts.replaceState (tree);
    lastSlot = idx (ParamIDs::presetSlot); // don't re-apply a preset on load

    // never come back with the pedal stuck down
    if (auto* p = apvts.getParameter (ParamIDs::pedal))
        p->setValueNotifyingHost (0.0f);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AfterglowProcessor();
}
