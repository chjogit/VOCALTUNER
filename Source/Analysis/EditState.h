#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include "BlockParser.h"

// 블록 목록(구조 + 편집)의 저장/복원용 직렬화. JUCE 비의존. 구조체 메모리 배치에 의존하지 않는 리틀엔디언 형식.

// 저장 당시의 분석 결과와 지금 분석 결과가 같은 오디오에서 나왔는지 확인하는 값 (모두 정확히 같아야 함)
struct EditFingerprint
{
    double sampleRate = 0.0;
    int64_t numSamples = 0;
    size_t numFrames = 0;      // VocalFeatures 프레임 수
    uint64_t contentHash = 0;  // audioContentHash. 0 = 모름 (형식 1·2로 저장, 복원 때 길이만 비교)

    bool operator==(const EditFingerprint& o) const noexcept
    {
        return sampleRate == o.sampleRate && numSamples == o.numSamples && numFrames == o.numFrames
               && contentHash == o.contentHash;
    }
    bool operator!=(const EditFingerprint& o) const noexcept { return ! (*this == o); }
};

// 분석에 쓴 모노 샘플의 내용 해시 (16비트로 양자화한 값의 FNV-1a 64). 0을 돌려주지 않는다.
// 길이가 같아도 오디오 내용이 바뀌면 옛 편집을 얹지 않기 위해 지문에 넣는다.
uint64_t audioContentHash(const std::vector<double>& mono) noexcept;

std::vector<uint8_t> encodeEditState(const std::vector<VocalBlock>& blocks, const EditFingerprint& fingerprint);

// 형식 오류, 지문 불일치, 잘못된 블록이 하나라도 있으면 nullopt.
// 형식 3은 내용 해시까지 비교한다. 해시가 없던 형식 1·2(1.1.11 이전 저장)는 길이만 비교한다.
std::optional<std::vector<VocalBlock>> decodeEditState(const uint8_t* data, size_t size, const EditFingerprint& expected);

// 현재 블록 목록이 첫 분석 결과(original)와 다르면 true: 구간이 다르거나(자르기/붙이기) 편집 필드가 다름
bool hasEdits(const std::vector<VocalBlock>& blocks, const std::vector<VocalBlock>& original);
