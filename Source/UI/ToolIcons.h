#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// 도구 버튼용 단색 선 아이콘 (16x16 격자에 그린 벡터 경로)
enum class ToolIcon { None, Move, Cut, Volume, Drift, Vibrato, Flatten, UpDown, LeftRight, Undo, Redo, Balance };

juce::Path makeToolIcon(ToolIcon icon);

// 아이콘 + 이름을 함께 그리는 버튼. 아이콘이 None이면 일반 TextButton과 같다.
class IconTextButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    void setIcon(ToolIcon newIcon);
    ToolIcon getIcon() const noexcept { return icon; }
    void setIconOnly(bool shouldHideText) { iconOnly = shouldHideText; repaint(); }   // 이름은 툴팁으로
    void setBoldText(bool shouldBeBold) { boldText = shouldBeBold; repaint(); }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    juce::Colour currentTextColour() const;

    void paintBoldText(juce::Graphics&);

    ToolIcon icon = ToolIcon::None;
    juce::Path iconPath;
    bool iconOnly = false, boldText = false;
};

// 설정 줄의 톱니바퀴
void drawGearIcon(juce::Graphics&, juce::Rectangle<float> area);
