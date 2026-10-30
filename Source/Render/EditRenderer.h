#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <vector>
#include "BlockParser.h"
#include "VocalFeatures.h"

// 편집(음정 이동 / 곡선 / 비브라토 / 펴기 / 타이밍 / 볼륨)을 반영한 오디오를 만든다. JUCE 비의존.
// 바뀐 음정은 WORLD(CheapTrick + D4C + Synthesis)로 다시 합성하고, 바뀌지 않은 부분은 원본 그대로(시간만 대응) 쓴다.

struct AudioPlanes
{
    std::vector<std::vector<float>> channels;
    int sampleRate = 44100;
    size_t length() const noexcept { return channels.empty() ? 0 : channels.front().size(); }
};

struct RenderOptions
{
    ParallelFor parallelFor;                    // 합성 구간을 병렬로
    std::function<bool()> shouldCancel;
    double chunkSeconds = 10.0;                 // 합성 구간 길이
    double unchangedSemitones = 0.12;           // 이 이하의 음정 변화는 원본을 그대로 쓴다
};

// features는 source와 같은 길이의 모노 분석 결과. 취소되면 nullopt.
std::optional<AudioPlanes> renderEdited(const AudioPlanes& source, const VocalFeatures& features,
                                        const std::vector<VocalBlock>& blocks, const RenderOptions& = {});

// 출력 구간 [beginSample, endSample)만 앞뒤 여유를 붙여 렌더한다 (블록 미리 듣기용, 전체 렌더보다 훨씬 빠름).
// audio.channels[c][0]이 출력의 startSample 번째 샘플. 취소되면 nullopt.
struct RenderedSegment
{
    size_t startSample = 0;
    AudioPlanes audio;
    size_t endSample() const noexcept { return startSample + audio.length(); }
};
std::optional<RenderedSegment> renderEditedRange(const AudioPlanes& source, const VocalFeatures& features,
                                                 const std::vector<VocalBlock>& blocks, size_t beginSample, size_t endSample,
                                                 const RenderOptions& = {});

// 이 블록 목록이 소리를 바꾸는지 (바꾸지 않으면 원본을 그대로 쓰면 됨)
bool changesAudio(const std::vector<VocalBlock>& blocks, const VocalFeatures& features, double unchangedSemitones = 0.12);
