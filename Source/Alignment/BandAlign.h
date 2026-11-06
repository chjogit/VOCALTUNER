#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include "GuidedMelody.h"
#include "TimeWarp.h"

// 분리 보컬 기준에 대한 대역 에너지 정렬. JUCE 비의존.
// 공개 자료(Bloom·Marshall 특허 US4591928, VocAlign 매뉴얼의 Max Shift·Max Difference·SmartAlign 설명)의 구조를 따라 새로 작성:
//   1. 발음 특징: 4대역(250-500, 500-1k, 1-2k, 2-4k Hz) 대역통과 -> 정류 -> 약 10 Hz 평활 -> dB, 10 ms마다.
//      음 높이와 상관없이 모음·자음의 에너지 분포를 본다 (화음이어도 같은 발음이면 같은 모양).
//   2. 트랙마다 대역별 잡음 바닥·최고 레벨로 0..1에 맞추고, 바닥보다 12 dB 넘는 대역이 2개 이상이면 말소리 프레임.
//   3. 따라가는 트랙의 말소리 덩어리마다(SmartAlign: 정렬의 시작과 끝을 따라가는 트랙에 소리가 있는 곳으로 정한다) 따로 DTW:
//      기준 프레임은 한 걸음에 0·1·2칸 (기울기 1/2..2), 말소리 안의 멈춤·건너뛰기에 벌점, 무음에서는 거의 자유. 시작·끝은 열려 있다.
//      탐색은 원래 자리 ± maxShift (Max Shift).
//   4. 이동량이 maxDifference보다 작은 곳은 옮기지 않는다 (Max Difference).
struct BandFrames
{
    double hopSec = 0.01;
    std::vector<std::array<float, 4>> db;
    size_t size() const noexcept { return db.size(); }
};

// 소스 시간 0..length를 읽어 대역 dB를 만든다 (좌우 평균). 취소되면 빈 결과
BandFrames computeBandFrames(const StereoReader&, int64_t length, int sampleRate, double hopSec = 0.01,
                             const std::function<bool()>& shouldCancel = {});

struct BandAlignOptions
{
    double maxShiftSec = 0.3;        // 원래 자리에서 옮길 수 있는 최대 시간 (Max Shift)
    double maxDifferenceSec = 0.015; // 이보다 작은 차이는 옮기지 않는다 (Max Difference)
    double speechThresholdDb = 12.0; // 잡음 바닥보다 이만큼 높으면 그 대역은 말소리
    double minGapSec = 0.2;          // 이보다 긴 무음에서 덩어리를 끊는다
    double marginSec = 0.08;         // 덩어리 앞뒤로 같이 보는 폭 (자음 시작이 문턱 아래일 때)
    double minRunSec = 0.15;         // 이보다 짧은 덩어리는 이웃을 따른다
    double repeatPenalty = 0.35;     // 말소리에서 기준이 멈추는 걸음 (따라가는 쪽을 늘림)
    double skipPenalty = 0.25;       // 말소리에서 기준이 두 칸 가는 걸음 (따라가는 쪽을 줄임)
    double silencePenalty = 0.02;    // 무음에서의 같은 걸음
    double shiftPenaltyPerSec = 0.0; // 원래 자리에서 멀어지는 데 주는 벌점. 평가(정답 있는 화음 20조각)에서 0.5면 최대 오차 222 ms, 0이면 25 ms
    double smoothSec = 0.06;
    double maxStretch = 0.15;        // 이동량 변화율 한도 = 늘이고 줄이는 비율 한도 (0.15 = ±15%). VocAlign의 Flexibility에 해당
    std::function<bool()> shouldCancel;
    std::function<void(double, double, double)> onRun;   // 진단(선택): 덩어리마다 (시작, 끝 곡 시간, 평균 이동량)
};

// 타깃 곡 시간 -> 기준 곡 시간. 정렬할 덩어리가 없으면 빈 TimeMap.
TimeMap bandAlignMap(const BandFrames& reference, double referenceOffsetSec, const BandFrames& target, double targetOffsetSec,
                     const BandAlignOptions& = {});
