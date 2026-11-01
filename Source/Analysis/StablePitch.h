#pragma once
#include <cstddef>
#include "VocalFeatures.h"

// 블록 중심 음정 계산에 쓸 프레임: 유성 프레임 중 이웃 프레임과 비슷한 음정인 것만 (옥타브 오분석, 튀는 프레임, 막 시작·끝나는
// 불안정한 음정 제외). 안정한 프레임이 너무 적으면 모든 유성 프레임을 그대로 쓴다 (짧은 블록, 계속 미끄러지는 음).
constexpr size_t stableNeighbourFrames = 3;       // 앞뒤로 이만큼 프레임(유성만)의 중앙값과 비교
constexpr double stableToleranceSemitones = 0.5;  // 이웃 중앙값에서 이만큼 안이면 안정
constexpr double stableMinShare = 0.4;            // 안정한 프레임이 유성 프레임의 이 비율 미만이면 전부 사용
constexpr size_t stableMinFrames = 3;

// [begin, end)에서 중심 음정 계산에 쓸 음정들 (프레임 순서)
std::vector<double> centreMidiFrames(const VocalFeatures& f, size_t begin, size_t end);

// 중앙값 (짝수 개면 가운데 둘의 평균). 비어 있으면 0
double medianOf(std::vector<double> values);
