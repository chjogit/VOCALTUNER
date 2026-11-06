#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include "BlockParser.h"

// 블록 목록(구조 + 편집)의 저장/복원용 직렬화. JUCE 비의존. 구조체 메모리 배치에 의존하지 않는 리틀엔디언 형식.

// 저장 당시의 분석 결과와 지금 분석 결과가 같은 오디오에서 나왔는지 확인하는 값 (셋 다 정확히 같아야 함)
struct EditFingerprint
{
    double sampleRate = 0.0;
    int64_t numSamples = 0;
    size_t numFrames = 0;   // VocalFeatures 프레임 수

    bool operator==(const EditFingerprint& o) const noexcept
    {
        return sampleRate == o.sampleRate && numSamples == o.numSamples && numFrames == o.numFrames;
    }
    bool operator!=(const EditFingerprint& o) const noexcept { return ! (*this == o); }
};

std::vector<uint8_t> encodeEditState(const std::vector<VocalBlock>& blocks, const EditFingerprint& fingerprint);

// 형식 오류, 지문 불일치, 잘못된 블록이 하나라도 있으면 nullopt
std::optional<std::vector<VocalBlock>> decodeEditState(const uint8_t* data, size_t size, const EditFingerprint& expected);
