#pragma once

#include "Params.h"
#include "PedalControl.h"
#include "Keeper.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

class AfterglowProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kPresets = 16;

    AfterglowProcessor();
    ~AfterglowProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Afterglow"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 25.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    ag::AfterglowEngine& getEngine() { return engine; }
    const ag::AfterglowEngine& getEngine() const { return engine; }

    // ---- called from the panel (message thread) ----
    void clearLayers();
    void savePreset (int slot);
    void loadPreset (int slot);
    bool hasPreset (int slot) const;
    void stepPreset (int direction);
    int selectedPreset() const { return selected; }

    // ---- footswitches and expression ----
    void footDown (int sw);   // the on-screen A to D squares act exactly like the footswitches
    void footUp (int sw);
    PedalSettings& pedals() { return settings; }
    void learnFootswitch (int sw);
    void learnExpression();
    void learnPedal();        // a key or a controller (a touch strip, say) for the Brass Pedal
    void forgetPedal();
    void calibrateExpression();
    void resetFootswitches();
    void handleDirectMidi (const juce::MidiMessage&);   // MIDI thread
    bool pedalsDirect() const { return direct->connected(); }
    juce::String footInputStatus() const { return direct->status(); }
    juce::String footDevice() const { return settings.footDevice; }
    void setFootDevice (const juce::String& device);
    std::atomic<bool> hostFeedsPedals { false };
    bool isLearning() const { return learnTarget != LearnNone; }
    int learningWhat() const { return learnTarget; }
    bool isCalibrating() const { return calibrating; }
    enum { LearnNone = -1, LearnExpression = 4, LearnKeeper = 5, LearnPedal = 6 };
    float expressionLevel() const { return uiExp.load(); }
    double lastExpressionMove() const { return expMovedAt; }
    juce::String pedalSources() const;   // what plays the Brass Pedal besides the screen
    float pedalFromOutside() const;       // the depth the footswitch, expression and MIDI ask for

    // ---- Keeper: log this moment, the last 8 seconds of audio included ----
    void keepNow();
    void addKeeperNote (int number, const juce::String& note) { keeper.addNote (number, note); }
    juce::File exportKeepers();
    void openKeepersFolder();
    void learnKeeper();
    bool keeperLive() const { return settings.keeperLive; }
    void setKeeperLive (bool live);
    int lastKeeper() const { return keeper.lastNumber(); }

    // ---- what the panel and Pebble show ----
    std::atomic<int> uiFootPulse { 0 };   // counts footswitch presses, Pebble taps its foot
    std::atomic<int> uiFootDown { 0 };    // bitmask of footswitches held now
    std::atomic<int> uiKeepPulse { 0 };   // counts Keepers, Pebble nods
    juce::String uiMessage;               // a short line for Pebble's status
    double uiMessageAt = 0.0;
    void say (const juce::String& s);

private:
    void timerCallback() override;
    ag::EngineParams readParams();
    void handleMidi (const juce::MidiMessage&, bool fromDevice);
    void onFootEvent (int sw, FootswitchLogic::Event);
    void setNow (const juce::String& id, float plainValue);
    void setNorm (const juce::String& id, float v01);
    juce::ValueTree snapshotOf() const;
    void applySnapshot (const juce::ValueTree&);
    static bool inSnapshot (const juce::String& id);

    ag::AfterglowEngine engine;
    Keeper keeper;

    std::map<juce::String, std::atomic<float>*> raw;
    float f (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }
    bool on (const char* id) const { return f (id) > 0.5f; }
    int idx (const char* id) const { return (int) std::lround (f (id)); }

    // the Brass Pedal's other sources, combined with the on-screen pedal
    std::atomic<float> footPedal { 0.0f }, expPedal { 0.0f }, sustainPedal { 0.0f }, notePedal { 0.0f }, stripPedal { 0.0f }, mapPedal { 0.0f };
    std::atomic<int> pedalNote { -1 }, pedalCC { -1 };

    // MIDI handed to the message thread, from Ableton (audio thread) or the footswitch's own cable
    struct Pending { int kind = 0; int a = 0; float v = 0; const char* id = nullptr; };
    enum PendingKind { FootPress, FootRelease, Expression, SetNorm, SetPlain, LoadPreset, Learned, LearnedNote, KeepCC };
    juce::AbstractFifo fifo { 512 };
    std::array<Pending, 512> pending;
    juce::AbstractFifo directFifo { 512 };
    std::array<Pending, 512> directPending;
    juce::SpinLock directWriteLock;
    juce::SharedResourcePointer<DirectMidiInput> direct;
    juce::Time settingsStamp;
    bool wasDirect = false;
    double nextSettingsCheck = 0.0;
    void queue (int kind, int a, float v);
    void queueParam (int kind, const char* id, float v);

    struct Slot { juce::ValueTree params; };
    std::array<Slot, kPresets> slots;
    int selected = -1;

    PedalSettings settings;
    FootswitchLogic footswitches;
    std::atomic<int> learnTarget { LearnNone };
    bool calibrating = false;
    double calibrateUntil = 0.0;
    int calLo = 127, calHi = 0, calFirst = -1;
    double expMovedAt = 0.0;
    std::atomic<float> uiExp { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterglowProcessor)
};
