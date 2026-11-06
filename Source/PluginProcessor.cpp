#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Several Afterglow windows can be open at once. Footswitches drive one of them at a time:
// the one whose panel was last touched, or the next one along when C+D are tapped.
namespace
{
SpinLock focusLock;
Array<AfterglowProcessor*> instances;
std::atomic<AfterglowProcessor*> focused { nullptr };

bool hasFocus (AfterglowProcessor* p)
{
    auto* f = focused.load();
    return f == nullptr || f == p;
}

// set while a message from the footswitch device is being handled on the MIDI thread
thread_local bool fromDeviceNow = false;

// footswitch device messages go to the copy that is learning, else the copy in charge
void sendToFocused (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (focusLock);
    AfterglowProcessor* target = nullptr;
    for (auto* p : instances) if (p->isLearning()) target = p;
    if (target == nullptr) target = focused.load();
    if (target == nullptr && ! instances.isEmpty()) target = instances.getLast();
    if (target != nullptr) target->handleDirectMidi (m);
}

String letter (int sw) { return String::charToString ((juce_wchar) ('A' + sw)); }
} // namespace

AfterglowProcessor::AfterglowProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AfterglowState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    settings.load();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setSink (&sendToFocused);
    direct->setWanted (settings.footDevice);
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e) { onFootEvent (sw, e); };
    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    startTimerHz (100);
}

AfterglowProcessor::~AfterglowProcessor()
{
    stopTimer();
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

void AfterglowProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keeper.prepare (sampleRate);
}

bool AfterglowProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo())
        return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

float AfterglowProcessor::pedalFromOutside() const
{
    return std::max ({ footPedal.load(), expPedal.load(), sustainPedal.load(), notePedal.load(), stripPedal.load(), mapPedal.load() });
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
    p.inputMode = idx (ParamIDs::inputMode);
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
    p.pedal = std::max (f (ParamIDs::pedal), pedalFromOutside());
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

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    keeper.pushInput (L, R, n);

    // Process in pieces between MIDI events so a sustain pedal press lands on time
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = jlimit (0, n, meta.samplePosition);
        if (at > pos)
        {
            engine.process (L + pos, R + pos, at - pos, readParams());
            pos = at;
        }
        handleMidi (meta.getMessage(), false);
    }
    if (pos < n)
        engine.process (L + pos, R + pos, n - pos, readParams());

    keeper.pushOutput (L, R, n);
}

// =====================================================================================
// MIDI: footswitches, expression, the sustain pedal and the original pedal's own MIDI map
// =====================================================================================
void AfterglowProcessor::queue (int kind, int a, float v)
{
    auto& fifoRef = fromDeviceNow ? directFifo : fifo;
    auto& dest = fromDeviceNow ? directPending : pending;
    const auto scope = fifoRef.write (1);
    if (scope.blockSize1 > 0) dest[(size_t) scope.startIndex1] = { kind, a, v, nullptr };
}

void AfterglowProcessor::queueParam (int kind, const char* id, float v)
{
    auto& fifoRef = fromDeviceNow ? directFifo : fifo;
    auto& dest = fromDeviceNow ? directPending : pending;
    const auto scope = fifoRef.write (1);
    if (scope.blockSize1 > 0) dest[(size_t) scope.startIndex1] = { kind, 0, v, id };
}

void AfterglowProcessor::handleDirectMidi (const MidiMessage& m)
{
    // several MIDI devices can call in on their own threads
    const SpinLock::ScopedLockType l (directWriteLock);
    fromDeviceNow = true;
    handleMidi (m, true);
    fromDeviceNow = false;
}

void AfterglowProcessor::setFootDevice (const String& device)
{
    settings.footDevice = device;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setWanted (device);
}

