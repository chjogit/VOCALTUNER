#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Analysis/AnalysisManager.h"
#include "EditRenderer.h"

// 편집이 반영된 오디오를 백그라운드에서 만들어 두고, 재생/바운스 때 PlaybackRenderer가 가져다 쓴다.
// 편집이 소리를 바꾸지 않으면 원본 오디오(읽어 둔 경우) 또는 nullptr(원본 재생). 렌더 중에는 직전 결과를 계속 재생한다.
// 교체된 결과는 바로 지우지 않고 보관했다가, 오디오 스레드가 더 이상 쥐고 있지 않을 때 렌더 스레드에서 해제한다.
class RenderCache final : private juce::Thread, private juce::ARAAudioSourceListener
{
public:
    explicit RenderCache(AnalysisManager&);
    ~RenderCache() override;

    void track(juce::ARAAudioSource&);                                                  // 메시지 스레드

    // 오디오 스레드용. 잠금을 못 잡으면 false (out은 그대로 → 호출자는 직전 결과를 계속 쓴다)
    bool tryGetRendered(juce::ARAAudioSource*, std::shared_ptr<const AudioPlanes>& out) const noexcept;
    std::shared_ptr<const AudioPlanes> getRendered(juce::ARAAudioSource*) const;     // 오프라인 바운스용 (기다림)
    bool isUpToDate(juce::ARAAudioSource*) const;
    // 오프라인 바운스: 최신 렌더가 끝날 때까지 기다림 (제한 시간 초과 시 false)
    bool waitUntilUpToDate(juce::ARAAudioSource*, int timeoutMs) const;
    bool isRendering() const noexcept { return rendering.load(); }

    // 블록 미리 듣기: 출력 구간 [begin, end)만 기다림 없이 바로 렌더한다 (메시지 스레드).
    // 요청이 걸려 있는 동안 전체 렌더는 미룬다. 블록이 바뀔 때마다 다시 부르면 그 블록 목록으로 다시 렌더한다.
    void requestPreview(juce::ARAAudioSource*, std::shared_ptr<const std::vector<VocalBlock>>, juce::int64 begin, juce::int64 end);
    void clearPreview();
    // 이 블록 목록으로 만든 구간 결과가 준비됐는지 (메시지 스레드)
    bool isPreviewReady(juce::ARAAudioSource*, const std::shared_ptr<const std::vector<VocalBlock>>&) const;
    // 오디오 스레드용. 전체 렌더보다 새로운 구간 결과 (없으면 nullptr). 잠금을 못 잡으면 false.
    bool tryGetSegment(juce::ARAAudioSource*, std::shared_ptr<const RenderedSegment>& out) const noexcept;

private:
    struct Entry
    {
        std::shared_ptr<const std::vector<VocalBlock>> seenBlocks, renderedFor;
        std::shared_ptr<const VocalFeatures> seenFeatures, audioFeatures;
        juce::uint32 seenAtMs = 0, retryAtMs = 0;
        std::shared_ptr<const AudioPlanes> sourceAudio, rendered;
        std::shared_ptr<const RenderedSegment> segment;         // 전체 렌더가 끝나면 비운다
        std::shared_ptr<const std::vector<VocalBlock>> segmentFor;
    };

    struct PreviewJob
    {
        juce::ARAAudioSource* source = nullptr;
        juce::int64 begin = 0, end = 0;
        std::shared_ptr<const std::vector<VocalBlock>> blocks, renderedFor;
        juce::int64 renderedBegin = -1, renderedEnd = -1;
    };

    void run() override;
    void willDestroyAudioSource(juce::ARAAudioSource*) override;
    void service(juce::ARAAudioSource*);
    bool needsRender(Entry&, const std::shared_ptr<const std::vector<VocalBlock>>&,
                     const std::shared_ptr<const VocalFeatures>&) const;
    std::shared_ptr<const AudioPlanes> loadSourceAudio(juce::ARAAudioSource*) const;
    void publish(juce::ARAAudioSource*, std::shared_ptr<const AudioPlanes>,
                 std::shared_ptr<const std::vector<VocalBlock>>);
    void releaseRetired();
    bool servicePreview();
    std::shared_ptr<const AudioPlanes> sourceAudioFor(juce::ARAAudioSource*);
    void publishSegment(const PreviewJob&, std::shared_ptr<const std::vector<VocalBlock>>, RenderedSegment);

    AnalysisManager& analysis;
    mutable std::mutex mutex;           // entries 보호. 오디오 스레드는 try_lock만 사용
    std::mutex workMutex;               // 렌더 중인 소스가 사라지기 전에 기다림
    std::map<juce::ARAAudioSource*, Entry> entries;
    std::vector<std::shared_ptr<const void>> retired;            // 교체된 결과 (mutex 보호)
    PreviewJob previewJob;                                       // mutex 보호
    std::atomic<bool> previewActive { false };
    std::atomic<juce::ARAAudioSource*> removing { nullptr };
    std::atomic<bool> rendering { false };
};
