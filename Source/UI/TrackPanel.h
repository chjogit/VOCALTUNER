#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <vector>
#include "ReferenceFit.h"
#include "TrackTypes.h"

// 왼쪽 패널 (시안 v3): 로고 / 트랙 줄(보정, 기준 색) / 편집 중인 트랙의 역할 / 상태줄 / 설정 / 가이드 모드.
// 오른쪽 가장자리 탭으로 접고 편다. 키보드 포커스를 받지 않는다 (DAW 단축키 통과).
class TrackPanel : public juce::Component, public juce::TooltipClient
{
public:
    struct Row
    {
        const void* id = nullptr;
        juce::String name;
        juce::String detail;        // 둘째 줄: 표시 상태 / 맞춘 기준 / 클립 수
        TrackView view = TrackView::Editable;
        int referenceSlot = 0;      // 0 = 기준 아님, 1 = 네이비, 2 = 베이지
        bool isActive = false;      // 지금 편집 중인 트랙
        bool baseCorrection = true; // 기초 자동 보정 켜짐
    };

    std::function<void(const void* id, TrackView)> onViewChanged;
    std::function<void(const void* id)> onActivate;
    std::function<void(const void* id, int slot)> onReferenceChosen;   // 기준 버튼: 꺼짐 -> 네이비 -> 베이지 -> 꺼짐
    std::function<void(bool)> onAlignmentToggled;                       // 가이드 모드
    std::function<void(const TrackRoles&)> onRolesChanged;      // 편집 중인 트랙의 역할 (슬라이더는 드래그 중에도 호출)
    std::function<void()> onRolesGestureBegin;                  // 슬라이더 드래그 시작 (되돌리기 지점)
    std::function<void()> onFitRequested;                       // 편집 중인 트랙 맞추기
    std::function<void()> onFitAllRequested;                    // 역할이 켜진 모든 트랙 맞추기
    std::function<void()> onFitCancelled;                       // 진행 중인 맞추기 취소
    std::function<void(const void* id, bool on)> onBaseCorrectionToggled;
    std::function<void()> onSettingsClicked;
    std::function<void()> onCollapseToggled;

    static constexpr int expandedWidth = 236;
    static constexpr int collapsedWidth = 16;
    int preferredWidth() const noexcept { return collapsed ? collapsedWidth : expandedWidth; }
    bool isCollapsed() const noexcept { return collapsed; }
    void setCollapsed(bool);
    // 설정 줄의 위치 (편집기가 설정 창을 그 옆에 띄운다)
    juce::Rectangle<int> settingsRowBounds() const;
    void setSettingsOpen(bool);

    TrackPanel();

    void setRows(std::vector<Row>);
    void setAlignmentStatus(bool active, const juce::String& status);
    // 맞추기 진행률 0..1 (음수 = 진행 중 아님). 상태줄에 로딩바와 취소 버튼을 보여준다
    void setFitProgress(double progress, const juce::String& stage);
    void setFitBlocked(bool blocked);       // 분석 중에는 맞추기 버튼을 막는다
    void setAnalysisProgress(double fraction);  // 발음·호흡 분석 진행 막대 (0..1, 음수 = 없음)
    // 편집 중인 트랙 정보: 기준이 있고 편집 트랙이 기준이 아니면 역할 설정을 보여준다
    void setActiveRoles(const TrackRoles&, bool canEditRoles, const juce::String& referenceName);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    static constexpr int headerHeight = 66;
    static constexpr int rowHeight = 46;
    static constexpr int footerRowHeight = 46;

    juce::Rectangle<int> rowBounds(size_t index) const;
    juce::Rectangle<int> detailText(size_t index) const;
    juce::Rectangle<int> referenceBox(size_t index) const;
    juce::Rectangle<int> correctionBox(size_t index) const;
    juce::Rectangle<int> collapseTab() const;
    juce::Rectangle<int> guideRowBounds() const;
    juce::Rectangle<int> statusArea() const;
    int rowsBottom() const noexcept;
    void paintHeader(juce::Graphics&);
    void paintRow(juce::Graphics&, size_t index);
    void paintChip(juce::Graphics&, juce::Rectangle<int>, const char* text, juce::Colour fill, juce::Colour textColour, bool outlined);
    void paintRoleLabels(juce::Graphics&);
    void paintFooter(juce::Graphics&);
    void paintStatus(juce::Graphics&);
    void paintCollapseTab(juce::Graphics&);
    void layoutDetails();
    void notifyRoles();
    void syncRoleControls();
    void handleRowClick(size_t index, juce::Point<int>);

    std::vector<Row> rows;
    bool alignmentActive = false;
    juce::String status;
    bool rolesEditable = false;
    juce::String referenceName;
    TrackRoles roles;
    bool syncing = false;
    double fitProgress = -1.0;
    bool fitBlocked = false;
    double analysisProgress = -1.0;
    void paintBar(juce::Graphics&, juce::Rectangle<float> bar, double fraction, const juce::String& label, bool moving);
    void updateFitButtons();
    juce::String fitStage;
    bool collapsed = false;
    bool settingsOpen = false;

    juce::TextButton timingToggle, pitchToggle, relationButton, fitButton, fitAllButton, cancelButton;
    struct LabeledSlider
    {
        juce::Slider slider;
        juce::String label;
    };
    std::array<LabeledSlider, 3> sliders;   // 박자 / 음정 / 이음
    juce::Rectangle<int> detailsArea;
};
