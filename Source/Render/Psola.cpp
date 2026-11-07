#include "Psola.h"
#include "PsolaGrain.h"
#include "PsolaLevel.h"
#include <algorithm>
#include <cmath>

using namespace psola;

namespace
{
constexpr double activeShift = 0.01;       // 이보다 작은 이동은 옮기지 않는 것으로 본다
constexpr double mixFadeSec = 0.02, movingFadeSec = 0.01;

double periodAt(const PsolaInput& in, double sample, double fallbackHz) noexcept
{
    const auto midi = in.sourceMidi[frameOf(in, sample)];
    return in.sampleRate / (midi > 0.0 ? hzOf(midi) : fallbackHz);
}

// 2차 저역 통과(1 kHz)를 앞뒤로 (위상 그대로). 첫 마크를 성문 펄스 근처에 놓는 데만 쓴다
std::vector<double> lowPassed(const float* x, size_t n, int sampleRate)
{
    const auto w = std::tan(pi * 1000.0 / sampleRate), q = std::sqrt(0.5);
    const auto norm = 1.0 / (1.0 + w / q + w * w);
    const auto b0 = w * w * norm, b1 = 2.0 * b0, a1 = 2.0 * (w * w - 1.0) * norm, a2 = (1.0 - w / q + w * w) * norm;
    std::vector<double> y(x, x + n);
    for (int pass = 0; pass < 2; ++pass)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (size_t k = 0; k < n; ++k)
        {
            const auto i = pass == 0 ? k : n - 1 - k;
            const auto v = b0 * y[i] + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = y[i]; y2 = y1; y1 = v; y[i] = v;
        }
    }
    return y;
}

// prev 둘레 한 주기와 가장 닮은 자리를 guess ± 0.15주기에서 찾는다 (마크가 같은 위상에 놓이도록)
long bestMatch(const float* x, size_t n, long prev, long guess, double period)
{
    const auto h = static_cast<long>(period / 2.0), r = std::max(1L, static_cast<long>(0.15 * period));
    if (prev - h < 0 || guess + r + h >= static_cast<long>(n))
        return guess;
    double best = -2.0;
    auto at = guess;
    for (auto d = -r; d <= r; ++d)
    {
        double ab = 0, aa = 0, bb = 0;
        for (long i = -h; i < h; ++i)
        {
            const double a = x[prev + i], b = x[guess + d + i];
            ab += a * b; aa += a * a; bb += b * b;
        }
        const auto c = ab / (std::sqrt(aa * bb) + 1e-12);
        if (c > best) { best = c; at = guess + d; }
    }
    return at;
}

// 유성 구간 하나 [a, b) (샘플)에 마크를 놓는다
// fallbackHz: 구간 첫 프레임의 음정 (샘플→프레임 반올림으로 앞 무성 프레임을 읽지 않게 직접 받는다)
void marksInRun(const PsolaInput& in, const std::vector<double>& lp, size_t offset, long a, long b, double fallback, std::vector<long>& marks)
{
    double t = a;
    bool first = true;
    while (t < b)
    {
        const auto period = periodAt(in, t, fallback);
        long p = static_cast<long>(t);
        if (first)
        {
            const auto lo = std::max<long>(static_cast<long>(offset), static_cast<long>(t - period / 2));
            const auto hi = std::min<long>(static_cast<long>(offset + lp.size()), static_cast<long>(t + period / 2));
            for (auto i = lo; i < hi; ++i)
                if (lp[static_cast<size_t>(i) - offset] > lp[static_cast<size_t>(p) - offset]) p = i;
            first = false;
        }
        else
            p = bestMatch(in.samples, in.length, marks.back(), static_cast<long>(t), period);
        if (marks.empty() || p > marks.back() + period * 0.5)
            marks.push_back(p);
        t = static_cast<double>(p) + period;
    }
}

