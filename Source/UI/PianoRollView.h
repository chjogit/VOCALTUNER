#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "BlockParser.h"
#include "BlockTransforms.h"
#include "PitchBalancePanel.h"
#include "RollViewport.h"
#include "ToolIcons.h"
#include "TrackTypes.h"
#include "VocalFeatures.h"

// 위에서부터: 타임라인(눈금자) / 모드·도구 메뉴 / 피아노롤.
// 타임라인: 클릭 = 재생 위치 이동(호스트 연동), 드래그 = 좌우 이동 (위로 끌면 확대, 아래로 끌면 축소), 휠 = 확대/축소
// 왼쪽 음 눈금: 드래그 = 상하 이동, 휠 = 세로 확대/축소
// 피아노롤: 휠 = 가로 확대, Ctrl+휠 = 세로, Alt+Shift+휠 = 커서 기준 가로·세로, Shift+휠 = 좌우 이동,
//          Alt+드래그 또는 휠 클릭 드래그 = 화면 이동, 더블클릭(빈 곳) = 전체 맞춤
// 블롭/곡선/핸들을 끄는 도중 Ctrl을 누르면 미세 조정 (움직임 1/10, 반음 스냅 없음)
//
// 모드: Ctrl+1 블롭 / Ctrl+2 박자 / Ctrl+3 곡선 (또는 버튼). 숫자 키 1~6은 현재 모드의 도구를 고른다.
// Ctrl+4 (또는 '음정 균형' 버튼): 중심 음정 / 음정 흐름 / 조 패널
//  블롭: 이동(상하 = 음정, 더블클릭 = 음정 가운데로 정렬, 블롭 사이 선 = 이음 정도.
//             이미 선택된 블롭은 좌우로 끌면 타이밍 이동, 양 끝을 끌면 길이) / 자르기·붙이기 / 볼륨
//        곡선 도구(▾ 다시 누르면 선택): 음정 흐름(기본, 멜로다인 Pitch Drift: 세로 드래그 = 느린 흔들림 줄이기/늘리기,
//             더블클릭 = 0%/100%) / 비브라토(세로 = 깊이, 가로 = 속도) / 펴기(세로 = 펴는 정도)
//  박자: 음정은 노래 평균 음정에 고정(F0 곡선은 그대로). 이동(타이밍) / 자르기 / 볼륨
//  곡선: 블롭을 선택하면 곡선의 꺾이는 지점에 핸들. 더블클릭 = 핸들 추가/삭제. 1 상하 / 2 좌우로 핸들 이동
// 다중 선택: 빈 곳 드래그(범위), Ctrl+클릭, Ctrl+A
class PianoRollView : public juce::Component
{
public:
    struct Content
    {
        std::shared_ptr<const VocalFeatures> features;
        std::shared_ptr<const std::vector<VocalBlock>> blocks;
        double offsetSec = 0.0;         // 곡 시간 = 소스 시간 + offsetSec
        double regionSongStartSec = 0.0;
        double regionSongEndSec = 0.0;
        juce::String message;           // features가 없을 때 표시할 안내
    };

    // 편집 중인 트랙 뒤에 겹쳐 그리는 다른 트랙들. Editable 트랙의 블롭을 누르면 onLayerPicked로 그 트랙으로 전환.
    struct Layer
    {
        const void* id = nullptr;
        Content content;
        TrackView view = TrackView::Visible;
        int colourIndex = 0;
    };

    enum class Mode { Blob, Curve, Rhythm };      // Ctrl+1 / Ctrl+2 / Ctrl+3 순서
    enum class BlobTool { Move, Cut, Volume, Timing, Vibrato, Flatten, Drift };
    enum class CurveTool { Vertical, Horizontal };

