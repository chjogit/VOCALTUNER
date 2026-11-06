#pragma once
// 실제 목소리 비교용 측정 함수 (출력 오디오만 본다, 내부 목표값은 쓰지 않는다). JUCE 비의존, 도구 전용.
#include "../Source/Analysis/SpectralEnvelope.h"
#include "../Source/Analysis/VocalFeatures.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace voicemetrics
{
constexpr double framePeriod = 0.005;

inline double median(std::vector<double> v)
{
    if (v.empty()) return std::nan("");
    std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

inline std::vector<double> harvestMidi(const std::vector<float>& x, int sr, double floorHz)
{
    FeatureOptions opt;
    opt.f0FloorHz = floorHz;
    return extractVocalFeatures(std::vector<double>(x.begin(), x.end()), sr, opt)->midiF0;
}

inline double midiHz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

struct F0Stat { double medCents = std::nan(""), within20 = 0.0, kept = 0.0; };

// 원본 x 2^(이동/12) 대비, 둘 다 유성인 프레임의 오차(cent), 20 cent 이내 비율, 원본 유성 프레임 중 출력도 유성인 비율
inline F0Stat f0Stats(const std::vector<double>& src, const std::vector<double>& out, double shift)
{
    std::vector<double> err;
    size_t voiced = 0, good = 0;
    for (size_t i = 0; i < src.size() && i < out.size(); ++i)
    {
        if (src[i] <= 0.0) continue;
        ++voiced;
        if (out[i] <= 0.0) continue;
        err.push_back(std::abs(out[i] - src[i] - shift) * 100.0);
        good += err.back() <= 20.0;
    }
    F0Stat s;
    s.medCents = median(err);
    s.within20 = err.empty() ? 0.0 : 100.0 * static_cast<double>(good) / static_cast<double>(err.size());
    s.kept = voiced ? 100.0 * static_cast<double>(err.size()) / static_cast<double>(voiced) : 0.0;
    return s;
}

inline bool frameFormants(const std::vector<float>& x, size_t center, int sr, Formants& fm)
{
    constexpr size_t n = 2048;
    if (center < n / 2 || center + n / 2 > x.size()) return false;
    const std::vector<double> w(x.begin() + static_cast<long>(center - n / 2), x.begin() + static_cast<long>(center + n / 2));
    fm = lpcFormants(w.data(), n, sr);
    return fm.count >= 2;
}

struct FormantStat { double f1 = std::nan(""), f2 = std::nan(""), measurable = 0.0; };

inline bool measurablePair(const Formants& a, const Formants& b, double hzA, double hzB)
{
    return a.hz[0] >= 2.0 * hzA && b.hz[0] >= 2.0 * hzB;   // F1 < 2 f0 에서는 포먼트가 하모닉에 끌린다
}

// 둘 다 유성인 프레임 (20 ms 간격)에서 출력 포먼트 / (원본 포먼트 x 2^((1-양) 이동/12)) 오차 %의 중앙값
inline FormantStat formantStats(const std::vector<float>& src, const std::vector<float>& out, const std::vector<double>& ms,
                                const std::vector<double>& mo, double shift, double amount, int sr)
{
    std::vector<double> e1, e2;
    size_t tried = 0;
    const auto scale = std::pow(2.0, (1.0 - amount) * shift / 12.0);
    for (size_t i = 0; i < ms.size() && i < mo.size(); i += 4)
    {
        if (ms[i] <= 0.0 || mo[i] <= 0.0) continue;
        ++tried;
        Formants a, b;
        const auto c = static_cast<size_t>(static_cast<double>(i) * framePeriod * sr);
        if (! frameFormants(src, c, sr, a) || ! frameFormants(out, c, sr, b) || ! measurablePair(a, b, midiHz(ms[i]), midiHz(mo[i]))) continue;
        e1.push_back(std::abs(b.hz[0] / (a.hz[0] * scale) - 1.0) * 100.0);
        e2.push_back(std::abs(b.hz[1] / (a.hz[1] * scale) - 1.0) * 100.0);
    }
    return { median(e1), median(e2), tried ? 100.0 * static_cast<double>(e1.size()) / static_cast<double>(tried) : 0.0 };
}
} // namespace voicemetrics