// 한 구간의 마크들로 새 간격의 조각을 겹쳐 놓는다
void synthesizeRun(const PsolaInput& in, const std::vector<long>& ms, Synth& s)
{
    double t = static_cast<double>(ms.front());
    while (t < static_cast<double>(ms.back()))
    {
        const auto k = static_cast<size_t>(nearestMark(ms, t));
        const auto period = std::max(20.0, 0.5 * static_cast<double>(ms[std::min(k + 1, ms.size() - 1)] - ms[k > 0 ? k - 1 : 0]));
        const auto frame = frameOf(in, t);
        const auto shift = in.shiftSemitones[frame];
        const auto ratio = formantRatio(formantAmountAt(in, frame), shift);
        addGrain(in, s, ms[k], static_cast<long>(std::lround(t)), grainHalf(static_cast<long>(period), shift, ratio), ratio);
        auto next = t + period / std::pow(2.0, shift / 12.0);
        if (const auto lock = 1.0 - std::min(1.0, std::abs(shift) / lockBelow); lock > 0.0)
            next += lock * 0.5 * (static_cast<double>(ms[static_cast<size_t>(nearestMark(ms, next))]) - next);
        t = std::max(next, t + period * 0.5);
    }
}

// 프레임 범위 [a, b)가 합성 대상(이동 있음 + 유성)인지, 앞뒤로 넓혀서
std::vector<std::pair<size_t, size_t>> activeRuns(const PsolaInput& in, size_t f0, size_t f1)
{
    std::vector<char> act(f1 - f0, 0);
    for (auto i = f0; i < f1; ++i)
        if (std::abs(in.shiftSemitones[i]) > activeShift && in.sourceMidi[i] > 0.0)
            for (auto j = (i > activeDilateFrames ? i - activeDilateFrames : 0); j <= std::min(f1 - 1, i + activeDilateFrames); ++j)
                if (j >= f0) act[j - f0] = 1;
    std::vector<std::pair<size_t, size_t>> runs;
    for (size_t i = 0; i < act.size();)
    {
        if (! act[i]) { ++i; continue; }
        auto j = i;
        while (j < act.size() && act[j]) ++j;
        runs.emplace_back(f0 + i, f0 + j);
        i = j;
    }
    return runs;
}

std::vector<double> boxSmoothed(const std::vector<double>& x, size_t width)
{
    std::vector<double> y(x.size(), 0.0);
    if (width < 2)
        return x;
    double sum = 0.0;
    const auto half = width / 2;
    for (size_t i = 0; i < x.size() + half; ++i)
    {
        if (i < x.size()) sum += x[i];
        if (i >= width) sum -= x[i - width];
        if (i >= half) y[i - half] = sum / static_cast<double>(width);
    }
    return y;
}

// 합성 소리를 쓰는 비율: 합성한 구간 안쪽으로 20ms 페이드, 그리고 이동이 있는 곳(+10ms)에서만
std::vector<double> mixWeight(const PsolaInput& in, const std::vector<double>& mask, size_t offset)
{
    const auto fade = static_cast<size_t>(mixFadeSec * in.sampleRate);
    auto m = boxSmoothed(mask, fade);
    for (size_t i = 0; i < m.size(); ++i) m[i] = std::min(m[i], mask[i]);
    m = boxSmoothed(m, fade);
    std::vector<double> moving(mask.size());
    for (size_t i = 0; i < moving.size(); ++i)
        moving[i] = std::abs(in.shiftSemitones[frameOf(in, static_cast<double>(offset + i))]) > 0.005 ? 1.0 : 0.0;
    const auto k = static_cast<size_t>(movingFadeSec * in.sampleRate);
    moving = boxSmoothed(moving, 2 * k);
    for (size_t i = 0; i < m.size(); ++i)
        m[i] *= mask[i] * std::min(1.0, 2.0 * moving[i]);
    return m;
}
} // namespace

