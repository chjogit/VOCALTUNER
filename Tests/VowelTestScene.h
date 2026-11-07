#pragma once
// 모음 가중치 시험용 합성 장면 (헤더만). VowelWeightTests와 VowelRenderTests가 같이 쓴다.
#include "../Source/Render/VowelWeight.h"
#include "VowelSynth.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace vwtest
{
constexpr int sr = 48000;
constexpr double period = 0.005, f0 = 200.0;

inline size_t samplesOf(double sec) { return static_cast<size_t>(sec * sr); }

struct Scene
{
    std::vector<float> x;
    std::vector<double> midi;
    void append(const std::vector<float>& part, bool voiced = true)
    {
        x.insert(x.end(), part.begin(), part.end());
        midi.resize(x.size() / samplesOf(period), voiced ? 69.0 + 12.0 * std::log2(f0 / 440.0) : 0.0);
    }
    std::vector<double> weights(size_t a = 0, size_t b = static_cast<size_t>(-1)) const
    {
        return vowelWeights(x.data(), x.size(), sr, period, midi.data(), midi.size(), a, b);
    }
};

inline std::vector<float> vowel(double sec, double peak = 0.3) { return vowelsynth::synth(vowelsynth::vowelA(), f0, samplesOf(sec), sr, peak); }

inline std::vector<float> whiteNoise(double sec, double peak)
{
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    std::vector<float> x(samplesOf(sec));
    for (auto& v : x) v = static_cast<float>(peak) * d(rng);
    return x;
}

// Paul Kellet 근사 핑크 잡음
inline std::vector<float> pinkNoise(double sec, double peak)
{
    auto w = whiteNoise(sec, 1.0);
    double b0 = 0, b1 = 0, b2 = 0;
    for (auto& v : w)
    {
        b0 = 0.99765 * b0 + v * 0.0990460; b1 = 0.96300 * b1 + v * 0.2965164; b2 = 0.57000 * b2 + v * 1.0526913;
        v = static_cast<float>(peak * 0.25 * (b0 + b1 + b2 + v * 0.1848));
    }
    return w;
}

// 치찰음처럼 고역 위주 잡음 (부호 교대)
inline std::vector<float> hiss(double sec, double peak)
{
    auto w = whiteNoise(sec, peak);
    for (size_t i = 0; i < w.size(); i += 2) w[i] = -w[i];
    return w;
}

// 비음처럼 낮고 조용한 유성: 저역 통과 펄스열, 약한 포먼트, 모음보다 RMS 25 dB 낮다
inline std::vector<float> nasal(double sec)
{
    std::vector<double> x(samplesOf(sec), 0.0);
    for (double t = 0.0; t < static_cast<double>(x.size()); t += sr / f0) x[static_cast<size_t>(std::lround(t))] = 1.0;
    for (int i = 0; i < 4; ++i) vowelsynth::onePole(x, 0.97);
    vowelsynth::resonate(x, 250.0, 200.0, sr);
    const auto ref = vowel(0.5);   // RMS를 모음에 맞춘 뒤 25 dB 낮춘다
    double eRef = 0.0, eNasal = 1e-24;
    for (const auto v : ref) eRef += static_cast<double>(v) * v;
    for (const auto v : x) eNasal += v * v;
    const auto gain = std::sqrt(eRef / static_cast<double>(ref.size()) / (eNasal / static_cast<double>(x.size()))) * std::pow(10.0, -25.0 / 20.0);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float>(gain * x[i]);
    return y;
}

inline double lowestIn(const std::vector<double>& w, double a, double b) { return *std::min_element(w.begin() + static_cast<long>(a / period), w.begin() + static_cast<long>(b / period)); }
inline double highestIn(const std::vector<double>& w, double a, double b) { return *std::max_element(w.begin() + static_cast<long>(a / period), w.begin() + static_cast<long>(b / period)); }

} // namespace vwtest
