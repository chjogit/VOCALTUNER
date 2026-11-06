#pragma once
#include <cstddef>
#include <memory>
#include <vector>

// 음색(포먼트) 분석. JUCE 비의존. 포먼트 이동 합성과 측정 도구가 같이 쓴다.

// True Envelope (Röbel & Rodet): 반복 켑스트럼 평활로 하모닉 봉우리를 이은 로그 진폭 포락선.
// frame은 Hann 창을 안에서 씌우고 2의 거듭제곱 길이(nfft = 2^k >= n)로 0을 채워 FFT한다.
// 켑스트럼 차수 = 0.5 * sampleRate / f0Hz (f0Hz <= 0이면 200 Hz로 본다).
// 결과는 nfft/2 + 1개 빈의 자연로그 진폭. 빈 b의 주파수 = b * sampleRate / nfft.
size_t trueEnvelopeFftSize(size_t n) noexcept;
std::vector<double> trueEnvelope(const double* frame, size_t n, int sampleRate, double f0Hz);

// 같은 길이의 프레임을 여러 번 분석할 때 FFT 플랜을 한 번만 만든다. 플랜을 못 만들었거나 f0가 유한하지 않으면 빈 결과
class EnvelopeAnalyzer
{
public:
    explicit EnvelopeAnalyzer(size_t frameLength);
    ~EnvelopeAnalyzer();
    EnvelopeAnalyzer(const EnvelopeAnalyzer&) = delete;
    EnvelopeAnalyzer& operator=(const EnvelopeAnalyzer&) = delete;
    std::vector<double> analyze(const double* frame, int sampleRate, double f0Hz);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// LPC 근으로 찾은 포먼트 (낮은 주파수부터). count개만 유효
struct Formants
{
    double hz[4] = {};
    double bandwidth[4] = {};
    int count = 0;
};

// 약 11 kHz로 줄인 복사본(정수 배 간격 추출, 저역 통과 후)에서 분석: 0-5.5 kHz만 쓰므로 차수가 낮고 안정적이다.
// 차수 = 2 + fs'/1000 (최대 18). 프리엠퍼시스 0.97, Hamming 창, 자기상관 + Levinson-Durbin, Durand-Kerner 근.
// 허수부 > 0인 근 중 90-5000 Hz, 대역폭 < 400 Hz인 것만 남겨 앞의 4개.
Formants lpcFormants(const double* frame, size_t n, int sampleRate);
