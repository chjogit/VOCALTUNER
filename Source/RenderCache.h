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

private:
    struct Entry
    {
        std::shared_ptr<const std::vector<VocalBlock>> seenBlocks, renderedFor;
        std::shared_ptr<const VocalFeatures> seenFeatures, audioFeatures;
        juce::uint32 seenAtMs = 0, retryAtMs = 0;
        std::shared_ptr<const AudioPlanes> sourceAudio, rendered;
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

    AnalysisManager& analysis;
    mutable std::mutex mutex;           // entries 보호. 오디오 스레드는 try_lock만 사용
    std::mutex workMutex;               // 렌더 중인 소스가 사라지기 전에 기다림
    std::map<juce::ARAAudioSource*, Entry> entries;
    std::vector<std::shared_ptr<const AudioPlanes>> retired;     // 교체된 결과 (mutex 보호)
    std::atomic<juce::ARAAudioSource*> removing { nullptr };
    std::atomic<bool> rendering { false };
};
