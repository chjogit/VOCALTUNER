#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <map>
#include <memory>
#include <optional>
#include "UI/PianoRollView.h"
#include "UI/SettingsPanel.h"
#include "UI/SharpLookAndFeel.h"
#include "UI/TrackPanel.h"

class VocalTunerDocumentController;

constexpr int statusHeight = 24;

class VocalTunerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       public juce::AudioProcessorEditorARAExtension,
                                       private juce::Timer
{
public:
    explicit VocalTunerAudioProcessorEditor(VocalTunerAudioProcessor&);
    ~VocalTunerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updatePianoRoll();
    const juce::ARAPlaybackRegion* findRegion() const;
    const juce::ARAPlaybackRegion* activeRegion() const;
    std::vector<const juce::ARAPlaybackRegion*> collectRegions();
    void refreshTracks();
    void pushTrackPanel(const std::vector<const juce::ARAPlaybackRegion*>&);
    void pushLayers(const std::vector<const juce::ARAPlaybackRegion*>&);
    void wireTrackPanel();
    void fitTracks(bool all);
    void pollFit();
    void setBaseCorrection(const juce::ARAPlaybackRegion*, bool on);
    void applyBaseCorrection(juce::ARAAudioSource*, bool on, bool undoable);
    static juce::String regionLabel(const juce::ARAPlaybackRegion*);
    static juce::String trackLabel(const juce::ARAPlaybackRegion*);
    juce::String referenceLabel(const juce::ARAPlaybackRegion* target) const;
    juce::String rowDetail(const juce::ARAPlaybackRegion*, int clipCount) const;
    void toggleSettings();
    void applyTheme(const ThemeChoice&);
    PianoRollView::Content makeContent(const juce::ARAPlaybackRegion*) const;
    const VocalTunerDocumentController* getDocumentController() const;
    VocalTunerDocumentController* getDocumentController();
    void cutBlockAt(double songSec, double toleranceSec);
    void seekHostTo(double songSec);
    juce::ARAAudioSource* currentSource() const;
    void beginEditGesture();
    std::vector<juce::ARAAudioSource*> editableSources() const;
    void pushUndoForOthers(juce::ARAAudioSource* except);
    void commitBlocks(juce::ARAAudioSource*, const std::vector<VocalBlock>&);
    void applyBlockEdits(const std::vector<BlockUpdate>&);
    void bakeShapes();
    void undoEdit();
    void redoEdit();
    void revertBlocks(const std::vector<size_t>& indices, bool toFirst);
    void startBlockPreview(size_t index);
    void stopBlockPreview();
    void pollBlockPreview();
    void requestPreviewRender();
    void playBlockPreview();
    void trackProcessing();
    bool hostProcessesWhileStopped() const;
    juce::String formatAnalysisStatus() const;
    static juce::String formatPlayHead(const PlayHeadSnapshot&);

    VocalTunerAudioProcessor& processor;
    SharpLookAndFeel lookAndFeel;       // 화면 요소보다 먼저 만들고 나중에 없앤다
    TrackPanel trackPanel;
    PianoRollView pianoRoll;
    SettingsPanel settingsPanel;
    juce::TooltipWindow tooltips { this, 450 };

    // 여러 트랙: 편집 중인 트랙은 기본으로 이 인스턴스의 클립이고, 다른 편집 가능 트랙의 블롭을 누르면 바뀐다
    const juce::ARAPlaybackRegion* activeOverride = nullptr;
    std::vector<const juce::ARAPlaybackRegion*> liveRegions;
    juce::String alignmentMessage;
    std::map<juce::ARAAudioSource*, std::shared_ptr<const std::vector<VocalBlock>>> gestureBase;   // 드래그 시작 시점의 블록 (트랙별)
    std::vector<const void*> shownLayerKey;

    // 변경 감지용 (바뀔 때만 뷰를 갱신)
    const void* shownRegion = nullptr;
    const void* shownFeatures = nullptr;
    const void* shownBlocks = nullptr;

    // 호스트가 정지 중이면 processBlock이 안 돌아 재생 위치가 갱신되지 않으므로, 요청한 위치를 잠시 표시
    std::optional<double> pendingSeekSec;
    double snapshotTimeAtSeek = 0.0;
    double shownStart = 0.0, shownDuration = 0.0;

    // 미리 듣기
    std::uint32_t lastProcessCount = 0;
    juce::uint32 lastProcessChangeMs = 0;
    bool previewByHost = false;          // 호스트 재생을 켜 둔 상태
    bool previewUsesHost = false;        // 누를 때 정한 방식 (호스트 재생 / 플러그인 직접 재생)
    bool previewWaiting = false;         // 처음 누를 때: 편집 반영 구간 렌더를 기다리는 중
    juce::uint32 previewStartedMs = 0, previewLengthMs = 0;   // 블록 끝에 닿으면 처음부터 다시
    double previewSongStart = 0.0;
    juce::ARAAudioSource* previewSource = nullptr;
    size_t previewIndex = 0;
    std::shared_ptr<const std::vector<VocalBlock>> previewBlocks;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalTunerAudioProcessorEditor)
};