void AfterglowProcessor::handleMidi (const MidiMessage& m, bool fromDevice)
{
    const int ch = m.getChannel();

    // a learned key plays the Brass Pedal
    if (m.isNoteOn() || m.isNoteOff())
    {
        if (m.isNoteOn() && learnTarget.load() == LearnPedal)
        {
            queue (LearnedNote, m.getNoteNumber(), 0.0f);
            return;
        }
        if (m.getNoteNumber() == pedalNote.load())
            notePedal.store (m.isNoteOn() ? 1.0f : 0.0f);
        return;
    }

    if (m.isProgramChange())
    {
        // the original pedal's preset recall, on its own channel only
        if (settings.pedalMapChannel != 0 && ch == settings.pedalMapChannel && (fromDevice || hasFocus (this)))
            queue (LoadPreset, m.getProgramChangeNumber(), 0.0f);
        return;
    }
    if (! m.isController()) return;
    const int cc = m.getControllerNumber(), val = m.getControllerValue();
    const float v01 = (float) val / 127.0f;

    if (learnTarget.load() != LearnNone)
    {
        if (val > 0) queue (Learned, ch * 128 + cc, v01);
        return;
    }

    // Straight from the footswitch device, any channel will do: it's the footswitch's own cable,
    // and some footswitches ignore their channel setting. Through Ableton, the channel must match.
    // When the device is read directly, the same messages arriving through Ableton are ignored,
    // so nothing happens twice.
    if (fromDevice || ! pedalsDirect())
    {
        int foot = -1;
        for (int k = 0; k < 4 && foot < 0; ++k)
            if ((fromDevice || ch == settings.fsChannel[(size_t) k]) && cc == settings.fsCC[(size_t) k]) foot = k;
        const bool keep = foot < 0 && settings.keeperCC >= 0 && (fromDevice || ch == settings.keeperChannel) && cc == settings.keeperCC;
        const bool expression = foot < 0 && ! keep && (fromDevice || ch == settings.expChannel) && cc == settings.expCC;
        if (foot >= 0 || keep || expression)
        {
            if (! fromDevice)
            {
                // The copy Ableton sends footswitch MIDI to takes charge, unless the copy in
                // charge is fed too. A newly loaded copy with no MIDI track can't hold the pedals.
                hostFeedsPedals.store (true);
                const SpinLock::ScopedLockType l (focusLock);
                auto* f = focused.load();
                if (f != this && (f == nullptr || ! f->hostFeedsPedals.load())) focused.store (this);
                if (focused.load() != this) return;
            }
            if (foot >= 0) queue (val >= 64 ? FootPress : FootRelease, foot, 0.0f);
            else if (keep) { if (val >= 64) queue (KeepCC, 0, 0.0f); }
            else queue (Expression, val, 0.0f);
            return;
        }
    }

    // a learned controller (a touch strip or fader) is a half pedal
    if (cc == pedalCC.load()) { stripPedal.store (v01); return; }

    // a keyboard's sustain pedal is a Brass Pedal, sample-accurate through Ableton
    if (cc == 64 && on (ParamIDs::sustainCC) && ! fromDevice) { sustainPedal.store (v01); return; }

    // The PLUS Pedal II's own CC chart. Off unless a channel is chosen for it on the Pedal page:
    // left listening on every channel, CC 11 made the expression pedal sweep Decay.
    if (settings.pedalMapChannel == 0 || ch != settings.pedalMapChannel) return;
    auto three = [] (int x) { return x < 43 ? 0.0f : (x < 86 ? 1.0f : 2.0f); };
    using namespace ParamIDs;
    switch (cc)
    {
        case 8:  queueParam (SetPlain, level, -24.0f + 30.0f * v01); break;
        case 9:
        {
            static const int edges[10] = { 12, 24, 35, 47, 59, 70, 82, 94, 105, 117 };
            int i = 0;
            while (i < 10 && val >= edges[i]) ++i;
            queueParam (SetPlain, layers, (float) i);
            break;
        }
        case 10: queueParam (SetNorm, rise, v01); break;
        case 11: queueParam (SetNorm, decay, v01); break;
        case 12: queueParam (SetNorm, tone, v01); break;
        case 13: queueParam (SetPlain, resonance, val == 64 ? 0.0f : (val < 64 ? -(float) (64 - val) / 64.0f : (float) (val - 64) / 63.0f)); break;
        case 14: case 64: mapPedal.store (v01); break;
        case 15: queueParam (SetPlain, catchMode, three (val)); break;
        case 16: queueParam (SetPlain, holdMode, three (val)); break;
        case 17: queueParam (SetPlain, killDry, val >= 64 ? 1.0f : 0.0f); break;
        case 18: queueParam (SetPlain, fxPos, 2.0f - three (val)); break; // the chart runs Post, Loop, Pre
        case 19:
            if (val < 32) queueParam (SetPlain, fsOn, 0.0f);
            else
            {
                queueParam (SetPlain, fsMode, (float) ((val - 32) / 32));
                queueParam (SetPlain, fsOn, 1.0f);
            }
            break;
        default: break;
    }
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

void AfterglowProcessor::setNorm (const String& id, float v01)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (jlimit (0.0f, 1.0f, v01));
        p->endChangeGesture();
    }
}

