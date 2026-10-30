#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "BlockTransforms.h"

// 음정 균형 패널 (멜로다인 Correct Pitch 방식): 중심 음정 / 음정 흐름 슬라이더, 조 선택, 허용 범위.
// 피아노롤 위에 떠 있고 키보드 포커스를 잡지 않는다.
class PitchBalancePanel : public juce::Component
{
public:
    std::function<void()> onGestureBegin;                       // 슬라이더 드래그 / 조 변경 시작 (되돌리기 지점)
    std::function<void(const PitchBalance&)> onChanged;         // 값이 바뀔 때마다 (드래그 중에도)
    std::function<void()> onClosed;

    static constexpr int preferredWidth = 300;
    static constexpr int preferredHeight = 132;

    PitchBalancePanel();

    void setBalance(const PitchBalance&);       // 알림 없이 표시만 바꾼다
    const PitchBalance& getBalance() const noexcept { return balance; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void notify();

    PitchBalance balance;
    bool syncing = false;
    juce::Slider centerSlider, driftSlider;
    juce::ComboBox keyBox;
    juce::ToggleButton minorToggle, toleranceToggle;
    juce::TextButton closeButton;
};
