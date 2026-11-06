#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "BlockParser.h"
#include "VocalFeatures.h"

// 편집(음정 이동 / 곡선 / 비브라토 / 펴기 / 타이밍 / 볼륨)을 반영한 오디오를 만든다. JUCE 비의존.
// 음정 이동과 박자 변경 모두 PSOLA(원본 파형 조각을 옮겨 놓음)로 만든다. 박자가 그대로면 Psola, 바뀌면 PsolaWarp.
// 바뀌지 않은 부분은 원본 그대로(시간만 대응) 쓴다. WORLD는 음정 추출(Harvest)에만 쓴다.

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
    bool levelMatch = false;                    // 옮긴 구간의 음량을 원본에 맞춘다 (PSOLA 에너지 손실 보정, 기본 꺼짐)
    bool vowelFocus = true;                     // 숨소리·비음·자음 같은 모음 아닌 유성 프레임의 이동을 줄인다 (기본 켬, 사용자 청취 승인 11/6)
    double unchangedSemitones = 0.05;           // 이 이하의 음정 변화는 원본을 그대로 쓴다 (5센트, 들리지 않음)
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
bool changesAudio(const std::vector<VocalBlock>& blocks, const VocalFeatures& features, double unchangedSemitones = 0.05);
