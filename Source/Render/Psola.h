#pragma once
#include <cstddef>
#include <vector>

// 원본 파형 조각을 옮기는 음정 이동 (TD-PSOLA). JUCE 비의존.
// 유성 구간에 주기마다 피치 마크를 놓고(앞 주기와 파형이 가장 닮은 자리), 두 주기 길이의 조각을
// 새 간격으로 다시 겹쳐 놓는다. 조각 안의 포먼트·숨결은 원본 그대로 남는다.
// 이동이 0에 가까운 곳은 원본 마크 쪽으로 위상을 끌어당겨, 원본과 이어 붙일 때 빗살 상쇄가 없다.

struct PsolaInput
{
    const float* samples = nullptr;     // 채널 하나
    size_t length = 0;
    int sampleRate = 44100;
    double framePeriodSec = 0.005;      // 아래 두 곡선의 프레임 간격
    const double* sourceMidi = nullptr; // 원본 음정 (0 = 무성)
    const double* shiftSemitones = nullptr;   // 프레임마다 옮길 양 (반음)
    size_t frames = 0;
    // 프레임마다 포먼트를 원본 자리에 두는 정도 (0..1). nullptr = 어디서나 1 (조각을 그대로 옮김, 기존 결과와 같다)
    // 1보다 작으면 조각을 2^((1-양)*이동/12)배 빠르게 읽어 포먼트가 음정과 같이 움직인다
    const double* formantAmount = nullptr;
};

// [beginSample, endSample) 구간의 결과. 옮길 것이 없는 곳(이동 0, 무성)은 원본 샘플 그대로다.
// 안에서는 앞뒤 무성 틈까지 넓혀 계산하므로, 어느 구간을 잘라 렌더해도 같은 샘플은 같은 값이 된다.
std::vector<float> psolaShift(const PsolaInput&, size_t beginSample, size_t endSample);
// 넓히지 않고 주어진 구간만으로 계산 (시제품 비교용)
std::vector<float> psolaShiftExact(const PsolaInput&, size_t beginSample, size_t endSample);

// 피치 마크 (샘플 위치). 유성 구간마다 주기 간격, 앞 주기와 가장 닮은 자리. 테스트/재사용용
std::vector<long> pitchMarks(const PsolaInput&, size_t beginSample, size_t endSample);
