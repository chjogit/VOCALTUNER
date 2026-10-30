#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// 시안 v3: 둥근 모서리와 그라데이션 없이 납작한 사각 버튼, 가는 슬라이더.
// 버튼 속성 "flat" = true 이면 테두리 없이 (떠 있는 도구 막대의 아이콘 버튼)
class SharpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SharpLookAndFeel();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& background,
                              bool highlighted, bool down) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    void refreshColours();   // 테마가 바뀌면 다시 부른다
};
