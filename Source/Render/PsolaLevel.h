#pragma once
#include <vector>

// PSOLA 음량 보정 (옵트인, 기본 꺼짐). JUCE 비의존.
// 합성한 조각의 가중 평균은 이웃 조각의 저역이 서로 어긋나 에너지를 잃는다 (기본 경로에서 +7 반음 약 -4 dB).
// 그래서 합성된 구간에서 원본과 출력의 RMS를 20 ms 창(5 ms 간격)으로 재서 gain = 원본 RMS / 출력 RMS (+-6 dB 제한)를 곱한다.
// - 원본이 거의 무음(RMS < 1e-4)이거나 합성 비중(weight)이 낮은 창은 gain 1.
// - 창 사이는 dB로 선형 보간하고, 인접 창의 gain 차는 0.5 dB 안으로 제한한다 (계단 없음).
// - weight가 0인 표본(원본 그대로인 곳)에는 손대지 않는다. 경계는 weight만큼만 gain을 적용해 이어진다.
namespace psola
{
constexpr double levelWindowSec = 0.02, levelHopSec = 0.005;
constexpr double levelMaxDb = 6.0, levelSlewDb = 0.5, levelGateRms = 1e-4;

// 창 중심(절대 표본 번호가 hop의 배수인 곳)마다의 gain (dB). y, ref, weight는 같은 길이 (ref = 같은 시각의 원본).
// origin = y[0]의 절대 표본 번호. 격자가 절대 표본에 묶이므로 어느 구간을 렌더해도 같은 표본은 같은 gain을 받는다.
// 첫 창 중심의 y 안 위치는 levelGridOffset(origin, sampleRate)
std::vector<double> levelGainsDb(const std::vector<float>& y, const std::vector<float>& ref, const std::vector<double>& weight, int sampleRate,
                                 size_t origin = 0);
size_t levelGridOffset(size_t origin, int sampleRate);

// y에 levelGainsDb를 보간해 곱한다. weight[i] == 0이면 y[i]는 그대로.
// 표본마다 |y|*gain이 그 표본 둘레 20 ms 안 원본 최대 |ref|를 넘지 않게 gain을 제한한다 (클립 방지, 원래 |y|가 넘는 표본은 gain 1)
void applyLevelMatch(std::vector<float>& y, const std::vector<float>& ref, const std::vector<double>& weight, int sampleRate, size_t origin = 0);

// 마스크(0/1)를 가장자리 10 ms로 눌러 만든 weight (마스크가 0인 곳은 항상 0)
std::vector<double> maskWeight(const std::vector<double>& mask, int sampleRate);
} // namespace psola