std::vector<long> pitchMarks(const PsolaInput& in, size_t beginSample, size_t endSample)
{
    std::vector<long> marks;
    endSample = std::min(endSample, in.length);
    if (beginSample >= endSample || in.frames == 0)
        return marks;
    const auto lp = lowPassed(in.samples + beginSample, endSample - beginSample, in.sampleRate);
    const auto f0 = frameOf(in, static_cast<double>(beginSample)), f1 = frameOf(in, static_cast<double>(endSample)) + 1;
    for (auto i = f0; i < f1;)
    {
        if (in.sourceMidi[i] <= 0.0) { ++i; continue; }
        auto j = i;
        while (j < f1 && in.sourceMidi[j] > 0.0) ++j;
        const auto toSample = [&](size_t frame) { return static_cast<long>(static_cast<double>(frame) * in.framePeriodSec * in.sampleRate); };
        const auto a = std::max(toSample(i), static_cast<long>(beginSample)), b = std::min(toSample(j), static_cast<long>(endSample));
        if (b > a)
            marksInRun(in, lp, beginSample, a, b, hzOf(in.sourceMidi[i]), marks);
        i = j;
    }
    return marks;
}

std::vector<float> psolaShift(const PsolaInput& in, size_t beginSample, size_t endSample)
{
    endSample = std::min(endSample, in.length);
    if (beginSample >= endSample || in.frames == 0)
        return std::vector<float>(in.samples + beginSample, in.samples + std::max(beginSample, endSample));
    if (const auto [a, b] = expandedToQuiet(in, beginSample, endSample); a < beginSample || b > endSample)
    {
        const auto wide = psolaShiftExact(in, a, b);
        return std::vector<float>(wide.begin() + static_cast<long>(beginSample - a), wide.begin() + static_cast<long>(endSample - a));
    }
    return psolaShiftExact(in, beginSample, endSample);
}

namespace
{
// 유성 구간마다 합성하고, 합성한 곳을 mask에 1로 표시 (mask[0]은 표본 beginSample)
void synthesizeRuns(const PsolaInput& in, size_t beginSample, size_t endSample, Synth& s, std::vector<double>& mask)
{
    const auto marks = pitchMarks(in, beginSample, endSample);
    const auto toSample = [&](size_t frame) { return static_cast<long>(static_cast<double>(frame) * in.framePeriodSec * in.sampleRate); };
    for (const auto& [fa, fb] : activeRuns(in, frameOf(in, static_cast<double>(beginSample)), frameOf(in, static_cast<double>(endSample)) + 1))
    {
        std::vector<long> ms;
        for (const auto m : marks)
            if (m >= toSample(fa) && m < toSample(fb)) ms.push_back(m);
        if (ms.size() < 3)
            continue;
        synthesizeRun(in, ms, s);
        for (auto i = ms.front(); i < ms.back(); ++i)
            mask[static_cast<size_t>(i) - beginSample] = 1.0;
    }
}

// 합성을 원본에 섞는다. 반환값은 실제로 섞인 표본의 weight (그 밖은 0): 음량 보정도 같은 조건을 쓴다
std::vector<double> mixSynthesis(std::vector<float>& y, const Synth& s, std::vector<double> w)
{
    for (size_t i = 0; i < y.size(); ++i)
    {
        const auto mixed = w[i] > 0.0 && s.weight[i] > 1e-3;
        w[i] = mixed ? w[i] : 0.0;
        if (mixed)
            y[i] = static_cast<float>((1.0 - w[i]) * y[i] + w[i] * s.out[i] / s.weight[i]);
    }
    return w;
}
} // namespace

std::vector<float> psolaShiftExact(const PsolaInput& in, size_t beginSample, size_t endSample)
{
    endSample = std::min(endSample, in.length);
    std::vector<float> y(in.samples + beginSample, in.samples + std::max(beginSample, endSample));
    if (beginSample >= endSample || in.frames == 0)
        return y;
    Synth s;
    s.offset = beginSample;
    s.out.assign(y.size(), 0.0);
    s.weight.assign(y.size(), 0.0);
    std::vector<double> mask(y.size(), 0.0);
    synthesizeRuns(in, beginSample, endSample, s, mask);
    const auto w = mixSynthesis(y, s, mixWeight(in, mask, beginSample));
    if (in.levelMatch)
        applyLevelMatch(y, std::vector<float>(in.samples + beginSample, in.samples + endSample), w, in.sampleRate, beginSample);
    return y;
}