void AfterglowProcessor::say (const String& text)
{
    uiMessage = text;
    uiMessageAt = Time::getMillisecondCounterHiRes() * 0.001;
}

void AfterglowProcessor::clearLayers()
{
    engine.requestClear();
    say ("CLEARED");
}

void AfterglowProcessor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;

    // settings changed by another copy, or another Flip Fella plugin: pick them up
    if (now >= nextSettingsCheck)
    {
        nextSettingsCheck = now + 1.0;
        const auto stamp = PedalSettings::file().getLastModificationTime();
        if (stamp != settingsStamp && learnTarget.load() == LearnNone && ! calibrating)
        {
            settingsStamp = stamp;
            settings.load();
            direct->setWanted (settings.footDevice);
        }
        const bool d = pedalsDirect();
        if (d != wasDirect)
        {
            wasDirect = d;
            if (d && hasFocus (this)) say ("FOOTSWITCHES READY");
        }
    }

    auto apply = [&] (const std::array<Pending, 512>& from, int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& q = from[(size_t) i];
            switch (q.kind)
            {
                case FootPress: footswitches.press (q.a, now); break;
                case FootRelease: footswitches.release (q.a, now); break;
                case SetNorm: setNorm (q.id, q.v); break;
                case SetPlain: setNow (q.id, q.v); break;
                case LoadPreset: loadPreset (q.a); break;
                case KeepCC: keepNow(); break;
                case Expression:
                {
                    const int cc = q.a;
                    expMovedAt = now;
                    if (calibrating)
                    {
                        if (calFirst < 0) calFirst = cc;
                        calLo = jmin (calLo, cc);
                        calHi = jmax (calHi, cc);
                        uiExp.store ((float) cc / 127.0f);
                        break;
                    }
                    const float v = settings.expressionValue (cc);
                    uiExp.store (v);
                    const auto& targets = names::expTargets();
                    const int t = jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget));
                    if (t == 0) expPedal.store (v); // the Brass Pedal: swell layers in, ease them away
                    else setNorm (targets[(size_t) t].id, v);
                    break;
                }
                case LearnedNote:
                    if (learnTarget.exchange (LearnNone) == LearnPedal)
                    {
                        pedalNote.store (q.a);
                        pedalCC.store (-1);
                        say ("BRASS PEDAL IS " + MidiMessage::getMidiNoteName (q.a, true, true, 3).toUpperCase());
                    }
                    break;
                case Learned:
                {
                    const int ch = q.a / 128, cc = q.a % 128;
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < 4)
                    {
                        settings.fsChannel[(size_t) what] = ch;
                        settings.fsCC[(size_t) what] = cc;
                        say ("FOOTSWITCH " + letter (what) + " IS CC " + String (cc));
                    }
                    else if (what == LearnKeeper)
                    {
                        settings.keeperChannel = ch;
                        settings.keeperCC = cc;
                        say ("KEEPER IS CC " + String (cc));
                    }
                    else if (what == LearnExpression)
                    {
                        settings.expChannel = ch;
                        settings.expCC = cc;
                        say ("EXPRESSION IS CC " + String (cc));
                    }
                    else if (what == LearnPedal)
                    {
                        pedalCC.store (cc);
                        pedalNote.store (-1);
                        stripPedal.store (q.v);
                        say ("BRASS PEDAL IS CC " + String (cc));
                        break; // this one is saved with the set, not shared
                    }
                    settings.save();
                    settingsStamp = PedalSettings::file().getLastModificationTime();
                    break;
                }
                default: break;
            }
        }
    };
    {
        const auto scope = fifo.read (fifo.getNumReady());
        apply (pending, scope.startIndex1, scope.blockSize1);
        apply (pending, scope.startIndex2, scope.blockSize2);
    }
    {
        const auto scope = directFifo.read (directFifo.getNumReady());
        apply (directPending, scope.startIndex1, scope.blockSize1);
        apply (directPending, scope.startIndex2, scope.blockSize2);
    }

    // actions a remote controller can press: each is a switch that springs back
    if (on (ParamIDs::keeper)) { setNow (ParamIDs::keeper, 0.0f); keepNow(); }
    if (on (ParamIDs::actClear)) { setNow (ParamIDs::actClear, 0.0f); clearLayers(); }
    if (on (ParamIDs::actNext)) { setNow (ParamIDs::actNext, 0.0f); stepPreset (1); }

    footswitches.tick (now);
    int mask = 0;
    for (int k = 0; k < 4; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    if (calibrating && now > calibrateUntil)
    {
        calibrating = false;
        if (calHi - calLo > 20)
        {
            // whichever end the pedal started at is the heel
            const bool startedLow = std::abs (calFirst - calLo) < std::abs (calFirst - calHi);
            settings.heel = startedLow ? calLo : calHi;
            settings.toe = startedLow ? calHi : calLo;
            settings.save();
            settingsStamp = PedalSettings::file().getLastModificationTime();
            say ("EXPRESSION CALIBRATED");
        }
        else
            say ("NO MOVEMENT, TRY AGAIN");
    }
}

