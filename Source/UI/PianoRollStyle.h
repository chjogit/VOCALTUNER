#pragma once
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

namespace rollstyle
{
// 색은 테마(설정 창)에 따라 바뀐다 (메시지 스레드 전용, 모든 인스턴스 공통). 기본값 = 확정 시안 v3.
inline juce::Colour background    { 0xff121212 };
inline juce::Colour whiteKeyRow   { 0xff1a1a18 };
inline juce::Colour blackKeyRow   { 0xff151514 };
inline juce::Colour gridLine      { 0xff222220 };
inline juce::Colour octaveLine    { 0xff3a3936 };
inline juce::Colour voicedFill    { 0xffa9a497 };   // 블록
inline juce::Colour consonantFill { 0xff6f6b62 };
inline juce::Colour curve         { 0xfff2eee4 };   // F0 곡선
inline juce::Colour playhead      { 0xffe8a33d };
inline juce::Colour accent        { 0xffe8a33d };   // 강조 (켜짐, 진행)
inline juce::Colour ink           { 0xffe9e4d8 };   // 글자
inline juce::Colour inkDim        { 0xff9a958a };
inline juce::Colour inkFaint      { 0xff5f5b53 };
inline juce::Colour panel         { 0xff0e0e0d };   // 왼쪽 패널
inline juce::Colour chrome        { 0xff0c0c0b };   // 떠 있는 도구 막대 / 설정 창
inline juce::Colour chromeEdge    { 0xff3b3934 };
inline float fontScale = 1.0f;

// 기준 트랙 색 (고정): 1 = 네이비, 2 = 베이지
inline const juce::Colour referenceNavy  { 0xff34558a };
inline const juce::Colour referenceBeige { 0xffd8c29a };
inline const juce::Colour logoNavy       { 0xff21213f };

inline juce::Colour referenceColour(int slot) noexcept
{
    return slot == 1 ? referenceNavy : (slot == 2 ? referenceBeige : juce::Colour(0xff2b2a27));
}

inline float fontSize(float size) noexcept { return size * fontScale; }

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
