#pragma once
#include <cmath>
#include <cstddef>
#include <vector>
#include "../Source/Analysis/SpectralEnvelope.h"

// 시험/측정용 합성 모음: 성문 펄스열(-12 dB/oct 기울기)을 포먼트 공진기 4개에 통과. 헤더만으로 쓴다.
namespace vowelsynth
{
struct Vowel { double hz[4]; double bw[4]; };

inline Vowel vowelA() { return { { 730, 1090, 2440, 3400 }, { 80, 90, 120, 120 } }; }
inline Vowel vowelI() { return { { 270, 2290, 3010, 3500 }, { 60, 90, 120, 120 } }; }

// 2극 공진기 (중심 hz, 대역폭 bw). 직류 이득 1
inline void resonate(std::vector<double>& x, double hz, double bw, int sr)
{
    const auto r = std::exp(-3.14159265358979 * bw / sr), th = 2.0 * 3.14159265358979 * hz / sr;
    const auto a1 = 2.0 * r * std::cos(th), a2 = -r * r, b0 = 1.0 - a1 - a2;
    double y1 = 0.0, y2 = 0.0;
    for (auto& v : x)
    {
        const auto y = b0 * v + a1 * y1 + a2 * y2;
        y2 = y1; y1 = y; v = y;
    }
}

inline void onePole(std::vector<double>& x, double p)
{
    double y = 0.0;
    for (auto& v : x) { y = (1.0 - p) * v + p * y; v = y; }
}

// length 샘플, 기본 주파수 f0, 최대 진폭 peak
inline std::vector<float> synth(const Vowel& v, double f0, size_t length, int sr, double peak = 0.3)
{
    std::vector<double> x(length, 0.0);
    for (double t = 0.0; t < static_cast<double>(length); t += sr / f0)
        x[static_cast<size_t>(std::lround(t))] = 1.0;
    onePole(x, 0.97);
    onePole(x, 0.97);
    for (int i = 0; i < 4; ++i) resonate(x, v.hz[i], v.bw[i], sr);
    double top = 1e-12;
    for (const auto s : x) top = std::max(top, std::abs(s));
    std::vector<float> y(length);
    for (size_t i = 0; i < length; ++i) y[i] = static_cast<float>(peak * x[i] / top);
    return y;
}

// x[begin, begin+n) 구간의 LPC 포먼트
inline Formants measure(const std::vector<float>& x, size_t begin, size_t n, int sr)
{
    std::vector<double> frame(x.begin() + static_cast<long>(begin), x.begin() + static_cast<long>(begin + n));
    return lpcFormants(frame.data(), frame.size(), sr);
}
} // namespace vowelsynth
