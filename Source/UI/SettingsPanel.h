#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Theme.h"

// 설정 창 (톱니바퀴): 블록 / 곡선 / 배경 색과 글씨 크기. 고르면 바로 적용된다.
class SettingsPanel : public juce::Component
{
public:
    static constexpr int preferredWidth = 270;
    static constexpr int preferredHeight = 176;

    std::function<void(const ThemeChoice&)> onChanged;

    SettingsPanel();
    void setChoice(const ThemeChoice&);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    enum class Row { Block, Curve, Background, Font };

    juce::Rectangle<int> rowArea(Row) const;
    juce::Rectangle<int> optionBox(Row, int index) const;
    static int optionCount(Row) noexcept;
    int& selected(Row) noexcept;
    juce::Colour swatch(Row, int index) const;
    void paintRow(juce::Graphics&, Row, const char* name);

    ThemeChoice choice;
};
