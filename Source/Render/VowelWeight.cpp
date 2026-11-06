#include "VowelWeight.h"
#include <algorithm>
#include <cmath>

namespace
{
// lo에서 0, hi에서 1인 부드러운 계단 (lo > hi 이면 거꾸로)
double smooth(double x, double lo, double hi)
{
    const auto t = std::clamp((x - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// 유성 프레임만으로 한 창 안의 중앙값 (무성 프레임은 건너뛴다)
std::vector<double> medianed(const std::vector<double>& raw, const std::vector<char>& voiced)
{
    std::vector<double> out(raw);
    for (size_t i = 0; i < raw.size(); ++i)
    {
        if (! voiced[i])
            continue;
        std::vector<double> v;
        for (auto k = i > vowelMedianRadius ? i - vowelMedianRadius : 0; k <= std::min(raw.size() - 1, i + vowelMedianRadius); ++k)
            if (voiced[k])
                v.push_back(raw[k]);
        std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
        out[i] = v[v.size() / 2];
    }
    return out;
}

// 유성 프레임만으로 한 이동 평균 (무성 프레임의 0이 모음 가장자리를 끌어내리지 않게)
std::vector<double> ramped(const std::vector<double>& x, const std::vector<char>& voiced)
{
    std::vector<double> out(x);
    for (size_t i = 0; i < x.size(); ++i)
    {
        if (! voiced[i])
            continue;
        double sum = 0.0;
        size_t count = 0;
        for (auto k = i > vowelRampRadius ? i - vowelRampRadius : 0; k <= std::min(x.size() - 1, i + vowelRampRadius); ++k)
            if (voiced[k])
            {
                sum += x[k];
                ++count;
            }
        out[i] = sum / static_cast<double>(count);
    }
    return out;
}
} // namespace

double rawVowelWeight(const VowelFeatures& f)
{
    if (! f.voiced || f.fricative)
        return 0.0;
    const auto product = smooth(f.periodicity, vowelPeriodicityLo, vowelPeriodicityHi) * smooth(f.lowShare, vowelLowShareLo, vowelLowShareHi)
                         * smooth(f.relLevelDb, vowelRelLevelLoDb, vowelRelLevelHiDb) * smooth(f.flatness, vowelFlatnessLo, vowelFlatnessHi);
    return std::pow(product, 0.25);
}

std::vector<double> vowelWeights(const float* x, size_t length, int sampleRate, double framePeriodSec, const double* midi, size_t frames,
                                 size_t firstFrame, size_t lastFrame)
{
    lastFrame = std::min(lastFrame, frames);
    if (firstFrame >= lastFrame)
        return {};
    const auto margin = std::max(vowelMedianRadius, vowelRampRadius) + vowelMedianRadius + vowelRampRadius;   // 평활 두 단계가 보는 거리
    const auto from = firstFrame > margin ? firstFrame - margin : 0, to = std::min(frames, lastFrame + margin);
    const auto feat = vowelFeatures(x, length, sampleRate, framePeriodSec, midi, frames, from, to);
    std::vector<double> raw(feat.size());
    std::vector<char> voiced(feat.size());
    for (size_t i = 0; i < feat.size(); ++i)
    {
        raw[i] = rawVowelWeight(feat[i]);
        voiced[i] = feat[i].voiced;
    }
    const auto smoothed = ramped(medianed(raw, voiced), voiced);
    return { smoothed.begin() + static_cast<long>(firstFrame - from), smoothed.begin() + static_cast<long>(lastFrame - from) };
}

void applyVowelFocus(std::vector<double>& shift, const float* x, size_t length, int sampleRate, double framePeriodSec, const double* midi,
                     size_t frames, size_t firstFrame, size_t lastFrame)
{
    lastFrame = std::min({ lastFrame, frames, shift.size() });
    const auto w = vowelWeights(x, length, sampleRate, framePeriodSec, midi, frames, firstFrame, lastFrame);
    for (size_t i = 0; i < w.size(); ++i)
        shift[firstFrame + i] *= w[i];
}