// =====================================================================================
// Footswitches, laid out after the PLUS Pedal II's own:
//   A  the Brass Pedal: press to catch, release to let go, quick double press clears
//   B  tap: Aux on or off (Latch by default)   hold: next Aux mode (Wet, Latch, Hide)
//   C  tap: Kill Dry on or off                 hold: clear every layer
//   D  tap: next preset                        hold: previous preset
//   A+B  bypass       C+D  hand the footswitches on, hold a second for Keeper
// =====================================================================================
void AfterglowProcessor::onFootEvent (int sw, FootswitchLogic::Event e)
{
    using E = FootswitchLogic;
    if (e == E::Down || e == E::ComboAB || e == E::ComboCD) uiFootPulse.fetch_add (1);

    if (e == E::ComboHoldCD)
    {
        focused.store (this); // Keeper, on the plugin you were playing
        keepNow();
        return;
    }
    if (e == E::ComboCD)
    {
        const SpinLock::ScopedLockType l (focusLock);
        const int i = instances.indexOf (this);
        auto* next = instances.isEmpty() ? nullptr : instances[(i + 1) % instances.size()];
        focused.store (next);
        if (next != nullptr) next->say ("FOOTSWITCHES HERE");
        return;
    }
    if (e == E::ComboAB)
    {
        // A pressed the Brass Pedal the moment it landed; as half of a pair, take that back
        footPedal.store (0.0f);
        setNow (ParamIDs::bypass, on (ParamIDs::bypass) ? 0.0f : 1.0f);
        say (on (ParamIDs::bypass) ? "BYPASSED, LAYERS RING OUT" : "ON");
        return;
    }
    if (e == E::ComboHoldAB) return; // reserved

    switch (sw)
    {
        case 0:
            // momentary, like the brass pedal itself
            if (e == E::Down) footPedal.store (1.0f);
            if (e == E::Tap || e == E::HoldEnd) footPedal.store (0.0f);
            break;
        case 1:
            if (e == E::Tap)
            {
                setNow (ParamIDs::fsOn, on (ParamIDs::fsOn) ? 0.0f : 1.0f);
                say (names::auxNames[jlimit (0, 2, idx (ParamIDs::fsMode))].toUpperCase() + (on (ParamIDs::fsOn) ? " ON" : " OFF"));
            }
            if (e == E::HoldStart)
            {
                const int next = (idx (ParamIDs::fsMode) + 1) % 3;
                setNow (ParamIDs::fsMode, (float) next);
                say ("AUX IS " + names::auxNames[next].toUpperCase());
            }
            break;
        case 2:
            if (e == E::Tap)
            {
                setNow (ParamIDs::killDry, on (ParamIDs::killDry) ? 0.0f : 1.0f);
                say (on (ParamIDs::killDry) ? "DRY OFF, LAYERS ONLY" : "DRY ON");
            }
            if (e == E::HoldStart) clearLayers();
            break;
        case 3:
            if (e == E::Tap) stepPreset (1);
            if (e == E::HoldStart) stepPreset (-1);
            break;
        default: break;
    }
}

void AfterglowProcessor::footDown (int sw)
{
    focused.store (this);
    footswitches.press (sw, Time::getMillisecondCounterHiRes() * 0.001);
}

