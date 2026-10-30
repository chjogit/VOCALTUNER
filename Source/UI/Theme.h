#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// 설정 창에서 고르는 테마. 고른 값은 사용자 설정 파일에 저장되어 다음에 열 때도 유지된다.
struct ThemeChoice
{
    int block = 0, curve = 0, background = 0, font = 1;   // 각 목록의 번호 (글씨: 0 작게 / 1 보통 / 2 크게)
};

namespace theme
{
inline constexpr std::array<juce::uint32, 4> blockColours { 0xffa9a497, 0xffb9c2c4, 0xffd08b5b, 0xff9fb79a };
inline constexpr std::array<juce::uint32, 3> curveColours { 0xfff2eee4, 0xffe8a33d, 0xff9a958a };
inline constexpr std::array<juce::uint32, 3> backgroundColours { 0xff121212, 0xff1d1f22, 0xffe7e2d6 };
inline constexpr std::array<float, 3> fontScales { 0.9f, 1.0f, 1.15f };

ThemeChoice clamp(ThemeChoice) noexcept;
void apply(const ThemeChoice&);     // rollstyle 색을 바꾼다
ThemeChoice load();
void save(const ThemeChoice&);
} // namespace theme
