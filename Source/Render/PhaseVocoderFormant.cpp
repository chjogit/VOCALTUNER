#include "PhaseVocoderInternal.h"
#include <algorithm>
#include <cmath>

namespace pvoc
{
namespace
{
constexpr double maxLogGain = 60.0 / 8.685889638;   // 60 dB (자연로그 진폭)

// 로그 포락선을 빈 위치(실수)에서 선형 보간. 범위 밖은 끝값
double sampleAt(const std::vector<double>& env, double pos) noexcept
{
    const auto last = static_cast<double>(env.size() - 1);
    const auto x = std::clamp(pos, 0.0, last);
    const auto i = static_cast<size_t>(x);
    const auto j = std::min(i + 1, env.size() - 1);
    const auto f = x - static_cast<double>(i);
    return env[i] * (1.0 - f) + env[j] * f;
}
} // namespace

std::vector<double> formantGain(const std::vector<double>& logEnv, double ratio, double formantRatio)
{
    std::vector<double> gain(logEnv.size(), 1.0);
    if (std::abs(ratio - formantRatio) < 1e-9) return gain;
    for (size_t k = 0; k < gain.size(); ++k)
    {
        const auto pos = static_cast<double>(k);
        const auto g = sampleAt(logEnv, pos / formantRatio) - sampleAt(logEnv, pos / ratio);
        gain[k] = std::exp(std::clamp(g, -maxLogGain, maxLogGain));
    }
    return gain;
}
} // namespace pvoc
