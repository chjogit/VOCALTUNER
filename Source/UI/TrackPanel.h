#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <vector>
#include "ReferenceFit.h"
#include "TrackTypes.h"

// 왼쪽 트랙 메뉴: 트랙별 표시 상태(편집 / 보기 / 숨김), 레퍼런스 지정, 정렬 기능 켜기,
// 그리고 편집 중인 트랙의 역할(박자 / 음정) 설정. 키보드 포커스를 받지 않는다 (DAW 단축키 통과).
class TrackPanel : public juce::Component
{
public:
    struct Row
    {
        const void* id = nullptr;
        juce::String name;
        TrackView view = TrackView::Editable;
        bool isReference = false;
        bool isActive = false;      // 지금 편집 중인 트랙
        bool baseCorrection = true; // 기초 자동 보정 켜짐
    };

    std::function<void(const void* id, TrackView)> onViewChanged;
    std::function<void(const void* id)> onActivate;
    std::function<void(const void* id)> onReferenceChosen;      // 같은 트랙을 다시 누르면 nullptr
    std::function<void(bool)> onAlignmentToggled;
    std::function<void(const TrackRoles&)> onRolesChanged;      // 편집 중인 트랙의 역할 (슬라이더는 드래그 중에도 호출)
    std::function<void()> onRolesGestureBegin;                  // 슬라이더 드래그 시작 (되돌리기 지점)
    std::function<void()> onFitRequested;                       // 편집 중인 트랙 맞추기
    std::function<void()> onFitAllRequested;                    // 역할이 켜진 모든 트랙 맞추기
    std::function<void()> onFitCancelled;                       // 진행 중인 맞추기 취소
    std::function<void(const void* id, bool on)> onBaseCorrectionToggled;

    static constexpr int preferredWidth = 230;

    TrackPanel();

    void setRows(std::vector<Row>);
    void setAlignmentStatus(bool active, const juce::String& status);
    // 맞추기 진행률 0..1 (음수 = 진행 중 아님). 상태줄에 로딩바와 취소 버튼을 보여준다
    void setFitProgress(double progress, const juce::String& stage);
    // 편집 중인 트랙 정보: 레퍼런스가 있고 편집 트랙이 레퍼런스가 아니면 역할 설정을 보여준다
    void setActiveRoles(const TrackRoles&, bool canEditRoles, const juce::String& referenceName);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    static constexpr int headerHeight = 60;
    static constexpr int rowHeight = 26;

    juce::Rectangle<int> rowBounds(size_t index) const;
    juce::Rectangle<int> viewBox(size_t index) const;
    juce::Rectangle<int> referenceBox(size_t index) const;
    juce::Rectangle<int> correctionBox(size_t index) const;
    juce::Rectangle<int> statusArea() const;
    void paintStatus(juce::Graphics&);
    void layoutDetails();
    void notifyRoles();
    void syncRoleControls();

    std::vector<Row> rows;
    bool alignmentActive = false;
    juce::String status;
    bool rolesEditable = false;
    juce::String referenceName;
    TrackRoles roles;
    bool syncing = false;
    double fitProgress = -1.0;
    juce::String fitStage;

    juce::TextButton alignmentButton;
    juce::TextButton timingToggle, pitchToggle, relationButton, fitButton, fitAllButton, cancelButton;
    struct LabeledSlider
    {
        juce::Slider slider;
        juce::String label;
    };
    std::array<LabeledSlider, 3> sliders;   // 박자 정도 / 음정 정도 / 이음 정도
    juce::Rectangle<int> detailsArea;
};