void AfterglowProcessor::footUp (int sw) { footswitches.release (sw, Time::getMillisecondCounterHiRes() * 0.001); }

// =====================================================================================
// Learn and calibration
// =====================================================================================
void AfterglowProcessor::learnFootswitch (int sw)
{
    learnTarget.store (sw);
    say ("PRESS FOOTSWITCH " + letter (sw));
}

void AfterglowProcessor::learnExpression()
{
    learnTarget.store (LearnExpression);
    say ("MOVE THE EXPRESSION PEDAL");
}

void AfterglowProcessor::learnPedal()
{
    learnTarget.store (LearnPedal);
    say ("PLAY A KEY OR MOVE A STRIP");
}

void AfterglowProcessor::forgetPedal()
{
    pedalNote.store (-1);
    pedalCC.store (-1);
    notePedal.store (0.0f);
    stripPedal.store (0.0f);
    say ("BRASS PEDAL KEY FORGOTTEN");
}

String AfterglowProcessor::pedalSources() const
{
    StringArray s;
    s.add ("FOOTSWITCH A");
    if (idx (ParamIDs::expTarget) == 0) s.add ("EXPRESSION");
    if (on (ParamIDs::sustainCC)) s.add ("SUSTAIN PEDAL");
    if (pedalNote.load() >= 0) s.add ("KEY " + MidiMessage::getMidiNoteName (pedalNote.load(), true, true, 3).toUpperCase());
    if (pedalCC.load() >= 0) s.add ("CC " + String (pedalCC.load()));
    return s.joinIntoString (String::fromUTF8 (" \xc2\xb7 "));
}

void AfterglowProcessor::calibrateExpression()
{
    calibrating = true;
    calibrateUntil = Time::getMillisecondCounterHiRes() * 0.001 + 6.0;
    calLo = 127; calHi = 0; calFirst = -1;
    say ("HEEL DOWN, THEN ROCK TO TOE");
}

void AfterglowProcessor::resetFootswitches()
{
    settings.resetFootswitches();
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    say ("FOOTSWITCH DEFAULTS RESTORED");
}

// =====================================================================================
// Keeper
// =====================================================================================
void AfterglowProcessor::keepNow()
{
    DynamicObject::Ptr e = new DynamicObject();
    e->setProperty ("plugin", "Afterglow");
   #ifdef JucePlugin_VersionString
    e->setProperty ("version", JucePlugin_VersionString);
   #endif
    e->setProperty ("recreates", "Gamechanger Audio PLUS Pedal II");
    e->setProperty ("time", Time::getCurrentTime().toISO8601 (true));
    e->setProperty ("context", settings.keeperLive ? "Live" : "Studio");
    static const char* const banks[4] = { "Red", "Yellow", "Green", "Blue" };
    e->setProperty ("preset", selected < 0 ? String ("none") : String (banks[selected / 4]) + " " + String (selected % 4 + 1));
    static const char* const modes[4] = { "normal", "real piano", "piano group", "piano sostenuto" };
    e->setProperty ("mode", modes[jlimit (0, 3, engine.uiMode.load())]);
    e->setProperty ("layersSounding", engine.uiActive.load());
    e->setProperty ("layersHeld", engine.uiHeld.load());
    e->setProperty ("pedalDown", engine.uiDown.load());
    e->setProperty ("pedalDepth", engine.uiDepth.load());

    DynamicObject::Ptr exp = new DynamicObject();
    const auto& targets = names::expTargets();
    exp->setProperty ("target", targets[(size_t) jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget))].label);
    exp->setProperty ("position", uiExp.load());
    exp->setProperty ("movedSecondsAgo", Time::getMillisecondCounterHiRes() * 0.001 - expMovedAt);
    e->setProperty ("expression", var (exp.get()));

    // every parameter, hidden ones included, as value and as shown on the panel
    static const StringArray skip { ParamIDs::keeper, ParamIDs::actClear, ParamIDs::actNext };
    DynamicObject::Ptr params = new DynamicObject();
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
        {
            if (skip.contains (rp->getParameterID())) continue;
            DynamicObject::Ptr pv = new DynamicObject();
            pv->setProperty ("value", rp->convertFrom0to1 (rp->getValue()));
            pv->setProperty ("shown", rp->getCurrentValueAsText());
            params->setProperty (rp->getParameterID(), var (pv.get()));
        }
    e->setProperty ("parameters", var (params.get()));

    keeper.keep (e);
    uiKeepPulse.fetch_add (1);
    say ("KEPT");
}

