#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <map>
#include <memory>
#include <optional>
#include "UI/PianoRollView.h"
#include "UI/TrackPanel.h"

class VocalTunerDocumentController;

constexpr int statusHeight = 26;

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
    juce::String referenceLabel() const;
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
    juce::String formatAnalysisStatus() const;
    static juce::String formatPlayHead(const PlayHeadSnapshot&);

    VocalTunerAudioProcessor& processor;
    TrackPanel trackPanel;
    PianoRollView pianoRoll;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalTunerAudioProcessorEditor)
};
