#include "RenderCache.h"
#include <algorithm>
#include <iterator>

namespace
{
constexpr juce::uint32 settleMs = 250;      // 편집이 이 시간 동안 멈추면 렌더 시작
constexpr juce::uint32 retryMs = 2000;
} // namespace

RenderCache::RenderCache(AnalysisManager& manager) : juce::Thread("VocalTuner render"), analysis(manager)
{
    startThread(juce::Thread::Priority::low);
}

RenderCache::~RenderCache()
{
    signalThreadShouldExit();
    notify();
    stopThread(30000);
}

void RenderCache::track(juce::ARAAudioSource& source)
{
    source.addListener(this);
    const std::lock_guard<std::mutex> guard(mutex);
    entries.emplace(&source, Entry{});
}

void RenderCache::willDestroyAudioSource(juce::ARAAudioSource* source)
{
    source->removeListener(this);
    removing = source;                       // 진행 중인 렌더가 중단되도록
    {
        const std::lock_guard<std::mutex> work(workMutex);
        const std::lock_guard<std::mutex> guard(mutex);
        if (const auto it = entries.find(source); it != entries.end())
        {
            retired.push_back(std::move(it->second.rendered));     // 재생 중인 쪽이 쥐고 있을 수 있음
            retired.push_back(std::move(it->second.sourceAudio));
            entries.erase(it);
        }
    }
    removing = nullptr;
}

bool RenderCache::tryGetRendered(juce::ARAAudioSource* source, std::shared_ptr<const AudioPlanes>& out) const noexcept
{
    std::unique_lock<std::mutex> guard(mutex, std::try_to_lock);
    if (! guard.owns_lock())
        return false;
    const auto it = entries.find(source);
    out = it == entries.end() ? nullptr : it->second.rendered;
    return true;
}

std::shared_ptr<const AudioPlanes> RenderCache::getRendered(juce::ARAAudioSource* source) const
{
    const std::lock_guard<std::mutex> guard(mutex);
    const auto it = entries.find(source);
    return it == entries.end() ? nullptr : it->second.rendered;
}

bool RenderCache::isUpToDate(juce::ARAAudioSource* source) const
{
    const auto blocks = analysis.getBlocks(source);
    const std::lock_guard<std::mutex> guard(mutex);
    const auto it = entries.find(source);
    return blocks == nullptr || it == entries.end() || it->second.renderedFor == blocks;
}

bool RenderCache::waitUntilUpToDate(juce::ARAAudioSource* source, int timeoutMs) const
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (! isUpToDate(source))
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        juce::Thread::sleep(20);
    }
    return true;
}

bool RenderCache::needsRender(Entry& e, const std::shared_ptr<const std::vector<VocalBlock>>& blocks,
                              const std::shared_ptr<const VocalFeatures>& features) const
{
    const auto now = juce::Time::getMillisecondCounter();
    if (e.seenBlocks != blocks || e.seenFeatures != features)
    {
        e.seenBlocks = blocks;
        e.seenFeatures = features;
        e.seenAtMs = now;
    }
    if (e.renderedFor == blocks && e.audioFeatures == features)
        return false;
    return now - e.seenAtMs >= settleMs && now >= e.retryAtMs;
}

std::shared_ptr<const AudioPlanes> RenderCache::loadSourceAudio(juce::ARAAudioSource* source) const
{
    juce::ARAAudioSourceReader reader(source);
    const auto length = static_cast<int>(source->getSampleCount());
    const auto channels = source->getChannelCount();
    if (length <= 0 || channels <= 0)
        return nullptr;
    juce::AudioBuffer<float> buffer(channels, length);
    if (! reader.read(&buffer, 0, length, 0, true, true))
        return nullptr;
    auto planes = std::make_shared<AudioPlanes>();
    planes->sampleRate = juce::roundToInt(source->getSampleRate());
    for (int ch = 0; ch < channels; ++ch)
        planes->channels.emplace_back(buffer.getReadPointer(ch), buffer.getReadPointer(ch) + length);
    return planes;
}

void RenderCache::publish(juce::ARAAudioSource* source, std::shared_ptr<const AudioPlanes> audio,
                          std::shared_ptr<const std::vector<VocalBlock>> blocks)
{
    const std::lock_guard<std::mutex> guard(mutex);
    const auto it = entries.find(source);
    if (it == entries.end())
        return;
    if (it->second.rendered != nullptr)
        retired.push_back(std::move(it->second.rendered));    // 오디오 스레드가 아직 쥐고 있을 수 있음
    it->second.rendered = std::move(audio);
    it->second.renderedFor = std::move(blocks);
    it->second.audioFeatures = it->second.seenFeatures;
}

// 오디오 스레드에서 큰 버퍼가 해제되면 재생이 끊긴다. 아무도 쓰지 않는 것만 여기(렌더 스레드)서 지운다.
void RenderCache::releaseRetired()
{
    std::vector<std::shared_ptr<const AudioPlanes>> unused;
    {
        const std::lock_guard<std::mutex> guard(mutex);
        const auto free = std::stable_partition(retired.begin(), retired.end(), [](const auto& p) { return p.use_count() > 1; });
        std::move(free, retired.end(), std::back_inserter(unused));
        retired.erase(free, retired.end());
    }
}

void RenderCache::service(juce::ARAAudioSource* source)
{
    const auto blocks = analysis.getBlocks(source);
    const auto features = analysis.getFeatures(source);
    if (blocks == nullptr || features == nullptr)
        return;

    std::shared_ptr<const AudioPlanes> sourceAudio;
    {
        const std::lock_guard<std::mutex> guard(mutex);
        const auto it = entries.find(source);
        if (it == entries.end() || ! needsRender(it->second, blocks, features))
            return;
        if (it->second.audioFeatures != features)
            it->second.sourceAudio = nullptr;       // 재분석됨: 원본 오디오도 다시 읽기
        sourceAudio = it->second.sourceAudio;
    }

    // 소리 변화가 없으면 읽어 둔 원본을 그대로 내놓는다 (재생이 렌더 결과와 원본 리더 사이를 오가지 않게)
    if (! changesAudio(*blocks, *features))
        return publish(source, sourceAudio, blocks);

    if (sourceAudio == nullptr)
    {
        sourceAudio = loadSourceAudio(source);
        const std::lock_guard<std::mutex> guard(mutex);
        const auto it = entries.find(source);
        if (it == entries.end())
            return;
        if (sourceAudio == nullptr)
        {
            it->second.retryAtMs = juce::Time::getMillisecondCounter() + retryMs;
            return;
        }
        it->second.sourceAudio = sourceAudio;
    }

    RenderOptions options;
    options.parallelFor = analysis.makeParallelFor();
    options.shouldCancel = [&] { return threadShouldExit() || removing.load() == source || analysis.getBlocks(source) != blocks; };
    rendering = true;
    auto result = renderEdited(*sourceAudio, *features, *blocks, options);
    rendering = false;
    if (result)
        publish(source, std::make_shared<const AudioPlanes>(std::move(*result)), blocks);
}

void RenderCache::run()
{
    while (! threadShouldExit())
    {
        std::vector<juce::ARAAudioSource*> sources;
        {
            const std::lock_guard<std::mutex> guard(mutex);
            for (const auto& e : entries)
                sources.push_back(e.first);
        }
        for (auto* source : sources)
        {
            const std::lock_guard<std::mutex> work(workMutex);
            if (threadShouldExit())
                return;
            {
                const std::lock_guard<std::mutex> guard(mutex);
                if (entries.find(source) == entries.end())
                    continue;
            }
            service(source);
        }
        releaseRetired();
        wait(100);
    }
}