    std::function<void(double songSec)> onSeekRequested;                      // 타임라인 클릭
    std::function<void(double songSec, double toleranceSec)> onCutRequested;  // 자르기 도구 더블클릭
    std::function<void()> onShapeSettled;                                     // 펴기/비브라토 동작이 끝남 (곡선에 굳히기)
    std::function<void()> onEditGestureBegin;                                 // 편집 시작 (되돌리기 지점)
    std::function<void(const std::vector<BlockUpdate>&)> onBlocksEdited;      // 드래그 중 계속 호출
    std::function<void()> onUndo, onRedo;
    std::function<void(const std::vector<size_t>& indices, bool toFirst)> onRevertRequested;
    std::function<void(size_t index)> onPreviewStart;                        // 블록을 누르는 동안 미리 듣기
    std::function<void()> onPreviewStop;   // 우클릭: 처음으로 / 한 단계
    std::function<void(const void* layerId)> onLayerPicked;                  // 다른 편집 가능 트랙의 블롭 클릭

    PianoRollView();

    void setContent(Content newContent, bool refit);
    void setLayers(std::vector<Layer>);
    void setPlayhead(double songSec, bool isPlaying);
    void setUndoRedoEnabled(bool canUndo, bool canRedo);
    const std::vector<size_t>& selectedBlocks() const noexcept { return selection; }
    // 숫자 키 1~5, Ctrl+1~3(모드), Ctrl+A. 키보드 포커스를 잡지 않기 위해 타이머에서 키 상태를 직접 읽는다.
    void pollShortcutKeys();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    enum class Zone { None, Ruler, Gutter, Plot };
    enum class EdgeSide { None, Front, Back };
    enum class DragKind { None, Pan, Marquee, BlobPitch, BlobGain, BlobMove, BlobFront, BlobBack, BlobVibrato, BlobFlatten, BlobDrift, BlobPitchOrTime, CurveHandle, Transition };

    struct BlockPick
    {
        size_t index = 0;
        EdgeSide edge = EdgeSide::None;
    };

    struct CurvePick
    {
        size_t block = 0;
        size_t frame = 0;
    };

    struct Drag
    {
        DragKind kind = DragKind::None;
        Zone zone = Zone::None;
        size_t block = 0;                                           // 끌고 있는 블록 (여러 개면 기준 블록)
        std::vector<size_t> group;                                  // 함께 편집되는 블록들 (오름차순)
        std::shared_ptr<const std::vector<VocalBlock>> snapshot;    // 시작 시점의 블록들
        size_t handle = 0;
        juce::Point<float> start;                                   // 플롯 좌표 (범위 선택)
        std::vector<size_t> baseSelection;                          // Ctrl+범위 선택: 원래 선택
        bool additive = false;
        bool begun = false;
        juce::Point<float> last;                                    // 직전 마우스 위치
        double effectiveX = 0.0, effectiveY = 0.0;                  // 미세 조정을 반영한 누적 이동량 (픽셀)
        bool timingAllowed = false;                                 // 이동 도구: 이미 선택된 블롭을 잡았음 (타이밍 편집 가능)
    };

    static constexpr int gutterWidth = 40;
    static constexpr int rulerHeight = 24;
    static constexpr int toolbarHeight = 30;
    static constexpr double jointTolerancePixels = 6.0;
    static constexpr float edgeHandlePixels = 7.0f;
    static constexpr float curveHandleRadius = 5.5f;
    static constexpr double transitionPixelsForFull = 100.0;
    static constexpr double vibratoPixelsPerSemitone = 80.0;    // 세로 드래그: 깊이
    static constexpr double vibratoPixelsPerHz = 40.0;          // 가로 드래그: 속도
    static constexpr double vibratoDefaultDepth = 0.5;
    static constexpr double flattenPixelsForFull = 120.0;       // 세로 드래그: 펴는 정도
    static constexpr double flattenDefault = 0.1;
    static constexpr double driftPixelsForFull = 120.0;         // 세로 드래그: 음정 흐름 보정 정도
    static constexpr double fineDragFactor = 0.1;               // 끌기 중 Ctrl 미세 조정

