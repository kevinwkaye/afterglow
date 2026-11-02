#pragma once

#include "engine/AfterglowEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Every control on the Afterglow panel. All are automatable, MIDI-mappable in
// Ableton and saved with the set.
namespace ParamIDs
{
inline constexpr auto level = "level";
inline constexpr auto layers = "layers";
inline constexpr auto rise = "rise";
inline constexpr auto decay = "decay";
inline constexpr auto tone = "tone";
inline constexpr auto resonance = "resonance";
inline constexpr auto input = "input";
inline constexpr auto catchMode = "catchMode";
inline constexpr auto holdMode = "holdMode";
inline constexpr auto fxPos = "fxPos";
inline constexpr auto fxType = "fxType";
inline constexpr auto fxAmount = "fxAmount";
inline constexpr auto fxMix = "fxMix";
inline constexpr auto killDry = "killDry";
inline constexpr auto fsMode = "fsMode";
inline constexpr auto fsOn = "fsOn";
inline constexpr auto pedal = "pedal";
inline constexpr auto bypass = "bypass";
inline constexpr auto presetSlot = "presetSlot";
} // namespace ParamIDs

namespace names
{
inline juce::String seconds (float s)
{
    if (s < 1.0f) return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";
    return juce::String (s, s < 10.0f ? 1 : 0) + " s";
}

inline juce::String riseText (float v) { return seconds (ag::Times::rise (v, false)); }
inline juce::String decayText (float v) { return ag::Times::decayInfinite (v) ? juce::String ("Infinite") : seconds (ag::Times::decay (v)); }

inline juce::String resonanceText (float v)
{
    const int pct = juce::roundToInt (std::abs (v) * 100.0f);
    if (pct == 0) return "Off";
    return (v > 0 ? "+" : "-") + juce::String (pct) + (v > 0 ? " Upper" : " Lower");
}

inline juce::String dbText (float v) { return std::abs (v) < 0.05f ? juce::String ("0 dB") : (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
} // namespace names

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using P = AudioParameterFloat;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pctText = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto knob = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<P> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def, pctText));
    };
    auto knobWith = [&] (const char* id, const char* name, NormalisableRange<float> range, float def, std::function<String (float, int)> text)
    {
        layout.add (std::make_unique<P> (ParameterID { id, 1 }, name, range, def,
                                         AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
    };
    auto toggle = [&] (const char* id, const char* name, bool def) { layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto choice = [&] (const char* id, const char* name, StringArray items, int def) { layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };

    // ---- the pedal's six knobs ----
    knobWith (ParamIDs::level, "Level", { -24.0f, 6.0f }, 0.0f, [] (float v, int) { return names::dbText (v); });
    choice (ParamIDs::layers, "Layers", { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "Infinite" }, 3);
    knobWith (ParamIDs::rise, "Rise", { 0.0f, 1.0f }, 0.2f, [] (float v, int) { return names::riseText (v); });
    knobWith (ParamIDs::decay, "Decay", { 0.0f, 1.0f }, 0.6f, [] (float v, int) { return names::decayText (v); });
    knob (ParamIDs::tone, "Tone", 0.75f);
    knobWith (ParamIDs::resonance, "Resonance", { -1.0f, 1.0f }, 0.0f, [] (float v, int) { return names::resonanceText (v); });
    knobWith (ParamIDs::input, "Input", { -12.0f, 12.0f }, 0.0f, [] (float v, int) { return names::dbText (v); });

    // ---- switches ----
    choice (ParamIDs::catchMode, "Catch", { "Single", "Group", "All" }, 2);
    choice (ParamIDs::holdMode, "Hold", { "None", "First", "All" }, 2);
    choice (ParamIDs::fxPos, "FX Loop", { "Pre", "Loop", "Post" }, 1);
    choice (ParamIDs::fxType, "FX Type", { "Verb", "Delay", "Chorus", "Shimmer" }, 0);
    knob (ParamIDs::fxAmount, "FX Amount", 0.55f);
    knob (ParamIDs::fxMix, "FX Mix", 0.3f);
    toggle (ParamIDs::killDry, "Kill Dry", false);
    choice (ParamIDs::fsMode, "Footswitch Mode", { "Wet", "Latch", "Hide" }, 0);
    toggle (ParamIDs::fsOn, "Footswitch", false);

    // ---- the brass pedal and the rest ----
    knob (ParamIDs::pedal, "Brass Pedal", 0.0f);
    toggle (ParamIDs::bypass, "Bypass", false);
    choice (ParamIDs::presetSlot, "Preset", { "A", "Live", "B" }, 1);

    return layout;
}
