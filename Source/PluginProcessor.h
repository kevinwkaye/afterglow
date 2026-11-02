#pragma once

#include "Params.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

class AfterglowProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
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
    void clearLayers() { engine.requestClear(); }
    void savePresetSlot (int slot);
    bool hasPresetSlot (int slot) const;

    // MIDI learn for the pedal: the next note becomes the pedal key, the next
    // controller (a touch strip, say) becomes a half-pedal
    void startLearn() { learning.store (true); }
    int learnState() const; // 0 idle, 1 listening, 2 just learned
    juce::String pedalSources() const;
    float midiDepth() const { return std::max ({ midiPedal.load(), notePedal.load(), stripPedal.load() }); }

private:
    void timerCallback() override;
    ag::EngineParams readParams();
    void handleMidi (const juce::MidiMessage&);
    void setNow (const juce::String& id, float plainValue);
    juce::ValueTree snapshotOf() const;
    void applySnapshot (const juce::ValueTree&);
    static bool inSnapshot (const juce::String& id);

    ag::AfterglowEngine engine;

    std::map<juce::String, std::atomic<float>*> raw;
    float f (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }
    bool on (const char* id) const { return f (id) > 0.5f; }
    int idx (const char* id) const { return (int) std::lround (f (id)); }

    // pedal sources from MIDI
    std::atomic<float> midiPedal { 0.0f }, notePedal { 0.0f }, stripPedal { 0.0f };
    std::atomic<int> pedalNote { 36 }, pedalCC { -1 };
    std::atomic<bool> learning { false };
    std::atomic<int> learnedFlag { 0 };
    double learnFlashUntil = 0;

    // MIDI CC changes are made on the message thread
    struct Pending { const char* id = nullptr; float value = 0; };
    juce::AbstractFifo fifo { 256 };
    std::array<Pending, 256> pending;
    void queueValue (const char* id, float plainValue);

    int lastSlot = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterglowProcessor)
};
