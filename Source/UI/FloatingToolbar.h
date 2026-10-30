#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

// 떠 있는 도구 막대: 왼쪽 손잡이(⋮⋮)로 옮기고, 오른쪽 핀으로 위에 붙이거나 떼어 낸다.
// 위쪽 가장자리 가까이 놓아도 붙는다. 담는 버튼들은 이 컴포넌트의 자식으로 추가한다.
class FloatingToolbar : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int barHeight = 36;
    static constexpr int shadow = 3;

    struct Item
    {
        juce::Component* component = nullptr;
        int width = 34;
        bool separatorBefore = false;   // 성격이 다른 묶음 앞에 세로줄 (간격도 넓게)
    };

    std::function<void(juce::Point<int> topLeft)> onMoved;   // 끄는 중 (부모 좌표)
    std::function<void()> onDropped;                          // 끌기를 마침
    std::function<void()> onPinClicked;

    FloatingToolbar();
    void setItems(std::vector<Item>);
    void setDocked(bool);
    bool isDocked() const noexcept { return docked; }
    int preferredWidth() const;
    void layoutItems();

    void paint(juce::Graphics&) override;
    void resized() override { layoutItems(); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    static constexpr int gripWidth = 14, pinWidth = 26, separatorGap = 11;

    juce::Rectangle<int> body() const;
    juce::Rectangle<int> gripArea() const { return body().withWidth(gripWidth); }
    juce::Rectangle<int> pinArea() const { return body().removeFromRight(pinWidth); }

    std::vector<Item> items;
    std::vector<int> separators;    // 세로줄 x 위치
    bool docked = false;
    bool dragging = false;
    juce::Point<int> dragStart;
};
