#pragma once
#include <functional>
#include "TimeWarp.h"
#include "VocalFeatures.h"

// 분리 보컬(반주 없는) 기준에 대한 프레이즈 단위 박자 찾기. JUCE 비의존.
// GuidedTiming은 9초 창마다 시차 하나를, 같은 음(±2반음)이 원곡에서 들리는 정도로 찾는다. 그래서
//   - 노래가 30% 안 되는 창(문장 중간에 잠깐 들어오는 화음)은 건너뛰고 먼 구간의 시차를 받고
//   - 3도·5도 위 화음은 기준 음을 찾지 못한다.
// 여기서는 따라가는 트랙을 무음으로 끊긴 프레이즈(긴 것은 조각)로 나누고, 조각마다 음 높이와 상관없는 특징으로 시차를 찾는다:
//   음량 곡선, 음절이 시작되는 순간(트랜지언트), 유성/무성 겹침, 음 높이의 '모양'(조각 안 중앙값을 뺀 곡선, 화음이면 평행 이동이라 같다).
// 전용 상용 도구의 공개 설정 개념(최대 이동량, 정렬 강도/유연성, 트랜지언트 민감도, 음정 사용 여부)을 옵션으로 둔다.
struct PhraseTimingOptions
{
    double maxShiftSec = 0.35;      // 찾는 시차 범위 ± (최대 이동량)
    double hopSec = 0.01;
    double minGapSec = 0.2;         // 이보다 긴 무성 틈에서 프레이즈를 끊는다
    double maxPieceSec = 3.0;       // 이보다 긴 프레이즈는 조각으로 나눈다 (프레이즈 안에서도 시차가 달라질 수 있게)
    double minPieceSec = 0.25;      // 이보다 짧은 유성 덩어리는 이웃 시차를 따른다
    double levelWeight = 1.0;       // 음량 곡선
    double transientWeight = 1.5;   // 음절 시작 (트랜지언트 민감도)
    double voicingWeight = 0.5;     // 유성/무성 겹침
    double pitchWeight = 1.0;       // 음 높이 모양 (0이면 음정을 쓰지 않음)
    double minConfidence = 2.0;     // 가장 좋은 시차가 다른 시차들보다 표준편차 몇 배 뚜렷해야 쓰는지
    double jumpCostPerSec = 2.0;    // 이웃 조각과 시차가 1초 달라질 때 벌점 (클수록 정렬이 뻣뻣하다). 평가에서 0·2·6 중 2가 가장 나음
    std::function<bool()> shouldCancel;
    // 진단용(선택): 조각마다 (시작, 끝 곡 시간, 점수, 고른 시차, 쓰였는지)
    std::function<void(double, double, double, double, bool)> onPiece;
};

// 타깃 곡 시간 -> 기준 곡 시간. 확신 있는 조각이 하나도 없으면 빈 TimeMap.
TimeMap phraseTimeMap(const VocalFeatures& reference, double referenceOffsetSec, const VocalFeatures& target, double targetOffsetSec,
                      const PhraseTimingOptions& = {});
