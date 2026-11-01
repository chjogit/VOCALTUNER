#include "PsolaWarp.h"
#include "PsolaGrain.h"
#include <algorithm>
#include <cmath>

using namespace psola;

namespace
{
constexpr double unvoicedHopSec = 0.005;        // 무성 조각 간격 (조각 길이는 이것의 2배, 50% 겹침 = 합이 1)
constexpr double slopeProbeSec = 0.001;
constexpr double slopeLockBelow = 0.004;         // 원본 흐름 속도가 1에서 이만큼 안이면 박자가 그대로인 것으로 본다
constexpr double marginSec = 0.05;
constexpr double maxGapHz = 50.0;                // 마크 간격이 이 주파수의 한 주기보다 길면 같은 유성 구간이 아니다

PsolaInput sourceView(const WarpInput& in)
{
    return { in.samples, in.length, in.sampleRate, in.framePeriodSec, in.sourceMidi, nullptr, in.sourceFrames };
}

double sourceSample(const WarpInput& in, double outSample) { return in.toSource(outSample / in.sampleRate) * in.sampleRate; }

// 무성 조각 간격 (샘플). 구간 시작 정렬과 조각 이동이 같은 값을 써야 어느 구간을 렌더해도 같은 격자가 된다
long unvoicedHop(const WarpInput& in) { return std::max(1L, std::lround(unvoicedHopSec * in.sampleRate)); }

bool voicedAtOutput(const WarpInput& in, double outSample)
{
    if (in.sourceFrames == 0)
        return false;
    return in.sourceMidi[frameOf(sourceView(in), std::max(0.0, sourceSample(in, outSample)))] > 0.0;
}

double shiftAtOutput(const WarpInput& in, double outSample)
{
    if (in.outFrames == 0)
        return 0.0;
    const auto k = static_cast<size_t>(std::max(0.0, outSample / in.sampleRate / in.framePeriodSec));
    return in.shiftSemitones[std::min(k, in.outFrames - 1)];
}

// 출력 구간을 앞뒤 무성 틈까지 넓힌다 (출력 시간 기준). 시작은 무성 조각 간격의 배수에 맞춘다
std::pair<size_t, size_t> outputExpandedToQuiet(const WarpInput& in, size_t begin, size_t end)
{
    const auto quietSamples = static_cast<size_t>((2 * activeDilateFrames + 2) * in.framePeriodSec * in.sampleRate);
    const auto step = static_cast<size_t>(in.framePeriodSec * in.sampleRate);
    const auto limit = static_cast<size_t>(maxExpandSec * in.sampleRate);
    auto a = begin, b = end;
    for (size_t run = 0, walked = 0; a > step && run < quietSamples && walked < limit; a -= step, walked += step)
        run = voicedAtOutput(in, static_cast<double>(a - step)) ? 0 : run + step;
    for (size_t run = 0, walked = 0; run < quietSamples && walked < limit; b += step, walked += step)
        run = voicedAtOutput(in, static_cast<double>(b)) ? 0 : run + step;
    const auto hop = static_cast<size_t>(unvoicedHop(in));
    return { a - a % hop, b };
}

struct Warper
{
    const WarpInput& in;
    PsolaInput source;
    std::vector<long> marks;
    Synth synth;

    // 원본 마크 k에서 한 주기: 앞뒤 마크 간격의 평균. 이웃이 다른 유성 구간(긴 무성 건너)의 마크면 쓰지 않는다
    long periodOf(size_t k) const
    {
        const auto limit = static_cast<long>(in.sampleRate / maxGapHz);
        long sum = 0, count = 0;
        if (k > 0 && marks[k] - marks[k - 1] <= limit) { sum += marks[k] - marks[k - 1]; ++count; }
        if (k + 1 < marks.size() && marks[k + 1] - marks[k] <= limit) { sum += marks[k + 1] - marks[k]; ++count; }
        if (count == 0)
            return std::max(20L, std::lround(in.sampleRate / hzOf(std::max(1.0, source.sourceMidi[frameOf(source, static_cast<double>(marks[k]))]))));
        return std::max(20L, sum / count);
    }

