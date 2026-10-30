#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include "AnalysisTask.h"
#include "BlockEditing.h"
#include "BlockHistory.h"
#include "BlockTransforms.h"

struct AnalysisSummary
{
    int analysed = 0;
    int pending = 0;
    size_t totalFrames = 0;
    size_t voicedFrames = 0;
    size_t numBlocks = 0;
    size_t numConsonantBlocks = 0;
    double lastElapsedSeconds = 0.0;   // 가장 최근에 끝난 분석: 걸린 시간 / 오디오 길이
    double lastAudioSeconds = 0.0;
    int numWorkers = 1;
};

struct UndoState
{
    bool canUndo = false;
    bool canRedo = false;
};

// 오디오 소스별 F0/RMS 분석 관리. 리스너 콜백과 공개 함수는 메시지 스레드 전용.
class AnalysisManager final : private juce::ARAAudioSourceListener
{
public:
    AnalysisManager();
    ~AnalysisManager() override;

    void track(juce::ARAAudioSource&);
    std::shared_ptr<const VocalFeatures> getFeatures(juce::ARAAudioSource*) const;
    std::shared_ptr<const std::vector<VocalBlock>> getBlocks(juce::ARAAudioSource*) const;
    AnalysisSummary getSummary() const;

    // --- 레퍼런스 정렬용 보조 분석 (기본 꺼짐). 켜면 모든 소스에서 스펙트럼 분석을 시작하고, 끄면 결과를 버린다.
    void setAlignmentActive(bool);
    bool isAlignmentActive() const noexcept { return alignmentActive.load(); }
    std::shared_ptr<const AlignmentData> getAlignment(juce::ARAAudioSource*) const;
    // 정렬 분석 진행: done / total (분석이 끝난 소스 기준)
    std::pair<int, int> getAlignmentProgress() const;

    // 낮은 우선순위 작업 스레드로 병렬 실행 (렌더도 같이 써서 DAW 오디오 스레드를 밀어내지 않게)
    ParallelFor makeParallelFor();
    // 메시지 스레드(타이머)에서 주기적으로: 새로 분석이 끝난 소스의 정렬 분석을 시작
    void pollAlignment();

    // --- 블록 편집. 블록 목록은 불변 스냅샷이고 편집마다 새 스냅샷으로 교체된다.
    // 화면 위치(소스 시간 + 시간 편집 반영)를 자르거나, 경계 근처(toleranceSec)면 붙인다. 성공하면 되돌리기 지점도 만든다.
    BlockEdit editBlocksAt(juce::ARAAudioSource*, double displaySourceSec, double toleranceSec);
    // 드래그 같은 연속 편집의 시작에서 한 번 호출 (되돌리기 지점), 이후 replaceBlock을 여러 번 호출
    void beginEditGesture(juce::ARAAudioSource*);
    bool replaceBlocks(juce::ARAAudioSource*, const std::vector<BlockUpdate>&);
    bool undo(juce::ARAAudioSource*);
    bool redo(juce::ARAAudioSource*);
    UndoState getUndoState(juce::ARAAudioSource*) const;
    // 블록별 되돌리기용: 첫 분석 결과와 되돌리기 기록 (오래된 것부터)
    BlockSnapshot getOriginalBlocks(juce::ARAAudioSource*) const;
    std::vector<BlockSnapshot> getUndoSnapshots(juce::ARAAudioSource*) const;

private:
    struct Entry
    {
        std::shared_ptr<AnalysisTask> task;
        std::shared_ptr<AnalysisTask> alignmentTask;
        std::shared_ptr<const AlignmentData> alignment;
        bool alignmentFailed = false;
        std::shared_ptr<const VocalFeatures> features;
        std::shared_ptr<const std::vector<VocalBlock>> blocks;
        std::shared_ptr<const std::vector<VocalBlock>> original;   // 첫 분석 결과 (블록별 '처음으로 되돌리기')
        double elapsedSeconds = 0.0;
        double audioSeconds = 0.0;
        std::vector<std::shared_ptr<const std::vector<VocalBlock>>> undoStack, redoStack;
    };

    static void pushUndo(Entry&);

    void didEnableAudioSourceSamplesAccess(juce::ARAAudioSource*, bool enable) override;
    void willEnableAudioSourceSamplesAccess(juce::ARAAudioSource*, bool enable) override;
    void willDestroyAudioSource(juce::ARAAudioSource*) override;

    void startAnalysis(juce::ARAAudioSource&);
    void startAlignmentAnalysis(juce::ARAAudioSource&);   // 메시지 스레드, entriesMutex를 잡지 않은 상태에서 호출
    void storeAlignment(juce::ARAAudioSource*, const std::shared_ptr<AnalysisTask>&, std::optional<AlignmentData>&&);
    void cancelAnalysis(juce::ARAAudioSource*);
    void storeResult(juce::ARAAudioSource*, const std::shared_ptr<AnalysisTask>&, AnalysisResult&&);

    std::atomic<bool> alignmentActive { false };
    mutable std::mutex entriesMutex;
    std::map<juce::ARAAudioSource*, Entry> entries;

    double lastElapsedSeconds = 0.0;
    double lastAudioSeconds = 0.0;
    const int numWorkers;

    // 아래 두 풀은 마지막에 선언: 파괴 순서(pool -> workerPool)가 곧 의존 순서이고,
    // 둘 다 실행 중인 작업이 끝난 뒤 entries가 파괴됨
    juce::ThreadPool workerPool;   // Harvest 구간 처리 (낮은 우선순위)
    juce::ThreadPool pool;         // 소스별 분석 코디네이터
};
