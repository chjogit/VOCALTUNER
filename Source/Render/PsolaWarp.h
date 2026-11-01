#pragma once
#include <cstddef>
#include <functional>
#include <vector>
#include "Psola.h"

// 박자(시간 변형)와 음정을 한 번에 처리하는 TD-PSOLA. JUCE 비의존.
// 출력 시각마다 "원본의 어느 시각을 가져올지"(toSource)를 따라가며 조각을 겹쳐 놓는다.
// - 유성: 원본 피치 마크의 2주기 조각을 새 주기 간격으로 놓는다. 원본이 느리게 흘러가면(늘임) 같은 조각이 되풀이되고,
//   빠르게 흘러가면 일부 조각이 건너뛰어진다. 조각 안의 포먼트·숨결은 그대로.
// - 무성(자음, 숨): 5ms 간격 조각으로 1:1 복사한다. 늘이지 않고 옮기기만 하는 구간(통째 이동)은 원본과 똑같이 재구성된다.
// - 박자가 그대로이고 이동도 0이면 원본 마크 쪽으로 위상을 끌어당겨 원본과 이어 붙일 때 빗살 상쇄가 없다.
struct WarpInput
{
    const float* samples = nullptr;         // 원본 채널 하나
    size_t length = 0;
    int sampleRate = 44100;
    double framePeriodSec = 0.005;
    const double* sourceMidi = nullptr;     // 원본 음정 (원본 시간 프레임, 0 = 무성)
    size_t sourceFrames = 0;
    const double* shiftSemitones = nullptr; // 옮길 반음 (출력 시간 프레임)
    size_t outFrames = 0;
    std::function<double(double)> toSource;   // 출력 초 -> 원본 초
    std::function<double(double)> toDisplay;  // 원본 초 -> 출력 초
};

// 출력 샘플 [outBegin, outEnd)의 결과. 앞뒤 무성 틈까지 넓혀 계산하므로 어느 구간을 잘라 렌더해도 같은 샘플은 같은 값이다.
std::vector<float> psolaWarp(const WarpInput&, size_t outBegin, size_t outEnd);