File AfterglowProcessor::exportKeepers()
{
    const auto file = keeper.exportZip();
    if (file.existsAsFile()) { say ("EXPORTED TO DOCUMENTS"); file.revealToUser(); }
    else say ("NOTHING KEPT YET");
    return file;
}

void AfterglowProcessor::openKeepersFolder()
{
    Keeper::folder().createDirectory();
    Keeper::folder().startAsProcess();
}

void AfterglowProcessor::learnKeeper()
{
    learnTarget.store (LearnKeeper);
    say ("PRESS YOUR KEEPER SWITCH");
}

void AfterglowProcessor::setKeeperLive (bool live)
{
    settings.keeperLive = live;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

// =====================================================================================
// Presets: sixteen slots in four colour banks
// =====================================================================================
bool AfterglowProcessor::inSnapshot (const String& id)
{
    static const StringArray excluded { ParamIDs::bypass, ParamIDs::pedal, ParamIDs::fsOn, ParamIDs::sustainCC,
                                        ParamIDs::keeper, ParamIDs::actClear, ParamIDs::actNext };
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

void AfterglowProcessor::savePreset (int slot)
{
    if (slot < 0 || slot >= kPresets) return;
    slots[(size_t) slot].params = snapshotOf();
    selected = slot;
}

void AfterglowProcessor::loadPreset (int slot)
{
    if (! hasPreset (slot)) return;
    applySnapshot (slots[(size_t) slot].params);
    selected = slot;
    static const char* const banks[4] = { "RED", "YELLOW", "GREEN", "BLUE" };
    say (String (banks[slot / 4]) + " " + String (slot % 4 + 1));
}

bool AfterglowProcessor::hasPreset (int slot) const
{
    return slot >= 0 && slot < kPresets && slots[(size_t) slot].params.isValid();
}

void AfterglowProcessor::stepPreset (int direction)
{
    for (int k = 1; k <= kPresets; ++k)
    {
        const int slot = ((selected < 0 ? (direction > 0 ? -1 : 0) : selected) + direction * k + kPresets * 2) % kPresets;
        if (hasPreset (slot)) { loadPreset (slot); return; }
    }
    say ("NO PRESETS SAVED YET");
}

AudioProcessorEditor* AfterglowProcessor::createEditor()
{
    focused.store (this); // opening a copy's window gives it the footswitches
    return new AfterglowEditor (*this);
}

// ---- saving: parameters, the 16 presets and a learned Brass Pedal key ----
void AfterglowProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild (i).getType().toString() == "Presets")
            state.removeChild (i, nullptr);

    ValueTree presets ("Presets");
    presets.setProperty ("selected", selected, nullptr);
    for (int i = 0; i < kPresets; ++i)
    {
        ValueTree slot ("Slot");
        if (slots[(size_t) i].params.isValid())
            slot.appendChild (slots[(size_t) i].params.createCopy(), nullptr);
        presets.appendChild (slot, nullptr);
    }
    state.appendChild (presets, nullptr);
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

    const auto presets = tree.getChildWithName ("Presets");
    if (presets.isValid())
    {
        selected = (int) presets.getProperty ("selected", -1);
        for (int i = 0; i < kPresets && i < presets.getNumChildren(); ++i)
        {
            const auto slot = presets.getChild (i);
            slots[(size_t) i].params = slot.getNumChildren() > 0 ? slot.getChild (0).createCopy() : ValueTree();
        }
        tree.removeChild (presets, nullptr);
    }
    // sets saved by the first version kept A / Live / B snapshots; they are not carried over
    for (int i = tree.getNumChildren(); --i >= 0;)
        if (tree.getChild (i).getType().toString() == "Snapshots")
            tree.removeChild (i, nullptr);

    pedalNote.store ((int) tree.getProperty ("pedalNote", -1));
    pedalCC.store ((int) tree.getProperty ("pedalCC", -1));
    apvts.replaceState (tree);

    // never come back with the pedal stuck down or an action half pressed
    for (auto* id : { ParamIDs::pedal, ParamIDs::keeper, ParamIDs::actClear, ParamIDs::actNext })
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (0.0f);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AfterglowProcessor();
}
