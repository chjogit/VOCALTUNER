#include "ConsonantShield.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr double highBandHz = 4000.0, minEnergy = 1e-7;
constexpr double pi = 3.14159265358979323846;

// 4 kHz 이상 통과 (2차, 앞으로만): 영교차와 함께 보는 거친 분류용이라 위상은 상관없다
std::vector<float> highPassed(const float* x, size_t n, int sampleRate)
{
    const auto w = std::tan(pi * highBandHz / sampleRate), q = std::sqrt(0.5);
    const auto norm = 1.0 / (1.0 + w / q + w * w);
    const auto b0 = norm, b1 = -2.0 * norm, a1 = 2.0 * (w * w - 1.0) * norm, a2 = (1.0 - w / q + w * w) * norm;
    std::vector<float> y(n);
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double v = b0 * x[i] + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x[i]; y2 = y1; y1 = v; y[i] = static_cast<float>(v);
    }
    return y;
}
} // namespace

std::vector<char> fricativeFrames(const float* x, size_t length, int sampleRate, double framePeriodSec, size_t frames, size_t firstFrame)
{
    std::vector<char> flag(frames, 0);
    const auto hop = framePeriodSec * sampleRate;
    const auto from = std::min(length, static_cast<size_t>(static_cast<double>(firstFrame) * hop));
    const auto to = std::min(length, static_cast<size_t>(static_cast<double>(firstFrame + frames + 1) * hop));
    if (to <= from)
        return flag;
    const auto hp = highPassed(x + from, to - from, sampleRate);   // 구간만 필터 (앞쪽 수십 샘플은 필터가 자리잡는 시간)
    for (size_t j = 0; j < frames; ++j)
    {
        const auto i = firstFrame + j;
        const auto a = static_cast<size_t>(static_cast<double>(i) * hop);
        const auto b = std::min(length, static_cast<size_t>(static_cast<double>(i) * hop + 2.0 * hop));
        if (b < a + 8 || a < from)
            continue;
        double energy = 0.0, high = 0.0;
        size_t crossings = 0;
        for (auto k = a; k < b; ++k)
        {
            energy += static_cast<double>(x[k]) * x[k];
            high += static_cast<double>(hp[k - from]) * hp[k - from];
            crossings += k > a && ((x[k] >= 0.0f) != (x[k - 1] >= 0.0f));
        }
        energy /= static_cast<double>(b - a);
        flag[j] = energy > minEnergy && static_cast<double>(crossings) / static_cast<double>(b - a) > shieldZeroCrossingRate
                  && high / static_cast<double>(b - a) > shieldHighBandShare * energy;
    }
    return flag;
}

std::vector<double> shieldedShift(const float* x, size_t length, int sampleRate, double framePeriodSec, const std::vector<double>& shift,
                                  size_t firstFrame, size_t lastFrame)
{
    lastFrame = std::min(lastFrame, shift.size());
    std::vector<double> out(shift);
    if (firstFrame >= lastFrame)
        return out;
    const auto count = lastFrame - firstFrame;
    const auto flag = fricativeFrames(x, length, sampleRate, framePeriodSec, count, firstFrame);
    constexpr long far = 1L << 30;
    std::vector<long> nearest(count, far);              // 가장 가까운 보호 프레임까지 거리
    for (long pass = 0, since = far; pass < 2; ++pass, since = far)
        for (size_t k = 0; k < count; ++k)
        {
            const auto i = pass == 0 ? k : count - 1 - k;
            since = flag[i] ? 0 : std::min(far, since + 1);
            nearest[i] = std::min(nearest[i], since);
        }
    for (size_t i = 0; i < count; ++i)
        if (nearest[i] <= static_cast<long>(shieldRampFrames))
            out[firstFrame + i] *= static_cast<double>(nearest[i]) / static_cast<double>(shieldRampFrames + 1);
    return out;
}
