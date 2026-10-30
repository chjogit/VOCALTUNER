#pragma once
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

namespace rollstyle
{
inline const juce::Colour background   { 0xff1b1c20 };
inline const juce::Colour whiteKeyRow { 0xff23252b };
inline const juce::Colour blackKeyRow { 0xff1d1e23 };
inline const juce::Colour gridLine    { 0x18ffffff };
inline const juce::Colour octaveLine  { 0x40ffffff };
inline const juce::Colour voicedFill  { 0xff4f8cff };
inline const juce::Colour consonantFill { 0xff9aa3b2 };
inline const juce::Colour playhead    { 0xffff7a3d };
inline const juce::Colour accent      { 0xff4ade80 };

inline bool isBlackKey(int midi) noexcept
{
    const auto pitchClass = ((midi % 12) + 12) % 12;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

inline juce::String noteName(int midi)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
}

inline juce::String formatTime(double sec)
{
    const auto total = static_cast<int>(std::floor(sec));
    const auto text = juce::String(std::abs(total) / 60) + ":" + juce::String(std::abs(total) % 60).paddedLeft('0', 2);
    return total < 0 ? "-" + text : text;
}

inline double dbToLinear(double db) noexcept { return std::pow(10.0, db / 20.0); }

// 블롭 두께(반음 단위): 소리(진폭)가 클수록 두껍다
inline double thicknessSemitones(bool consonant, double amplitude) noexcept
{
    return consonant ? 0.15 + 0.3 * std::tanh(1.5 * amplitude) : 0.3 + 0.6 * std::tanh(1.5 * amplitude);
}
} // namespace rollstyle
