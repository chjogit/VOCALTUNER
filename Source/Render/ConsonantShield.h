#pragma once
#include <cstddef>
#include <vector>

// 자음 보호: 유성으로 잡혔지만 마찰음·파열음처럼 들리는 프레임(영교차율 높음 + 4 kHz 위 에너지 비중 큼)에서는
// 음정 이동을 0으로 만든다. 그런 소리에 피치 이동을 걸면 금속성 위상 잡음이 생긴다.
// 이동량은 보호 구간 둘레에서 rampFrames 동안 서서히 0이 되어, 이동이 갑자기 끊기지 않는다.
constexpr double shieldZeroCrossingRate = 0.3;     // 샘플당 영교차 (약 7 kHz 중심 소리)
constexpr double shieldHighBandShare = 0.5;        // 4 kHz 위 에너지가 프레임 에너지의 절반 넘게
constexpr size_t shieldRampFrames = 2;

// shift[i] (반음, 프레임 i = 샘플 i*framePeriod*rate 부터 한 프레임 + 한 프레임 겹침)을 보호 구간에서 0으로
// [firstFrame, lastFrame) 밖은 건드리지 않고 분석도 하지 않는다 (구간 렌더가 곡 전체를 훑지 않게)
std::vector<double> shieldedShift(const float* samples, size_t length, int sampleRate, double framePeriodSec,
                                  const std::vector<double>& shift, size_t firstFrame = 0, size_t lastFrame = static_cast<size_t>(-1));

// 어느 프레임이 보호 구간인지 (테스트용)
std::vector<char> fricativeFrames(const float* samples, size_t length, int sampleRate, double framePeriodSec, size_t frames,
                                  size_t firstFrame = 0);   // 결과[i] = 프레임 firstFrame + i
