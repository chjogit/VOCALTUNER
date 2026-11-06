#pragma once
#include <cstddef>
#include <vector>

// 모음 중심 이동: 프레임마다 "모음다움" w (0..1)를 원본 오디오와 원본 F0만으로 구한다.
// 숨소리·비음·유성 파열음 시작·모음 앞뒤 전이처럼 모음이 아닌 유성 프레임은 w가 낮아 음정 이동이 줄어든다.
// 특징마다 (lo, hi)를 부드럽게 0..1로 바꾸고 (smoothstep), 기하평균으로 합친다. 하나라도 0이면 w도 0.
// w의 프레임당 변화 <= 0.15는 유성 구간 안에서만 보장한다: 유성 -> 무성 경계에서는 무성 프레임이 0이라 끊기지만,
// 무성 프레임은 원래 이동하지 않으므로 현재 동작과 같다. 비유한 값이나 null 입력은 w = 0.
constexpr double vowelPeriodicityLo = 0.55, vowelPeriodicityHi = 0.85;   // F0 주기 정규화 자기상관
constexpr double vowelLowShareLo = 0.10, vowelLowShareHi = 0.30;         // 저역 에너지 비중
constexpr double vowelLowBandMinHz = 1000.0, vowelLowBandHarmonics = 5.0; // 저역 = max(1 kHz, F0의 5배 이하): 높은 음은 1 kHz 아래에 배음이 적다
constexpr double vowelRelLevelLoDb = -24.0, vowelRelLevelHiDb = -12.0;   // 주변 유성 최대 레벨 대비 dB
constexpr double vowelFlatnessHi = 0.04, vowelFlatnessLo = 0.20;         // 스펙트럼 평탄도 (낮을수록 모음; 높으면 숨소리)
constexpr double vowelFlatBandLowHz = 100.0, vowelFlatBandHighHz = 6000.0;
constexpr double vowelCoreLo = 0.3, vowelCoreHi = 0.7;                   // 평활한 w를 이 구간에서 smoothstep: 모음 가운데는 정확히 1 (전체 이동), 낮은 곳은 0
// 반경은 초 단위 (5 ms 프레임이면 30 / 1 / 3 프레임). 프레임 수는 vowelRadiusFrames로 바꾼다
constexpr double vowelLevelRadiusSec = 0.15;                             // 레벨 기준 이웃 +-150 ms
constexpr double vowelMedianRadiusSec = 0.005;                           // 중앙값 3프레임 (15 ms)
constexpr double vowelRampRadiusSec = 0.015;                             // 이동 평균 7프레임: 유성 구간 안에서 w가 약 35 ms에 걸쳐 변한다 (프레임당 <= 0.15)
inline size_t vowelRadiusFrames(double sec, double framePeriodSec) { return static_cast<size_t>(sec / framePeriodSec + 0.5); }

struct VowelFeatures
{
    bool voiced = false, fricative = false;
    double periodicity = 0.0, lowShare = 0.0, relLevelDb = -120.0, flatness = 1.0;
};

// [firstFrame, lastFrame) 프레임의 특징 (결과[i] = 프레임 firstFrame + i). 프레임은 어느 구간을 요청해도 같은 값이다
std::vector<VowelFeatures> vowelFeatures(const float* samples, size_t length, int sampleRate, double framePeriodSec,
                                         const double* sourceMidi, size_t frames, size_t firstFrame, size_t lastFrame);

// 특징 하나씩의 부드러운 점수를 합친 날 가중치 (평활 전). 무성이거나 마찰음이면 0
double rawVowelWeight(const VowelFeatures&);

// 프레임별 w (결과[i] = 프레임 firstFrame + i). 중앙값 -> 코어 smoothstep -> 이동 평균 (유성 프레임끼리만). 이웃이 모두 높은 모음 가운데는 정확히 1이다.
// 앞뒤 +-(레벨 이웃 + 평활 반경) 프레임까지 보므로 구간 렌더와 전체 렌더의 값이 같다. sourceMidi == 0 이면 0
std::vector<double> vowelWeights(const float* samples, size_t length, int sampleRate, double framePeriodSec,
                                 const double* sourceMidi, size_t frames, size_t firstFrame, size_t lastFrame);

// shift[firstFrame, lastFrame) 에 w를 곱한다 (vowelFocus가 켜졌을 때 렌더러가 쓴다)
void applyVowelFocus(std::vector<double>& shift, const float* samples, size_t length, int sampleRate, double framePeriodSec,
                     const double* sourceMidi, size_t frames, size_t firstFrame, size_t lastFrame);
