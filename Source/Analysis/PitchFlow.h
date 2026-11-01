#pragma once
#include <cstddef>
#include <vector>
#include "BlockParser.h"

// 원본 F0 흐름을 지키는 음정 이동량 (사용자 편집 원칙, 2026-11-01).
// - 블롭 안 안착부(블롭 중앙값 ±0.3반음)는 이동량 그대로, 비브라토·드리프트 모양은 손대지 않는다.
// - 이웃 유성 블롭과의 이음: 시간에 비례하지 않고, 원래 음이 움직인 만큼 비례해 이동량이 바뀐다.
//   음이 머무는 곳에서는 이동량이 그대로라 이음(포르타멘토·스쿱)의 모양이 남는다.
// - 무음/자음에서 올라오는 스쿱은 0 -> 이동량, 무음으로 떨구는 꼬리는 이동량 -> 0 (경사면 0%).

constexpr double landingNearSemitones = 0.3;
constexpr size_t minSlopeFrames = 3;
// 이음 구간에서 원래 음이 이만큼(반음) 넘게 움직여야 "움직인 만큼 비례"를 온전히 쓴다. 덜 움직였으면 시간에 따른 부드러운 곡선과 섞는다
// (거의 안 움직이는 곳의 미세한 떨림이 이동량 전체를 한순간에 바꾸는 계단이 되지 않게)
constexpr double minFlowMovementSemitones = 0.5;

// 프레임의 이동량 (반음). 자음 블록은 블록 이동량 그대로.
double flowPitchShift(const VocalFeatures&, const std::vector<VocalBlock>& blocks, size_t index, size_t frame) noexcept;

// 블롭 안에서 이동량에 곱해지는 비율 0..1 (스쿱/꼬리). withHead/withTail = 그쪽에 이음이 없을 때만.
double landingWeight(const VocalFeatures&, const VocalBlock&, size_t frame, bool withHead, bool withTail) noexcept;

// 값들이 움직인 누적 비율 0..1 (움직임이 없으면 직선). 마지막 값은 1.
std::vector<double> movementProgress(const std::vector<double>& values);
// 값들이 움직인 총량 (인접 차의 절댓값 합)
double totalMovement(const std::vector<double>& values);
