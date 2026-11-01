#pragma once
#include <complex>
#include <cstddef>
#include <vector>

// 반주 섞인 스테레오 음원에서 가운데(메인 보컬) 성분의 배음 봉우리를 보는 스펙트럼. JUCE 비의존.
// 원곡 멜로디 추출(GuidedMelody)과 원곡 박자 찾기(GuidedTiming)가 같이 쓴다.
class MixSpectrum
{
public:
    explicit MixSpectrum(size_t frameSize);      // 2의 거듭제곱
    size_t size() const noexcept { return window.size(); }
    // left/right에서 frameSize 샘플: 좌우가 같은 성분만 남기고(압축·봉우리만) 크기 스펙트럼 (frameSize/2 빈)
    std::vector<double> centre(const float* left, const float* right);

private:
    std::vector<double> window;
    std::vector<std::complex<double>> work;
};

double hzOf(double midi) noexcept;
// 소수 빈 위치의 값 (선형 보간). 10센트 단위 후보를 구분하려면 반올림한 빈으로는 부족하다
double valueAt(const std::vector<double>& spec, double bin) noexcept;
// hz의 배음(5 kHz 아래)을 0.8배씩 줄여 가며 더한 점수
double harmonicSalience(const std::vector<double>& spec, double hz, double binHz, int harmonics) noexcept;
