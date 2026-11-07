#pragma once
#include <cstdint>
#include <functional>
#include "GuidedMelody.h"
#include "TimeWarp.h"
#include "VocalFeatures.h"

// 반주 섞인 원곡에 대한 박자 찾기. JUCE 비의존.
// 스펙트럼 정렬(DTW)은 반주까지 맞추려 들어 믹스에서는 엉뚱한 쪽으로 밀린다(실측: 0.25초 당겨야 할 곳을 0.14초 늦춤).
// 대신 따라 부른 음 자체를 본보기로, 그 음(과 배음)이 원곡 가운데 성분에서 가장 뚜렷하게 들리는 시차를 구간마다 찾는다.
struct GuidedTimingOptions
{
    double windowSec = 9.0;         // 한 번에 보는 구간
    double stepSec = 3.0;
    double maxLagSec = 0.6;         // 찾는 시차 범위 ±
    double hopSec = 0.01;
    int frameSize = 2048;
    double minConfidence = 1.5;     // 가장 좋은 시차가 다른 시차들보다 표준편차 몇 배 뚜렷해야 그 구간을 쓰는지
    double toleranceSec = 0.06;     // 전체 시차에서 이만큼 안쪽의 차이는 흔들림으로 보고 무시 (절반 안쪽 이동은 0)
    double jumpCostPerSec = 20.0;   // 이웃 구간과 시차가 1초 달라질 때의 벌점 (점수는 표준편차 단위)
    int transposeSemitones = 2;     // 음을 이만큼 틀리게 부른 프레이즈까지 박자를 찾는다
    std::function<bool()> shouldCancel;
    // 진단용(선택): 구간마다 (시작 곡 시간, 유성 비율, 가장 뚜렷한 시차의 점수, 그 시차 초). 건너뛴 구간은 점수 0
    std::function<void(double, double, double, double)> onWindow;
};

// 타깃 곡 시간 -> 원곡 곡 시간. 확신 있는 구간이 하나도 없으면 빈 TimeMap (박자를 옮기지 않는다).
TimeMap guidedTimeMap(int64_t referenceLength, int referenceRate, const StereoReader& readReference,
                      const VocalFeatures& target, double targetOffsetSec, double referenceOffsetSec,
                      const GuidedTimingOptions& = {});
