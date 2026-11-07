#pragma once
// 고역(4-8 kHz) 측정 (도구 전용). voice_compare와 vowel_focus가 같이 쓴다.
#include "VoiceMetrics.h"
#include <world/fft.h>

namespace voicemetrics
{
constexpr double hfPi = 3.14159265358979323846;

// 2048 창 1024 간격 프레임마다 (4-8 kHz 파워, 전체 파워)
inline std::vector<std::pair<double, double>> framePowers(const std::vector<float>& y, int sr)
{
    std::vector<double> in(2048), out(2050);
    auto plan = fft_plan_dft_r2c_1d(2048, in.data(), reinterpret_cast<fft_complex*>(out.data()), FFT_ESTIMATE);
    std::vector<std::pair<double, double>> frames;
    for (size_t s = 0; s + 2048 < y.size(); s += 1024)
    {
        for (size_t i = 0; i < 2048; ++i) in[i] = y[s + i] * (0.5 - 0.5 * std::cos(2.0 * hfPi * static_cast<double>(i) / 2048.0));
        fft_execute(plan);
        double high = 0.0, total = 0.0;
        for (size_t k = 0; k <= 1024; ++k)
        {
            const auto p = out[2 * k] * out[2 * k] + out[2 * k + 1] * out[2 * k + 1];
            total += p;
            if (k >= static_cast<size_t>(4000 * 2048 / sr) && k <= static_cast<size_t>(8000 * 2048 / sr)) high += p;
        }
        frames.emplace_back(high, total);
    }
    fft_destroy_plan(plan);
    return frames;
}


// 새 고역 스파이크: 원본이 유성인 프레임마다 (출력 4-8 kHz 비중 - 원본 비중)의 95 백분위수 (dB). 이득만 바뀐 것은 비중에 영향이 없다
inline double hfSpikeDb(const std::vector<float>& src, const std::vector<float>& y, const std::vector<double>& midi, int sr)
{
    const auto a = framePowers(src, sr), b = framePowers(y, sr);
    std::vector<double> d;
    for (size_t f = 0; f < a.size() && f < b.size(); ++f)
        if (a[f].second > 1e-6 && midi[std::min(midi.size() - 1, (f * 1024 + 1024) / static_cast<size_t>(framePeriod * sr))] > 0.0)
            d.push_back(10.0 * std::log10((b[f].first + 1e-12) / (b[f].second + 1e-12) * (a[f].second + 1e-12) / (a[f].first + 1e-12)));
    return percentile95(d);
}
} // namespace voicemetrics
