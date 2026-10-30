#include "PlaybackRenderer.h"

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

    if (request.source == nullptr)
    {
        if (preview.source != nullptr && preview.fadeOut < 0)
            preview.fadeOut = fadeLength;   // 놓았을 때 '툭' 끊기지 않게 짧게 줄인다
        return;
    }
    preview = { ownedSource(request.source), request.begin, request.end, request.generation, 0, -1 };
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
        applyPreviewEnvelope(numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
            output.addFrom(ch, 0, mixBuffer, ch, 0, numSamples);
    }
    preview.position += numSamples;
}