    // --- 모드/도구/선택 (PianoRollView.cpp)
    void setMode(Mode);
    void setBlobTool(BlobTool);
    void setCurveTool(CurveTool);
    void selectToolByNumber(size_t index);
    bool toolPicksConsonants() const noexcept { return ! needsPitchEditing(blobTool); }
    static bool isShapeTool(BlobTool t) noexcept { return t == BlobTool::Vibrato || t == BlobTool::Flatten || t == BlobTool::Drift; }
    void onToolButton(size_t index);
    void showShapeMenu();
    void showModeMenu();
    void showBlockMenu(const juce::MouseEvent&);
    void startPreview(size_t index);
    void requestUndo(bool redo);
    bool historyAllowed() const noexcept { return selection.size() <= 1; }   // 다중 선택 중에는 되돌리기/다시 불가
    void toggleDrift(const BlockPick&);
    void startTimingDrag(const BlockPick&);
    void toggleVibrato(const BlockPick&);
    void toggleFlatten(const BlockPick&);
    static bool needsPitchEditing(BlobTool t) noexcept { return t == BlobTool::Move || isShapeTool(t); }
    void syncToolbar();
    void toggleBalancePanel();
    void applyBalance(const PitchBalance&);
    std::vector<size_t> balanceTargets() const;
    void updateCursor(juce::Point<float> position);
    Zone zoneAt(juce::Point<float>) const;
    void fitToContent();
    bool isSelected(size_t index) const;
    void selectOnly(size_t index);
    void toggleSelection(size_t index);
    void setSelection(std::vector<size_t>);
    void selectAll();
    std::optional<size_t> singleSelection() const { return selection.size() == 1 ? std::optional<size_t>(selection.front()) : std::nullopt; }
    juce::Rectangle<int> plotBounds() const
    {
        return getLocalBounds().withTrimmedLeft(gutterWidth).withTrimmedTop(rulerHeight + toolbarHeight);
    }
    juce::Point<float> toPlot(juce::Point<float> p) const { return p - plotBounds().getPosition().toFloat(); }

    // --- 편집 (PianoRollEdit.cpp)
    std::optional<BlockPick> pickBlock(juce::Point<float> plotPos, bool includeConsonants) const;
    std::optional<size_t> pickCurveHandle(juce::Point<float> plotPos) const;
    std::optional<CurvePick> pickCurvePoint(juce::Point<float> plotPos) const;
    std::optional<size_t> pickTransition(juce::Point<float> plotPos) const;     // 왼쪽 블록 인덱스
    const void* pickOtherLayer(juce::Point<float> plotPos);                     // 다른 편집 가능 트랙의 블롭
    const std::vector<size_t>& curveHandlesForSelection() const;
    juce::Point<float> handlePoint(const VocalBlock&, size_t frame) const;
    std::vector<juce::Point<float>> transitionLine(const PitchJoint&) const;
    juce::Rectangle<float> blockBounds(const VocalBlock&) const;
    double blockCentreMidi(const VocalBlock&) const;
    double blockAmplitude(const VocalBlock&, double frame) const;
    double meanAmplitude(const VocalBlock&) const;
    void startPlotDrag(const juce::MouseEvent&);
    void startBlobDrag(const BlockPick&);
    void continueDrag(const juce::MouseEvent&);
    void updateMarquee(const juce::MouseEvent&);
    void emit(std::vector<BlockUpdate>);
    void alignSelectionToNoteCenter(const BlockPick&);
    void toggleCurveHandleAt(const CurvePick&);
    std::optional<double> jointDisplaySecNear(double displaySourceSec) const;
    double songToSource(double songSec) const { return songSec - content.offsetSec; }
    double sourceToSong(double sourceSec) const { return sourceSec + content.offsetSec; }
    float timeX(double sourceDisplaySec) const { return static_cast<float>(viewport.timeToX(sourceToSong(sourceDisplaySec))); }
    float midiY(double midi) const { return static_cast<float>(viewport.midiToY(midi)); }

