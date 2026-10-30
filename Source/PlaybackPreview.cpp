#include "PlaybackRenderer.h"
#include <utility>

using namespace juce;

// 블록 미리 듣기: 편집이 반영된 렌더 결과(없으면 원본)를 요청 구간만큼 재생한다. 오디오 스레드 전용.

ARAAudioSource* PlaybackRenderer::ownedSource(const void* id) const noexcept
{
    // 이 렌더러가 맡은 트랙의 소스일 때만 소리를 낸다 (다른 트랙 인스턴스와 겹쳐 나지 않게)
    for (const auto& entry : playing)
        if (entry.first == id)
            return entry.first;
    return nullptr;
}

void PlaybackRenderer::followPreviewRequest()
{
    PreviewRequest::Snapshot request;
    if (! previewRequest.read(request) || request.generation == preview.generation)
        return;
    preview.generation = request.generation;

    const auto audible = preview.source != nullptr && preview.fadeOut < 0 && preview.position < preview.end;
    if (request.source == nullptr)
    {
        preview.next = nullptr;
        if (audible)
            preview.fadeOut = fadeLength;   // 놓았을 때 '툭' 끊기지 않게 짧게 줄인다
        return;
    }
    auto* source = ownedSource(request.source);
    if (audible && source != nullptr)
    {
        // 재생 중에 새 소리로 바뀌면: 지금 소리를 짧게 줄인 뒤 블록 처음부터 다시
        preview.next = source;
        preview.nextBegin = request.begin;
        preview.nextEnd = request.end;
        preview.fadeOut = fadeLength;
        return;
    }
    preview = { source, request.begin, request.end, request.generation, 0, -1, nullptr, 0, 0 };
}

void PlaybackRenderer::applyPreviewEnvelope(int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        auto gain = 1.0f;
        if (preview.fadeIn < fadeLength)
            gain = (float) ++preview.fadeIn / (float) fadeLength;
        if (preview.fadeOut >= 0)
            gain *= (float) jmax(0, preview.fadeOut--) / (float) fadeLength;
        const auto remaining = preview.end - (preview.position + i);
        gain *= jlimit(0.0f, 1.0f, (float) remaining / (float) fadeLength);   // 블록 끝에서도 줄인다
        for (int ch = 0; ch < numChannels; ++ch)
            mixBuffer.getWritePointer(ch)[i] *= gain;
    }
}

void PlaybackRenderer::renderPreview(AudioBuffer<float>& output, AudioProcessor::Realtime realtime)
{
    followPreviewRequest();
    if (preview.fadeOut == 0 && preview.next != nullptr)
        preview = { preview.next, preview.nextBegin, preview.nextEnd, preview.generation, 0, -1, nullptr, 0, 0 };
    auto* source = preview.source;
    const auto numSamples = (int) jmin<int64>(output.getNumSamples(), preview.end - preview.position);
    if (source == nullptr || numSamples <= 0 || preview.fadeOut == 0)
    {
        preview.source = nullptr;
        return;
    }

    const auto found = playing.find(source);
    auto* state = found != playing.end() ? &found->second : nullptr;
    auto latest = latestFor(source, state, realtime);
    if (state != nullptr)
        switchTo(*state, std::move(latest), realtime);
    const auto* current = state != nullptr ? state->current.get() : latest.get();

    if (readSource(current, source, mixBuffer, 0, numSamples, preview.position, realtime))
    {
        if (state != nullptr && state->fadeDone < fadeLength)
            fadeFromPrevious(*state, source, 0, numSamples, preview.position, realtime);
        overlaySegment(source, state, 0, numSamples, preview.position, realtime);
        applyPreviewEnvelope(numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
            output.addFrom(ch, 0, mixBuffer, ch, 0, numSamples);
    }
    preview.position += numSamples;
}

namespace
{
// 구간 결과가 덮는 위치면 base와 섞은 값 (경계 fade 샘플 동안 크로스페이드), 아니면 base 그대로
float segmentSample(const RenderedSegment* seg, int ch, int64 p, float base, int fade) noexcept
{
    if (seg == nullptr || p < (int64) seg->startSample || p >= (int64) seg->endSample())
        return base;
    const auto offset = p - (int64) seg->startSample;
    const auto w = (float) jlimit<int64>(0, fade, jmin(offset, (int64) seg->endSample() - 1 - p)) / (float) fade;
    const auto& src = seg->audio.channels[(size_t) jmin(ch, (int) seg->audio.channels.size() - 1)];
    return base * (1.0f - w) + src[(size_t) offset] * w;
}

bool usable(const RenderedSegment* seg, double sampleRate) noexcept
{
    return seg != nullptr && ! seg->audio.channels.empty() && exactlyEqual((double) seg->audio.sampleRate, sampleRate);
}
} // namespace

// 누르고 있는 블록의 구간 결과가 전체 렌더보다 새로우면 그 구간을 덮어쓴다.
// 피치를 바꿔 새 결과가 오면 재생을 끊지 않고 그 자리에서 20ms 동안 새 소리로 넘어간다.
void PlaybackRenderer::overlaySegment(ARAAudioSource* source, Playing* state, int startInBuffer, int numSamples, int64 startInSource,
                                      AudioProcessor::Realtime realtime)
{
    if (realtime == AudioProcessor::Realtime::no)
        return;     // 바운스는 전체 렌더를 기다려 쓴다
    std::shared_ptr<const RenderedSegment> latest;
    if (! renderCache.tryGetSegment(source, latest))
        latest = state != nullptr ? state->segment : nullptr;
    if (state != nullptr && latest != state->segment)
    {
        // 놓아도 RenderCache가 보관 중이라 여기서 해제되지 않는다
        state->previousSegment = std::exchange(state->segment, latest);
        state->segmentFade = 0;
    }
    const auto* seg = usable(latest.get(), sampleRate) ? latest.get() : nullptr;
    const auto* old = state != nullptr && state->segmentFade < fadeLength && usable(state->previousSegment.get(), sampleRate)
                          ? state->previousSegment.get() : nullptr;
    if (seg == nullptr && old == nullptr)
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        const auto p = startInSource + i;
        const auto g = old == nullptr ? 1.0f : (float) jmin(fadeLength, state->segmentFade + i + 1) / (float) fadeLength;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& out = mixBuffer.getWritePointer(ch)[startInBuffer + i];
            const auto base = out;
            out = segmentSample(seg, ch, p, base, fadeLength) * g + (old != nullptr ? segmentSample(old, ch, p, base, fadeLength) * (1.0f - g) : 0.0f);
        }
    }
    if (state != nullptr)
        state->segmentFade = jmin(fadeLength, state->segmentFade + numSamples);
}
