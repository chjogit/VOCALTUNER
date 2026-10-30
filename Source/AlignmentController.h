#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <vector>
#include "Alignment/ReferenceFit.h"
#include "Analysis/AnalysisManager.h"
#include "UI/TrackTypes.h"

// 트랙별 표시 상태 / 역할과, 레퍼런스에 맞추는 동작. 메시지 스레드 전용.
// 트랙 = 스튜디오 원 트랙(ARA Region Sequence). 정보가 없으면 클립(재생 영역) 하나가 한 트랙.
// 무거운 분석(AnalysisManager의 정렬 분석)은 사용자가 정렬 기능을 켜기 전에는 돌지 않는다.
struct TrackSettings
{
    TrackView view = TrackView::Editable;
    TrackRoles roles;
    bool baseCorrection = true;     // 기초 자동 보정 (기준 트랙이 되면 자동으로 끈다)
};

class AlignmentController
{
public:
    explicit AlignmentController(AnalysisManager& analysisIn) : analysis(analysisIn) {}
    ~AlignmentController();

    struct Outcome
    {
        bool ok = false;
        juce::String message;
    };

    using TrackKey = const void*;
    static TrackKey trackOf(const juce::ARAPlaybackRegion*) noexcept;

    TrackSettings& settings(const juce::ARAPlaybackRegion*);       // 그 클립이 속한 트랙의 설정
    // 기준은 둘까지: 1 = 네이비, 2 = 베이지. 따라가는 클립은 같은 시간에 가장 많이 겹치는 기준 클립에 맞춘다.
    int referenceSlot(const juce::ARAPlaybackRegion*) const;        // 0 = 기준 아님
    bool isReference(const juce::ARAPlaybackRegion* r) const { return referenceSlot(r) > 0; }
    bool hasReference() const noexcept { return ! referenceSlots.empty(); }
    void setReferenceSlot(const juce::ARAPlaybackRegion*, int slot); // 그 클립의 트랙 전체. 0 = 해제, 같은 색을 쓰던 트랙은 해제된다
    bool hasBaseline(const juce::ARAPlaybackRegion* r) const { return baselines.count(r) > 0; }
    void prune(const std::vector<const juce::ARAPlaybackRegion*>& live);

    // target 클립을, 같은 시간에 가장 많이 겹치는 기준 트랙의 클립에 맞춘다. newBaseline: 지금 블록을 기준으로 삼음(맞추기 버튼), false면 이전 기준에서 정도만 다시 적용(슬라이더).
    // beginGesture: 되돌리기 지점을 만든다.
    Outcome fit(const juce::ARAPlaybackRegion* target, bool newBaseline, bool beginGesture);

    // --- 백그라운드 맞추기 ('이 트랙 맞추기' / '모두 맞추기'). 클립마다 차례로: 분석 대기 -> 타이밍 정렬 -> 적용
    struct FitProgress
    {
        bool running = false, finished = false;
        double progress = 0.0;          // 0..1 (전체 클립 기준)
        juce::String stage, message;    // message: 끝났을 때 결과
    };
    void startFit(std::vector<const juce::ARAPlaybackRegion*> targets);
    static juce::String changeSummary(const std::vector<VocalBlock>& before, const std::vector<BlockUpdate>& after);
    FitProgress pollFit();              // 메시지 스레드 타이머에서 호출. 끝난 정렬을 적용한다
    void cancelFit();
    bool isFitting() const noexcept { return job != nullptr; }
    std::function<void(juce::ARAAudioSource*)> onBeforeApply;     // 적용 직전 (편집기가 다른 트랙 되돌리기 단계를 맞춤)

    // target이 맞출 기준 클립 (같은 시간에 가장 많이 겹치는 기준 트랙의 클립). 없으면 nullptr
    const juce::ARAPlaybackRegion* referenceFor(const juce::ARAPlaybackRegion* target) const;
    static double offsetOf(const juce::ARAPlaybackRegion*) noexcept;
    static juce::ARAAudioSource* sourceOf(const juce::ARAPlaybackRegion*) noexcept;

private:
    struct CachedMap
    {
        const juce::ARAPlaybackRegion* reference = nullptr;
        std::shared_ptr<const AlignmentData> referenceData, targetData;
        double referenceOffset = 0.0, targetOffset = 0.0;
        AlignResult result;
        size_t anchorCount = 0;
    };

    struct Prepared
    {
        bool ok = false, ready = false;     // ok: 맞출 수 있음, ready: 분석 데이터가 다 있음
        juce::String message;
        const juce::ARAPlaybackRegion* reference = nullptr;
        std::shared_ptr<const AlignmentData> referenceData, targetData;
        Prepared fail(const juce::String& m) const { auto copy = *this; copy.message = m; return copy; }
    };
    struct AlignPlan
    {
        std::shared_ptr<const AlignmentData> referenceData, targetData;    // TrackFrames가 가리키는 데이터를 살려 둔다
        TrackFrames reference, target;
        TimeMap guide;
        AlignOptions options;
        size_t anchorCount = 0;
    };
    struct FitJob
    {
        std::vector<const juce::ARAPlaybackRegion*> targets;
        size_t index = 0;
        int applied = 0, skipped = 0;
        juce::String lastMessage;
        std::shared_ptr<std::atomic<double>> dtwProgress = std::make_shared<std::atomic<double>>(0.0);
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
        std::future<AlignResult> pending;
        Prepared pendingFor;
        size_t pendingAnchors = 0;
    };

    static std::vector<WordAnchor> anchorsFor(const AlignmentData&, double referenceOffset, const AlignmentData&, double targetOffset);
    Prepared prepare(const juce::ARAPlaybackRegion* target) const;
    const AlignResult* cachedMap(const juce::ARAPlaybackRegion* target, const Prepared&) const;
    static AlignPlan planFor(const juce::ARAPlaybackRegion* target, const Prepared&);
    static AlignResult runPlan(AlignPlan&);
    void storeMap(const juce::ARAPlaybackRegion* target, const Prepared&, size_t anchorCount, AlignResult);
    Outcome apply(const juce::ARAPlaybackRegion* target, const Prepared&, bool newBaseline, bool beginGesture);
    bool isLive(const juce::ARAPlaybackRegion*) const;
    bool advanceJob(FitProgress&);
    FitProgress finishJob(const juce::String& message);

    AnalysisManager& analysis;
    std::map<TrackKey, int> referenceSlots;
    std::vector<const juce::ARAPlaybackRegion*> live;
    std::map<TrackKey, TrackSettings> tracks;
    std::map<const juce::ARAPlaybackRegion*, std::shared_ptr<const std::vector<VocalBlock>>> baselines;  // 마지막 맞추기 직전 블록 (정도 슬라이더의 기준)
    std::map<const juce::ARAPlaybackRegion*, CachedMap> cache;
    std::unique_ptr<FitJob> job;
};