    // --- 그리기 (PianoRollDraw.cpp)
    void drawPitchGrid(juce::Graphics&) const;
    void drawTimeGrid(juce::Graphics&) const;
    void drawRuler(juce::Graphics&) const;
    void drawToolbarHint(juce::Graphics&) const;
    void drawBackdropLayers(juce::Graphics&) const;
    void drawBlocks(juce::Graphics&) const;
    juce::Rectangle<int> regionClipRect() const;
    bool inRegion(const VocalBlock&) const;
    void drawBlock(juce::Graphics&, const std::vector<VocalBlock>&, size_t index) const;
    void drawTransitions(juce::Graphics&) const;
    void drawBlockCurves(juce::Graphics&) const;
    void drawEditHandles(juce::Graphics&) const;
    void drawMarquee(juce::Graphics&) const;
    void drawPlayhead(juce::Graphics&) const;
    void drawCutGuide(juce::Graphics&) const;
    void drawKeyLabels(juce::Graphics&) const;
    double timeGridStep() const;

    // 다른 트랙을 그리거나 고를 때 content를 잠깐 바꿔 끼워 같은 그리기/선택 코드를 재사용한다 (메시지 스레드 전용)
    struct ScopedContent
    {
        ScopedContent(const PianoRollView& view, const Content& temporary) : owner(const_cast<PianoRollView&>(view)), saved(owner.content)
        {
            owner.content = temporary;
        }
        ~ScopedContent() { owner.content = std::move(saved); }
        PianoRollView& owner;
        Content saved;
    };

    juce::Colour tinted(juce::Colour colour) const;
    float blockOpacity() const noexcept;
    float curveOpacity() const noexcept;

    Content content;
    std::vector<Layer> layers;
    mutable bool paintingBackdrop = false;
    mutable int backdropColourIndex = 0;
    mutable float backdropSaturation = 1.0f;
    RollViewport viewport;
    RollViewport dragOrigin;
    Drag drag;

    juce::TextButton modeButton;                     // 활성 모드 이름만 표시, 누르면 모드 메뉴
    std::array<IconTextButton, 4> toolButtons;       // 블롭: 이동 / 자르기 / 볼륨 / 곡선 도구(▾)
    IconTextButton undoButton { juce::String::fromUTF8("되돌리기") };
    IconTextButton redoButton { juce::String::fromUTF8("다시") };
    IconTextButton balanceButton { juce::String::fromUTF8("음정 균형") };
    PitchBalancePanel balancePanel;
    PitchScale lastScale;
    bool canUndo = false, canRedo = false;
    bool previewing = false;                   // 패널을 다시 열 때 마지막 조를 유지

    Mode mode = Mode::Blob;
    BlobTool blobTool = BlobTool::Move;
    BlobTool shapeTool = BlobTool::Drift;   // 네 번째 버튼이 고르는 도구
    CurveTool curveTool = CurveTool::Vertical;
    std::vector<size_t> selection;          // 오름차순
    std::optional<size_t> primaryBlock;     // 마지막으로 선택한 블록 (라벨 표시 등)
    std::optional<size_t> hoverBlock;
    std::optional<size_t> hoverJoint;       // 이음선의 왼쪽 블록
    std::optional<juce::Point<float>> hover;
    std::optional<juce::Rectangle<float>> marquee;   // 플롯 좌표
    double rhythmRowMidi = 60.0;            // 박자 모드에서 모든 블롭이 놓이는 음 (노래 평균)

    mutable std::vector<size_t> curveHandles;
    mutable std::optional<size_t> handlesBlock;
    mutable std::vector<size_t> handlesUserKey;
    mutable size_t handlesBeginFrame = 0;

    std::array<bool, 8> keyWasDown {};      // 1~6, A, Z
    double playheadSec = 0.0;
    bool playing = false;
};
