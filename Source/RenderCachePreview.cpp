#include "RenderCache.h"

// 블록 미리 듣기용 구간 렌더. 전체 렌더는 편집이 멈추고 기다린 뒤 소스 전체를 만들지만,
// 미리 듣기는 누른 블록 구간만 바로 만들어 피치를 바꾸는 동안에도 바뀐 소리를 들려준다.

void RenderCache::requestPreview(juce::ARAAudioSource* source, std::shared_ptr<const std::vector<VocalBlock>> blocks,
                                 juce::int64 begin, juce::int64 end)
{
    {
        const std::lock_guard<std::mutex> guard(mutex);
        previewJob.source = source;
        previewJob.blocks = std::move(blocks);
        previewJob.begin = begin;
        previewJob.end = end;
    }
    previewActive = true;       // 진행 중인 전체 렌더는 멈춘다
    notify();
}

void RenderCache::clearPreview()
{
    const std::lock_guard<std::mutex> guard(mutex);
    previewJob = {};
    previewActive = false;
}

bool RenderCache::isPreviewReady(juce::ARAAudioSource* source, const std::shared_ptr<const std::vector<VocalBlock>>& blocks) const
{
    const std::lock_guard<std::mutex> guard(mutex);
    const auto it = entries.find(source);
    return it != entries.end() && it->second.segment != nullptr && it->second.segmentFor == blocks;
}

bool RenderCache::tryGetSegment(juce::ARAAudioSource* source, std::shared_ptr<const RenderedSegment>& out) const noexcept
{
    std::unique_lock<std::mutex> guard(mutex, std::try_to_lock);
    if (! guard.owns_lock())
        return false;
    const auto it = entries.find(source);
    out = it == entries.end() ? nullptr : it->second.segment;
    return true;
}

std::shared_ptr<const AudioPlanes> RenderCache::sourceAudioFor(juce::ARAAudioSource* source)
{
    {
        const std::lock_guard<std::mutex> guard(mutex);
        const auto it = entries.find(source);
        if (it == entries.end())
            return nullptr;
        if (it->second.sourceAudio != nullptr)
            return it->second.sourceAudio;
    }
    auto audio = loadSourceAudio(source);
    const std::lock_guard<std::mutex> guard(mutex);
    if (const auto it = entries.find(source); it != entries.end() && audio != nullptr)
        it->second.sourceAudio = audio;
    return audio;
}

void RenderCache::publishSegment(const PreviewJob& job, std::shared_ptr<const std::vector<VocalBlock>> blocks, RenderedSegment segment)
{
    const std::lock_guard<std::mutex> guard(mutex);
    const auto it = entries.find(job.source);
    if (it == entries.end())
        return;
    if (it->second.segment != nullptr)
        retired.push_back(std::move(it->second.segment));     // 오디오 스레드가 아직 쥐고 있을 수 있음
    it->second.segment = std::make_shared<const RenderedSegment>(std::move(segment));
    it->second.segmentFor = blocks;
    if (previewJob.source == job.source)
    {
        previewJob.renderedFor = std::move(blocks);
        previewJob.renderedBegin = job.begin;
        previewJob.renderedEnd = job.end;
    }
}

// 미리 듣기 요청이 걸려 있으면 true (그동안 전체 렌더는 하지 않는다)
bool RenderCache::servicePreview()
{
    PreviewJob job;
    {
        const std::lock_guard<std::mutex> guard(mutex);
        job = previewJob;
    }
    if (job.source == nullptr)
        return false;
    const std::lock_guard<std::mutex> work(workMutex);
    {
        const std::lock_guard<std::mutex> guard(mutex);
        if (entries.find(job.source) == entries.end())
            return true;        // 사라진 소스
    }
    const auto& blocks = job.blocks;
    const auto features = analysis.getFeatures(job.source);
    if (blocks == nullptr || features == nullptr || threadShouldExit()
        || (blocks == job.renderedFor && job.begin == job.renderedBegin && job.end == job.renderedEnd))
        return true;
    const auto audio = sourceAudioFor(job.source);
    if (audio == nullptr)
        return true;

    RenderOptions options;
    options.shouldCancel = [&] { return threadShouldExit() || removing.load() == job.source; };
    rendering = true;
    auto segment = renderEditedRange(*audio, *features, *blocks, (size_t) juce::jmax<juce::int64>(0, job.begin),
                                     (size_t) juce::jmax<juce::int64>(0, job.end), options);
    rendering = false;
    if (segment)
        publishSegment(job, blocks, std::move(*segment));
    return true;
}
