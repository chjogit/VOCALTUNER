#include "PlaybackRenderer.h"

using namespace juce;

PlaybackRenderer::PlaybackRenderer(ARA::PlugIn::DocumentController* dc, const ProcessingLock& lock, RenderCache& cache,
                                   const PreviewRequest& previewToFollow)
    : ARAPlaybackRenderer(dc), processingLock(lock), renderCache(cache), previewRequest(previewToFollow)
{
}

void PlaybackRenderer::prepareToPlay(double newSampleRate, int maxBlockSize, int newNumChannels,
                                     AudioProcessor::ProcessingPrecision, AlwaysNonRealtime alwaysNonRealtime)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;
    useBuffering = alwaysNonRealtime == AlwaysNonRealtime::no;
    mixBuffer.setSize(numChannels, maxBlockSize);
    fadeBuffer.setSize(numChannels, maxBlockSize);
    fadeLength = jmax(1, roundToInt(0.02 * sampleRate));     // 20 ms

    if (useBuffering && ! readAheadThread.isThreadRunning())
        readAheadThread.startThread();

    for (auto* region : getPlaybackRegions())
    {
        auto* source = region->getAudioModification()->getAudioSource();
        if (readers.find(source) == readers.end())
            readers.emplace(source, createReader(source));
        playing.try_emplace(source);
    }
}

void PlaybackRenderer::releaseResources()
{
    readers.clear();
    playing.clear();
    readAheadThread.stopThread(1000);
}

PlaybackRenderer::Reader PlaybackRenderer::createReader(ARAAudioSource* source)
{
    auto direct = std::make_unique<ARAAudioSourceReader>(source);
    if (! useBuffering)
        return { std::move(direct), nullptr };

    // 실시간 재생 중 디스크/호스트 읽기 대기를 피하려고 2초 선읽기
    const auto readAhead = jmax(4 * mixBuffer.getNumSamples(), roundToInt(2.0 * sampleRate));
    auto buffered = std::make_unique<BufferingAudioReader>(direct.release(), readAheadThread, readAhead);
    auto* raw = buffered.get();
    return { std::move(buffered), raw };
}

bool PlaybackRenderer::canRender(const ARAAudioSource& source) const noexcept
{
    // 샘플레이트/채널 변환은 아직 미지원
    return source.getChannelCount() == numChannels && exactlyEqual(source.getSampleRate(), sampleRate);
}

bool PlaybackRenderer::processBlock(AudioBuffer<float>& buffer, AudioProcessor::Realtime realtime,
                                    const AudioPlayHead::PositionInfo& position) noexcept
{
    const auto lock = processingLock.tryLockForProcessing();
    buffer.clear();
    if (! lock.isLocked())
        return false;
    if (! position.getIsPlaying())
    {
        renderPreview(buffer, realtime);
        return true;
    }
    preview.source = nullptr;   // 재생이 시작되면 미리 듣기는 끝

    const auto block = Range<int64>::withStartAndLength(position.getTimeInSamples().orFallback(0),
                                                       buffer.getNumSamples());
    bool didRender = false;
    bool success = true;

    for (auto* region : getPlaybackRegions())
        success &= renderRegion(*region, buffer, block, realtime, didRender);

    return success;
}

// 지금 재생할 결과. 실시간에서 잠금을 못 잡으면 직전 결과를 그대로 쓴다 (원본으로 잠깐 바뀌면 '틱' 소리가 남)
std::shared_ptr<const AudioPlanes> PlaybackRenderer::latestFor(ARAAudioSource* source, Playing* state,
                                                               AudioProcessor::Realtime realtime) const
{
    std::shared_ptr<const AudioPlanes> latest;
    if (realtime == AudioProcessor::Realtime::no)
    {
        renderCache.waitUntilUpToDate(source, 10 * 60 * 1000);   // 오프라인 바운스는 렌더가 끝날 때까지 기다린다
        latest = renderCache.getRendered(source);
    }
    else if (! renderCache.tryGetRendered(source, latest))
        latest = state != nullptr ? state->current : nullptr;
    if (latest != nullptr && ! exactlyEqual((double) latest->sampleRate, sampleRate))
        latest = nullptr;
    return latest;
}

void PlaybackRenderer::switchTo(Playing& state, std::shared_ptr<const AudioPlanes> latest, AudioProcessor::Realtime realtime) const noexcept
{
    if (state.known && latest == state.current)
        return;
    // 이전 결과는 RenderCache가 보관 중이라 여기서 놓아도 오디오 스레드에서 해제되지 않는다
    state.previous = state.known ? std::move(state.current) : nullptr;
    state.current = std::move(latest);
    state.fadeDone = state.known && realtime == AudioProcessor::Realtime::yes ? 0 : fadeLength;
    state.known = true;
}

