#include "PitchFlow.h"
#include "BlockTransforms.h"
#include <algorithm>
#include <cmath>

namespace
{
double smoothStep(double x) noexcept
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

// 표시 시각의 원본 음정 (그 시각을 덮는 블롭의 매핑으로). 무성/틈이면 0
double sourceMidiAt(const VocalFeatures& f, const VocalBlock& b, double displaySec) noexcept
{
    if (displaySec < b.displayStartSec() || displaySec >= b.displayEndSec())
        return 0.0;
    const auto k = static_cast<long>(std::lround(displayToSourceSec(b, displaySec) * 1000.0 / f.framePeriodMs));
    if (k < static_cast<long>(b.beginFrame) || k >= static_cast<long>(std::min(b.endFrame, f.size())))
        return 0.0;
    return f.midiF0[static_cast<size_t>(k)];
}

// 이음 [joint - half, joint + half]에서 왼쪽 -> 오른쪽 이동량. 원래 음이 움직인 비율만큼 바뀐다.
// 이음 안에 무성이 끼면 예전처럼 시간에 따라 부드럽게
double blendByFlow(const VocalFeatures& f, const std::vector<VocalBlock>& blocks, const PitchJoint& j, double t) noexcept
{
    const auto& left = blocks[j.left];
    const auto& right = blocks[j.right];
    const auto from = j.timeSec - j.halfWidthSec, step = f.framePeriodMs / 1000.0;
    const auto steps = static_cast<size_t>(std::ceil(2.0 * j.halfWidthSec / step)) + 1;
    std::vector<double> midi(steps);
    for (size_t i = 0; i < steps; ++i)
    {
        const auto at = from + static_cast<double>(i) * step;
        midi[i] = at < j.timeSec ? sourceMidiAt(f, left, at) : sourceMidiAt(f, right, at);
        if (midi[i] <= 0.0)
            return left.pitchShift + (right.pitchShift - left.pitchShift) * smoothStep((t - from) / (2.0 * j.halfWidthSec));
    }
    const auto progress = movementProgress(midi);
    const auto i = std::clamp(static_cast<long>(std::floor((t - from) / step)), 0L, static_cast<long>(steps) - 1);
    const auto weight = std::clamp(totalMovement(midi) / minFlowMovementSemitones, 0.0, 1.0);
    const auto byTime = smoothStep((t - from) / (2.0 * j.halfWidthSec));
    return left.pitchShift + (right.pitchShift - left.pitchShift) * (weight * progress[static_cast<size_t>(i)] + (1.0 - weight) * byTime);
}

double voicedMedian(const VocalFeatures& f, const VocalBlock& b)
{
    std::vector<double> v;
    for (auto i = b.beginFrame; i < b.endFrame; ++i)
        if (f.midiF0[i] > 0.0) v.push_back(f.midiF0[i]);
    if (v.size() < 5)
        return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// [a, b) 프레임이 모두 유성이고 minSlopeFrames 이상이면 그 구간의 움직임 비율에서 frame 자리 값
double slopeProgress(const VocalFeatures& f, size_t a, size_t b, size_t frame, bool& valid)
{
    std::vector<double> seg(f.midiF0.begin() + static_cast<long>(a), f.midiF0.begin() + static_cast<long>(b));
    valid = seg.size() >= minSlopeFrames && std::all_of(seg.begin(), seg.end(), [](double m) { return m > 0.0; });
    return valid ? movementProgress(seg)[frame - a] : 1.0;
}
} // namespace

double totalMovement(const std::vector<double>& values)
{
    double sum = 0.0;
    for (size_t i = 1; i < values.size(); ++i)
        sum += std::abs(values[i] - values[i - 1]);
    return sum;
}

std::vector<double> movementProgress(const std::vector<double>& values)
{
    std::vector<double> out(values.size(), 1.0);
    double sum = 0.0;
    for (size_t i = 1; i < values.size(); ++i)
        sum += std::abs(values[i] - values[i - 1]);
    double run = 0.0;
    for (size_t i = 0; i < values.size(); ++i)
    {
        run += i > 0 ? std::abs(values[i] - values[i - 1]) : 0.0;
        out[i] = sum > 1e-9 ? run / sum : (values.size() > 1 ? static_cast<double>(i) / static_cast<double>(values.size() - 1) : 1.0);
    }
    return out;
}

double landingWeight(const VocalFeatures& f, const VocalBlock& b, size_t frame, bool withHead, bool withTail) noexcept
{
    if ((! withHead && ! withTail) || frame < b.beginFrame || frame >= b.endFrame || b.endFrame > f.size())
        return 1.0;
    const auto median = voicedMedian(f, b);
    if (median <= 0.0)
        return 1.0;
    const auto near = [&](size_t i) { return f.midiF0[i] > 0.0 && std::abs(f.midiF0[i] - median) < landingNearSemitones; };
    auto first = b.beginFrame, last = b.endFrame;
    while (first < b.endFrame && ! near(first)) ++first;
    while (last > first && ! near(last - 1)) --last;
    if (first >= last)
        return 1.0;
    bool valid = false;
    if (withHead && frame < first)
        return slopeProgress(f, b.beginFrame, first, frame, valid);
    if (withTail && frame >= last)
    {
        const auto p = slopeProgress(f, last, b.endFrame, frame, valid);
        return valid ? 1.0 - p : 1.0;
    }
    return 1.0;
}

double flowPitchShift(const VocalFeatures& f, const std::vector<VocalBlock>& blocks, size_t index, size_t frame) noexcept
{
    const auto& cur = blocks[index];
    if (cur.isConsonant || frame >= f.size())
        return cur.pitchShift;
    const auto t = sourceToDisplaySec(cur, f.timeSec[frame]);
    const auto prev = neighbourVoicedBlock(blocks, index, -1), next = neighbourVoicedBlock(blocks, index, +1);
    const auto before = prev ? pitchJointBetween(blocks, *prev, index) : std::nullopt;
    const auto after = next ? pitchJointBetween(blocks, index, *next) : std::nullopt;
    if (before && before->halfWidthSec > 1e-9 && t < before->timeSec + before->halfWidthSec)
        return blendByFlow(f, blocks, *before, t);
    if (after && after->halfWidthSec > 1e-9 && t > after->timeSec - after->halfWidthSec)
        return blendByFlow(f, blocks, *after, t);
    if (std::abs(cur.pitchShift) < 1e-9)
        return 0.0;
    return cur.pitchShift * landingWeight(f, cur, frame, ! before, ! after);
}
