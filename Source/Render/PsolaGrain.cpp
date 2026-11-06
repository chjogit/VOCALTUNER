#include "PsolaGrain.h"
#include <algorithm>
#include <cmath>

namespace psola
{
double hzOf(double midi) noexcept { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

size_t frameOf(const PsolaInput& in, double sample) noexcept
{
    const auto k = static_cast<size_t>(std::max(0.0, sample / in.sampleRate / in.framePeriodSec));
    return std::min(k, in.frames == 0 ? 0 : in.frames - 1);
}

double hann(long i, long n) noexcept { return n <= 1 ? 1.0 : 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(n - 1)); }


double formantRatio(double amount, double shiftSemitones) noexcept
{
    return amount >= 1.0 ? 1.0 : std::pow(2.0, (1.0 - std::max(0.0, amount)) * shiftSemitones / 12.0);
}

double formantAmountAt(const PsolaInput& in, size_t frame) noexcept
{
    return in.formantAmount == nullptr ? 1.0 : in.formantAmount[frame];
}

namespace
{
// Catmull-Rom 3차 보간 (x[i]는 i-1..i+2가 범위 안일 때만)
double cubicAt(const float* x, long i, double frac) noexcept
{
    const double p0 = x[i - 1], p1 = x[i], p2 = x[i + 1], p3 = x[i + 2];
    return p1 + 0.5 * frac * (p2 - p0 + frac * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + frac * (3.0 * (p1 - p2) + p3 - p0)));
}

// mark 둘레 2*half*ratio 길이 원본을 간격 ratio로 읽은 조각. 읽을 범위가 원본 밖이면 건너뛴다 (기존과 같은 규칙)
void addResampledGrain(const PsolaInput& in, Synth& s, long mark, long centre, long half, double ratio)
{
    const auto reach = static_cast<long>(std::ceil(static_cast<double>(half) * ratio)) + 2;
    if (mark - reach < 1 || mark + reach >= static_cast<long>(in.length) - 2)
        return;
    for (long i = 0; i < 2 * half; ++i)
    {
        const auto at = centre - half + i - static_cast<long>(s.offset);
        if (at < 0 || at >= static_cast<long>(s.out.size()))
            continue;
        const auto pos = static_cast<double>(mark) + static_cast<double>(i - half) * ratio;
        const auto base = static_cast<long>(std::floor(pos));
        const auto w = hann(i, 2 * half);
        s.out[static_cast<size_t>(at)] += cubicAt(in.samples, base, pos - static_cast<double>(base)) * w;
        s.weight[static_cast<size_t>(at)] += w;
    }
}
} // namespace

void addGrain(const PsolaInput& in, Synth& s, long mark, long centre, long half, double ratio)
{
    if (ratio != 1.0)
        return addResampledGrain(in, s, mark, centre, half, ratio);
    if (mark - half < 0 || mark + half >= static_cast<long>(in.length))
        return;
    for (long i = 0; i < 2 * half; ++i)
    {
        const auto at = centre - half + i - static_cast<long>(s.offset);
        if (at < 0 || at >= static_cast<long>(s.out.size()))
            continue;
        const auto w = hann(i, 2 * half);
        s.out[static_cast<size_t>(at)] += in.samples[mark - half + i] * w;
        s.weight[static_cast<size_t>(at)] += w;
    }
}

long nearestMark(const std::vector<long>& ms, double t) noexcept
{
    const auto it = std::lower_bound(ms.begin(), ms.end(), static_cast<long>(t));
    if (it == ms.end()) return static_cast<long>(ms.size()) - 1;
    if (it == ms.begin()) return 0;
    return (*it - t < t - *(it - 1)) ? static_cast<long>(it - ms.begin()) : static_cast<long>(it - ms.begin()) - 1;
}

// [begin, end)을 앞뒤 무성 틈(합성 구간 넓힘보다 긴)까지 넓힌다. 어느 구간을 렌더하든 마크와 합성이 같아야
// 구간을 이어 붙이는 곳(겹쳐 섞는 곳)에서 두 결과의 위상이 어긋나지 않는다
std::pair<size_t, size_t> expandedToQuiet(const PsolaInput& in, size_t beginSample, size_t endSample)
{
    const auto quiet = 2 * activeDilateFrames + 2;
    const auto limit = static_cast<size_t>(maxExpandSec / in.framePeriodSec);
    auto a = frameOf(in, static_cast<double>(beginSample)), b = frameOf(in, static_cast<double>(endSample));
    for (size_t run = 0, steps = 0; a > 0 && run < quiet && steps < limit; ++steps, --a)
        run = in.sourceMidi[a - 1] > 0.0 ? 0 : run + 1;
    for (size_t run = 0, steps = 0; b + 1 < in.frames && run < quiet && steps < limit; ++steps, ++b)
        run = in.sourceMidi[b + 1] > 0.0 ? 0 : run + 1;
    const auto toSample = [&](size_t frame) { return static_cast<size_t>(static_cast<double>(frame) * in.framePeriodSec * in.sampleRate); };
    return { std::min(beginSample, toSample(a)), std::min(in.length, std::max(endSample, toSample(b + 1))) };
}
} // namespace psola
