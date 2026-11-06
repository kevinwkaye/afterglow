// Footswitch and expression test: drives the real plugin with plain MIDI, the way the
// footswitch and expression pedal would, and checks every job in the footswitch table.
//
// It also checks the clash the other pedals had: the original pedal's own CC chart must stay
// off unless a channel is chosen for it, so footswitches A to D (CC 20 to 23) and the
// expression pedal (CC 11, which the chart uses for Decay) do only their own jobs.
//
// Usage: afterglow_pedal_test

#include "PluginProcessor.h"
#include <cstdio>

using namespace juce;

namespace
{
int failures = 0;

void expect (bool ok, const String& what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

struct Rig
{
    AfterglowProcessor proc;
    AudioBuffer<float> buffer { 2, 128 };
    int k = 0;

    Rig()
    {
        proc.setPlayConfigDetails (2, 2, 48000.0, 128);
        proc.prepareToPlay (48000.0, 128);
    }

    void send (const MidiMessage& m, int waitMs = 30)
    {
        MidiBuffer midi;
        midi.addEvent (m, 0);
        fill();
        proc.processBlock (buffer, midi);
        run (waitMs);
    }

    void cc (int number, int value, int channel = 16, int waitMs = 30) { send (MidiMessage::controllerEvent (channel, number, value), waitMs); }

    // a plucked A, so there is something to catch
    void fill()
    {
        for (int i = 0; i < 128; ++i)
        {
            const float v = 0.2f * std::sin ((float) (k++) * 2.0f * MathConstants<float>::pi * 220.0f / 48000.0f);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
    }

    // plays audio through, and lets the panel timers run
    void run (int ms)
    {
        const double end = Time::getMillisecondCounterHiRes() + ms;
        while (Time::getMillisecondCounterHiRes() < end)
        {
            fill();
            MidiBuffer none;
            proc.processBlock (buffer, none);
            MessageManager::getInstance()->runDispatchLoopUntil (3);
        }
    }

    float value (const char* id) { return proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getValue()); }
    bool pedalDown() { return proc.getEngine().uiDown.load(); }
    void press (int sw, int holdMs = 60) { cc (20 + sw, 127, 16, holdMs); cc (20 + sw, 0); }
    void both (int a, int b, int holdMs)
    {
        MidiBuffer midi;
        midi.addEvent (MidiMessage::controllerEvent (16, 20 + a, 127), 0);
        midi.addEvent (MidiMessage::controllerEvent (16, 20 + b, 127), 1);
        fill();
        proc.processBlock (buffer, midi);
        run (holdMs);
        cc (20 + a, 0); cc (20 + b, 0, 16, 200);
    }
};
} // namespace

int main()
{
    ScopedJuceInitialiser_GUI juce;
    // test the defaults whatever this computer has saved, and put its settings back afterwards
    const auto settingsFile = PedalSettings::file();
    const bool hadSettings = settingsFile.existsAsFile();
    const String savedSettings = hadSettings ? settingsFile.loadFileAsString() : String();
    settingsFile.deleteFile();
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    r.run (200);

    std::printf ("Clash test, original pedal map off\n");
    const float decay = r.value (ParamIDs::decay);
    for (int ch : { 1, 5, 15 })
        for (int n : { 11, 20, 21, 22, 23 })
        {
            r.cc (n, 127, ch, 10);
            expect (! r.pedalDown(), "CC " + String (n) + " on channel " + String (ch) + " leaves the Brass Pedal up");
            r.cc (n, 0, ch, 10);
        }
    expect (r.value (ParamIDs::decay) == decay, "CC 11 off the expression channel leaves Decay alone");
    expect (r.value (ParamIDs::bypass) < 0.5f && r.value (ParamIDs::killDry) < 0.5f && r.value (ParamIDs::fsOn) < 0.5f,
            "nothing bypassed, no dry killed, no Aux switched");

    std::printf ("Footswitch A, the Brass Pedal\n");
    r.cc (20, 127, 16, 900);
    expect (r.pedalDown(), "press A and the pedal goes down");
    expect (r.proc.getEngine().uiActive.load() > 0, "and catches what you play");
    r.cc (20, 0, 16, 100);
    expect (! r.pedalDown(), "let go of A and the pedal comes up");
    expect (r.value (ParamIDs::bypass) < 0.5f, "no bypass");
    r.press (0, 40);
    expect (! r.pedalDown(), "a quick tap leaves it up afterwards");

    std::printf ("Footswitch B, the Aux\n");
    const float auxMode = r.value (ParamIDs::fsMode);
    r.press (1);
    expect (r.value (ParamIDs::fsOn) > 0.5f, "tap B switches the Aux on");
    r.press (1);
    expect (r.value (ParamIDs::fsOn) < 0.5f, "tap B again switches it off");
    r.cc (21, 127, 16, 700);
    r.cc (21, 0);
    expect (r.value (ParamIDs::fsMode) == std::fmod (auxMode + 1.0f, 3.0f), "hold B steps to the next Aux mode");
    expect (r.value (ParamIDs::fsOn) < 0.5f, "without switching it on");

    std::printf ("Footswitch C, Kill Dry and Clear\n");
    r.press (2);
    expect (r.value (ParamIDs::killDry) > 0.5f, "tap C kills the dry");
    r.press (2);
    expect (r.value (ParamIDs::killDry) < 0.5f, "tap C again brings it back");
    r.cc (20, 127, 16, 600); r.cc (20, 0, 16, 100);
    const int clears = r.proc.getEngine().uiClears.load();
    r.cc (22, 127, 16, 700);
    r.cc (22, 0, 16, 200);
    expect (r.proc.getEngine().uiClears.load() > clears, "hold C clears the layers");
    expect (r.value (ParamIDs::killDry) < 0.5f, "without touching Kill Dry");

    std::printf ("Footswitch D, presets\n");
    r.proc.savePreset (0);
    r.proc.savePreset (1);
    r.proc.savePreset (2);
    r.proc.loadPreset (0);
    r.run (50);
    r.press (3);
    expect (r.proc.selectedPreset() == 1, "tap D loads the next preset");
    r.cc (23, 127, 16, 700);
    r.cc (23, 0);
    expect (r.proc.selectedPreset() == 0, "hold D goes back one");

    std::printf ("Combos\n");
    r.both (0, 1, 60);
    expect (r.value (ParamIDs::bypass) > 0.5f, "A+B bypasses");
    expect (! r.pedalDown(), "and A's press is taken back, the pedal is up");
    expect (r.value (ParamIDs::fsOn) < 0.5f, "and B's tap doesn't switch the Aux");
    r.both (0, 1, 60);
    expect (r.value (ParamIDs::bypass) < 0.5f, "A+B again switches back on");

    const int keptBefore = r.proc.lastKeeper();
    r.both (2, 3, 1300);
    expect (r.proc.lastKeeper() > keptBefore, "hold C+D for a second saves a Keeper");
    expect (r.value (ParamIDs::killDry) < 0.5f && r.proc.selectedPreset() == 0, "and neither C nor D does its own job");

    std::printf ("Expression\n");
    r.cc (11, 127, 16, 100);
    expect (r.pedalDown(), "toe down presses the Brass Pedal");
    expect (r.proc.getEngine().uiDepth.load() > 0.9f, "all the way");
    r.cc (11, 64, 16, 100);
    const float half = r.proc.getEngine().uiDepth.load();
    expect (half > 0.3f && half < 0.7f, "half way is a half pedal (" + String (half, 2) + ")");
    r.cc (11, 0, 16, 100);
    expect (! r.pedalDown(), "heel down lets it up");
    expect (r.value (ParamIDs::decay) == decay, "Decay untouched (the original chart uses CC 11 for it)");

    std::printf ("Keyboard sustain pedal\n");
    r.cc (64, 127, 1, 100);
    expect (r.pedalDown(), "CC 64 from the keyboard presses the Brass Pedal");
    r.cc (64, 0, 1, 100);
    expect (! r.pedalDown(), "and lets it up");

    std::printf ("Original pedal MIDI map on channel 5\n");
    r.proc.pedals().pedalMapChannel = 5;
    r.cc (11, 127, 5);
    expect (r.value (ParamIDs::decay) > 0.98f, "CC 11 on channel 5 sets Decay, as on the pedal");
    r.cc (17, 127, 5);
    expect (r.value (ParamIDs::killDry) > 0.5f, "CC 17 on channel 5 kills the dry");
    r.cc (17, 0, 5);
    r.cc (20, 127, 16, 300);
    expect (r.pedalDown(), "CC 20 on the footswitch channel is still footswitch A");
    r.cc (20, 0, 16, 100);
    r.cc (11, 127, 16, 100);
    expect (r.pedalDown(), "and CC 11 on channel 16 is still the expression pedal");
    r.cc (11, 0, 16, 100);

    rig.reset();
    if (hadSettings) settingsFile.replaceWithText (savedSettings);
    else settingsFile.deleteFile();
    std::printf (failures == 0 ? "All footswitch checks passed\n" : "%d footswitch check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