    double slopeAt(double outSample) const
    {
        const auto probe = slopeProbeSec * in.sampleRate;
        return (sourceSample(in, outSample + probe) - sourceSample(in, outSample)) / probe;
    }

    // 다음 조각 자리: 박자 그대로 + 이동 0이면 원본 마크가 놓일 출력 자리로 끌어당긴다 (원본과 위상이 맞게)
    double lockedNext(double next, double shift, double slope) const
    {
        const auto lock = (1.0 - std::min(1.0, std::abs(shift) / lockBelow)) * (1.0 - std::min(1.0, std::abs(slope - 1.0) / slopeLockBelow));
        if (lock <= 0.0 || marks.empty())
            return next;
        const auto k = static_cast<size_t>(nearestMark(marks, sourceSample(in, next)));
        const auto outPos = in.toDisplay(static_cast<double>(marks[k]) / in.sampleRate) * in.sampleRate;
        return next + lock * 0.5 * (outPos - next);
    }

    void voicedGrain(double& t)
    {
        const auto k = static_cast<size_t>(nearestMark(marks, sourceSample(in, t)));
        const auto period = periodOf(k);
        addGrain(source, synth, marks[k], std::lround(t), period);
        const auto shift = shiftAtOutput(in, t);
        const auto next = t + static_cast<double>(period) / std::pow(2.0, shift / 12.0);
        t = std::max(lockedNext(next, shift, slopeAt(t)), t + 0.5 * static_cast<double>(period));
    }

    void unvoicedGrain(double& t, long hop)
    {
        addGrain(source, synth, std::lround(sourceSample(in, t)), std::lround(t), hop);
        t += static_cast<double>(hop);
    }
};
} // namespace

std::vector<float> psolaWarp(const WarpInput& in, size_t outBegin, size_t outEnd)
{
    if (outBegin >= outEnd || in.length == 0)
        return std::vector<float>(outEnd > outBegin ? outEnd - outBegin : 0, 0.0f);
    const auto [a, b] = outputExpandedToQuiet(in, outBegin, outEnd);
    Warper w { in, sourceView(in), {}, {} };
    const auto margin = marginSec * in.sampleRate;
    const auto lo = std::max(0.0, sourceSample(in, static_cast<double>(a)) - margin);
    const auto hi = std::min(static_cast<double>(in.length), sourceSample(in, static_cast<double>(b)) + margin);
    if (in.sourceFrames > 0 && hi > lo)
    {
        const auto [sa, sb] = expandedToQuiet(w.source, static_cast<size_t>(lo), static_cast<size_t>(hi));
        w.marks = pitchMarks(w.source, sa, sb);
    }
    w.synth.offset = a;
    w.synth.out.assign(b - a, 0.0);
    w.synth.weight.assign(b - a, 0.0);
    const auto hop = unvoicedHop(in);
    for (double t = static_cast<double>(a); t < static_cast<double>(b);)
    {
        if (! w.marks.empty() && voicedAtOutput(in, t))
            w.voicedGrain(t);
        else
        {
            t = std::ceil(t / static_cast<double>(hop)) * static_cast<double>(hop);   // 유성 뒤에는 무성 격자로 돌아와, 앞 이력과 무관하게 같은 자리에서 시작
            w.unvoicedGrain(t, hop);
        }
    }
    std::vector<float> y(outEnd - outBegin);
    for (size_t i = 0; i < y.size(); ++i)
    {
        const auto at = outBegin + i - a;
        y[i] = w.synth.weight[at] > 1e-3 ? static_cast<float>(w.synth.out[at] / w.synth.weight[at]) : 0.0f;
    }
    return y;
}