bool PlaybackRenderer::readSource(const AudioPlanes* planes, ARAAudioSource* source, AudioBuffer<float>& target,
                                  int startInBuffer, int numSamples, int64 startInSource, AudioProcessor::Realtime realtime)
{
    if (planes != nullptr)
        return copyRendered(*planes, target, startInBuffer, numSamples, startInSource);

    const auto it = readers.find(source);
    if (it == readers.end() || ! canRender(*source))
        return false;
    if (it->second.buffered != nullptr)
        it->second.buffered->setReadTimeout(realtime == AudioProcessor::Realtime::no ? 100 : 0);
    return it->second.reader->read(&target, startInBuffer, numSamples, startInSource, true, true);
}

// mixBuffer(새 결과)에 이전 결과를 겹쳐 fadeLength 동안 서서히 넘어간다
void PlaybackRenderer::fadeFromPrevious(Playing& state, ARAAudioSource* source, int startInBuffer, int numSamples,
                                        int64 startInSource, AudioProcessor::Realtime realtime)
{
    const auto count = jmin(numSamples, fadeLength - state.fadeDone);
    if (count <= 0)
        return;
    if (readSource(state.previous.get(), source, fadeBuffer, startInBuffer, count, startInSource, realtime))
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* mix = mixBuffer.getWritePointer(ch, startInBuffer);
            const auto* old = fadeBuffer.getReadPointer(ch, startInBuffer);
            for (int i = 0; i < count; ++i)
            {
                const auto g = (float) (state.fadeDone + i + 1) / (float) fadeLength;
                mix[i] = mix[i] * g + old[i] * (1.0f - g);
            }
        }
    state.fadeDone += count;
    if (state.fadeDone >= fadeLength)
        state.previous = nullptr;
}

bool PlaybackRenderer::renderRegion(ARAPlaybackRegion& region, AudioBuffer<float>& output,
                                    Range<int64> block, AudioProcessor::Realtime realtime, bool& didRender)
{
    const auto songRange = region.getSampleRange(sampleRate, ARAPlaybackRegion::IncludeHeadAndTail::no);
    const Range<int64> modRange { region.getStartInAudioModificationSamples(),
                                  region.getEndInAudioModificationSamples() };
    const auto render = block.getIntersectionWith(songRange)
                             .getIntersectionWith(modRange.movedToStartAt(songRange.getStart()));
    if (render.isEmpty())
        return true;

    auto* source = region.getAudioModification()->getAudioSource();
    const auto numSamples = (int) render.getLength();
    const auto startInBuffer = (int) (render.getStart() - block.getStart());
    const auto startInSource = render.getStart() + modRange.getStart() - songRange.getStart();

    // 편집 결과가 있으면 그것을, 없으면 원본을 재생/바운스
    const auto found = playing.find(source);
    auto* state = found != playing.end() ? &found->second : nullptr;
    auto latest = latestFor(source, state, realtime);
    if (state != nullptr)
        switchTo(*state, std::move(latest), realtime);
    const auto* current = state != nullptr ? state->current.get() : latest.get();

    if (! readSource(current, source, mixBuffer, startInBuffer, numSamples, startInSource, realtime))
        return false;
    if (state != nullptr && state->fadeDone < fadeLength)
        fadeFromPrevious(*state, source, startInBuffer, numSamples, startInSource, realtime);
    overlaySegment(source, state, startInBuffer, numSamples, startInSource, realtime);

    for (int ch = 0; ch < numChannels; ++ch)
        output.addFrom(ch, startInBuffer, mixBuffer, ch, startInBuffer, numSamples);
    didRender = true;
    return true;
}

bool PlaybackRenderer::copyRendered(const AudioPlanes& planes, AudioBuffer<float>& target, int startInBuffer, int numSamples,
                                    int64 startInSource) const noexcept
{
    if (planes.channels.empty() || startInSource < 0)
        return false;
    const auto available = (int64) planes.length() - startInSource;
    const auto count = (int) jlimit<int64>(0, numSamples, available);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* dest = target.getWritePointer(ch, startInBuffer);
        const auto& src = planes.channels[(size_t) jmin(ch, (int) planes.channels.size() - 1)];
        FloatVectorOperations::copy(dest, src.data() + startInSource, count);
        FloatVectorOperations::clear(dest + count, numSamples - count);
    }
    return true;
}
