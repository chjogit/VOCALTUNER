#include "BlockTransforms.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr double touchGapSec = 0.04;        // 이 이하로 맞닿은 블록은 볼륨을 이어 붙임
constexpr double maxTransitionSec = 0.04;   // 경계 양쪽으로 퍼지는 최대 폭
constexpr double pi = 3.14159265358979323846;

double smoothstep(double x) noexcept
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

bool touching(const VocalBlock& left, const VocalBlock& right) noexcept
{
    const auto gap = right.displayStartSec() - left.displayEndSec();
    return gap >= -1e-9 && gap <= touchGapSec;
}

double transitionHalfWidth(const VocalBlock& left, const VocalBlock& right) noexcept
{
    const auto shorter = std::min(left.displayEndSec() - left.displayStartSec(), right.displayEndSec() - right.displayStartSec());
    return std::max(0.0, std::min(maxTransitionSec, 0.5 * shorter));
}

double jointTime(const VocalBlock& left, const VocalBlock& right) noexcept
{
    return 0.5 * (left.displayEndSec() + right.displayStartSec());
}

// 경계 joint 주변 [joint-half, joint+half]에서 a -> b 로 부드럽게 전환
double blendAcross(double a, double b, double joint, double half, double t) noexcept
{
    return half <= 1e-9 ? (t < joint ? a : b) : a + (b - a) * smoothstep((t - (joint - half)) / (2.0 * half));
}

// 빈 곳은 직전 유성 값으로 채운 곡선 (유성 프레임이 없으면 빈 벡터)
std::vector<double> filledCurve(const VocalFeatures& f, const VocalBlock& b)
{
    std::vector<double> curve;
    if (b.endFrame <= b.beginFrame || b.endFrame > f.size())
        return curve;

    double last = 0.0;
    for (auto i = b.beginFrame; i < b.endFrame; ++i)
        if (f.midiF0[i] > 0.0)
        {
            last = f.midiF0[i];
            break;
        }
    if (last <= 0.0)
        return curve;

    for (auto i = b.beginFrame; i < b.endFrame; ++i)
    {
        if (f.midiF0[i] > 0.0)
            last = f.midiF0[i];
        curve.push_back(last);
    }
    return curve;
}

std::vector<double> movingAverage(const std::vector<double>& x, size_t radius)
{
    std::vector<double> out(x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto lo = i > radius ? i - radius : 0;
        const auto hi = std::min(x.size() - 1, i + radius);
        double sum = 0.0;
        for (auto k = lo; k <= hi; ++k)
            sum += x[k];
        out[i] = sum / static_cast<double>(hi - lo + 1);
    }
    return out;
}

// 임계값 이상 꺾이는 극값만 모으는 zigzag
std::vector<size_t> zigzagExtremes(const std::vector<double>& s, double threshold)
{
    std::vector<size_t> out;
    if (s.empty())
        return out;

    size_t hiIdx = 0, loIdx = 0;
    double hi = s[0], lo = s[0];
    int direction = 0;
    for (size_t i = 1; i < s.size(); ++i)
    {
        const auto v = s[i];
        if (direction == 0)
        {
            if (v > hi) { hi = v; hiIdx = i; }
            if (v < lo) { lo = v; loIdx = i; }
            if (v - lo >= threshold)      { out.push_back(loIdx); direction = 1;  hi = v; hiIdx = i; }
            else if (hi - v >= threshold) { out.push_back(hiIdx); direction = -1; lo = v; loIdx = i; }
        }
        else if (direction > 0)
        {
            if (v > hi)                   { hi = v; hiIdx = i; }
            else if (hi - v >= threshold) { out.push_back(hiIdx); direction = -1; lo = v; loIdx = i; }
        }
        else
        {
            if (v < lo)                   { lo = v; loIdx = i; }
            else if (v - lo >= threshold) { out.push_back(loIdx); direction = 1; hi = v; hiIdx = i; }
        }
    }
    return out;
}
} // namespace

double sourceToDisplaySec(const VocalBlock& b, double sourceSec) noexcept
{
    const auto span = b.endSec - b.startSec;
    if (span <= 0.0)
        return b.displayStartSec();
    return b.displayStartSec() + (sourceSec - b.startSec) / span * (b.displayEndSec() - b.displayStartSec());
}

double displayToSourceSec(const VocalBlock& b, double displaySec) noexcept
{
    const auto span = b.displayEndSec() - b.displayStartSec();
    if (span <= 0.0)
        return b.startSec;
    return b.startSec + (displaySec - b.displayStartSec()) / span * (b.endSec - b.startSec);
}

namespace
{
std::optional<size_t> neighbourVoiced(const std::vector<VocalBlock>& blocks, size_t index, int direction)
{
    for (auto i = static_cast<long>(index) + direction; i >= 0 && i < static_cast<long>(blocks.size()); i += direction)
        if (! blocks[static_cast<size_t>(i)].isConsonant)
            return static_cast<size_t>(i);
    return std::nullopt;
}

std::optional<PitchJoint> jointBetween(const std::vector<VocalBlock>& blocks, size_t l, size_t r)
{
    const auto& left = blocks[l];
    const auto& right = blocks[r];
    const auto gap = right.displayStartSec() - left.displayEndSec();
    if (gap < -1e-9 || gap > pitchJointMaxGapSec)
        return std::nullopt;

    const auto shorter = std::min(left.displayEndSec() - left.displayStartSec(), right.displayEndSec() - right.displayStartSec());
    const auto amount = std::clamp(left.transitionAfter, 0.0, 1.0);
    const auto maxHalf = std::max(0.0, std::min(pitchTransitionMaxHalfSec, 0.5 * shorter));
    return PitchJoint { l, r, 0.5 * (left.displayEndSec() + right.displayStartSec()), amount * maxHalf };
}

} // namespace

std::vector<PitchJoint> findPitchJoints(const std::vector<VocalBlock>& blocks)
{
    std::vector<PitchJoint> joints;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        if (blocks[i].isConsonant)
            continue;
        if (const auto next = neighbourVoiced(blocks, i, +1))
            if (const auto joint = jointBetween(blocks, i, *next))
                joints.push_back(*joint);
    }
    return joints;
}

double effectivePitchShift(const std::vector<VocalBlock>& blocks, size_t index, double t) noexcept
{
    const auto& cur = blocks[index];
    auto shift = cur.pitchShift;
    if (cur.isConsonant)
        return shift;

    if (const auto prev = neighbourVoiced(blocks, index, -1))
        if (const auto joint = jointBetween(blocks, *prev, index); joint && t < joint->timeSec + joint->halfWidthSec)
            shift = blendAcross(blocks[*prev].pitchShift, cur.pitchShift, joint->timeSec, joint->halfWidthSec, t);
    if (const auto next = neighbourVoiced(blocks, index, +1))
        if (const auto joint = jointBetween(blocks, index, *next); joint && t > joint->timeSec - joint->halfWidthSec)
            shift = blendAcross(cur.pitchShift, blocks[*next].pitchShift, joint->timeSec, joint->halfWidthSec, t);
    return shift;
}

// 곡선 편집(굳힌 보정 포함)이 반영된 프레임의 피치. 펴기/비브라토/정돈은 항상 이 곡선을 기준으로 계산한다.
double workingMidiAt(const VocalFeatures& f, const VocalBlock& b, size_t frame) noexcept
{
    auto midi = f.midiF0[frame];
    if (midi > 0.0 && b.curveOffset.size() == b.endFrame - b.beginFrame && frame >= b.beginFrame && frame < b.endFrame)
        midi += static_cast<double>(b.curveOffset[frame - b.beginFrame]);
    return midi;
}

namespace
{
bool shapeApplies(const VocalFeatures& f, const VocalBlock& b, size_t frame) noexcept
{
    return ! b.isConsonant && frame >= b.beginFrame && frame < b.endFrame && frame < f.size() && f.midiF0[frame] > 0.0;
}
} // namespace

double flattenOffsetAt(const VocalFeatures& f, const VocalBlock& b, size_t frame) noexcept
{
    if (b.flattenAmount <= 0.0 || ! shapeApplies(f, b, frame))
        return 0.0;
    return std::clamp(b.flattenAmount, 0.0, 1.0) * (b.baseMidi + b.pitchOffset - workingMidiAt(f, b, frame));
}

// 중심 음정: 블롭 전체를 같은 양만큼 목표 음 쪽으로 (곡선 모양 유지)
double tidyOffsetAt(const VocalFeatures& f, const VocalBlock& b, size_t frame) noexcept
{
    if (b.tidyAmount <= 0.0 || ! shapeApplies(f, b, frame))
        return 0.0;
    const auto target = b.centerTargetMidi >= 0 ? b.centerTargetMidi : b.baseMidi;
    return std::clamp(b.tidyAmount, 0.0, 1.0) * (target - (b.baseMidi + b.pitchOffset));
}

namespace
{
struct LocalRange { double mid = 0.0, halfRange = 0.0, mean = 0.0; };

// 약 한 주기(±200 ms)의 곡선에서 위/아래 극값의 가운데와 폭
LocalRange localRange(const VocalFeatures& f, const VocalBlock& b, size_t frame) noexcept
{
    constexpr size_t radius = 20;
    const auto lo = frame - b.beginFrame > radius ? frame - radius : b.beginFrame;
    const auto hi = std::min(b.endFrame - 1, frame + radius);
    double low = 1e9, high = -1e9, sum = 0.0;
    size_t count = 0;
    for (auto i = lo; i <= hi; ++i)
        if (f.midiF0[i] > 0.0)
        {
            const auto w = workingMidiAt(f, b, i);
            low = std::min(low, w);
            high = std::max(high, w);
            sum += w;
            ++count;
        }
    if (count == 0)
        return { workingMidiAt(f, b, frame), 0.0, workingMidiAt(f, b, frame) };
    return { 0.5 * (low + high), 0.5 * (high - low), sum / static_cast<double>(count) };
}
} // namespace

// 음정 흐름: 약 한 비브라토 주기(±100 ms)로 고른 곡선이 중심에서 벗어난 만큼을 줄인다. 빠른 떨림(비브라토)은 그대로
double driftOffsetAt(const VocalFeatures& f, const VocalBlock& b, size_t frame) noexcept
{
    if (std::abs(b.driftAmount) < 1e-9 || ! shapeApplies(f, b, frame))
        return 0.0;
    const auto keep = 1.0 - std::clamp(b.flattenAmount, 0.0, 1.0);    // 펴기가 이미 끌어온 만큼은 빼고
    return std::clamp(b.driftAmount, -1.0, 1.0) * keep * (b.baseMidi + b.pitchOffset - localRange(f, b, frame).mean);
}

double vibratoOffsetAt(const VocalFeatures& f, const VocalBlock& b, size_t frame) noexcept
{
    if (b.vibratoDepth <= 0.0 || frame < b.beginFrame || frame >= b.endFrame || frame >= f.size() || f.midiF0[frame] <= 0.0)
        return 0.0;

    const auto range = localRange(f, b, frame);
    const auto flatten = std::clamp(b.flattenAmount, 0.0, 1.0);
    const auto depth = std::clamp(b.vibratoDepth, 0.0, maxVibratoDepth);
    const auto working = workingMidiAt(f, b, frame);
    const auto base = working + flattenOffsetAt(f, b, frame) + tidyOffsetAt(f, b, frame) + driftOffsetAt(f, b, frame);

    // 이미 비브라토가 있는 곡선: 모양은 그대로 두고, 위/아래 극값의 가운데(좌우 균형)를 중심으로 폭만 깊이에 맞춘다
    const auto centre = (1.0 - flatten) * range.mid + flatten * (b.baseMidi + b.pitchOffset);
    const auto scale = range.halfRange > 1e-6 ? std::min(depth / range.halfRange, 1.5) : 0.0;
    const auto natural = centre + (working - range.mid) * scale;

    // 비브라토가 없는 곡선: 평균 위에 사인을 얹는다. 시작/끝은 서서히 커지고 작아진다
    const auto length = b.endSec - b.startSec;
    const auto t = f.timeSec[frame];
    const auto fadeIn = std::min(0.3 * length, 0.25);
    const auto fadeOut = std::min(0.15 * length, 0.1);
    const auto envelope = smoothstep((t - b.startSec) / std::max(fadeIn, 1e-6)) * smoothstep((b.endSec - t) / std::max(fadeOut, 1e-6));
    const auto rate = std::clamp(b.vibratoRateHz, minVibratoRate, maxVibratoRate);
    const auto synthetic = (1.0 - flatten) * range.mean + flatten * (b.baseMidi + b.pitchOffset) + depth * envelope * std::sin(2.0 * pi * rate * t);

    // 기존 비브라토 폭이 0.1 반음 이하면 합성, 0.25 이상이면 자연 곡선 (사이는 부드럽게 섞음)
    const auto natural_weight = smoothstep((range.halfRange - 0.1) / 0.15);
    const auto target = natural_weight * natural + (1.0 - natural_weight) * synthetic;
    const auto mix = std::min(1.0, b.vibratoDepth / 0.15);   // 아주 작은 깊이에서 갑자기 바뀌지 않도록
    return mix * (target - base);
}

double shapeOffsetAt(const VocalFeatures& f, const VocalBlock& b, size_t frame) noexcept
{
    return flattenOffsetAt(f, b, frame) + tidyOffsetAt(f, b, frame) + driftOffsetAt(f, b, frame) + vibratoOffsetAt(f, b, frame);
}

VocalBlock withShapeBaked(const VocalFeatures& f, const VocalBlock& b)
{
    if (b.isConsonant || b.endFrame <= b.beginFrame || (b.flattenAmount <= 0.0 && b.vibratoDepth <= 0.0 && std::abs(b.driftAmount) < 1e-9))   // 중심 음정만 있는 블록은 건드리지 않는다
        return b;
    auto out = b;
    const auto count = b.endFrame - b.beginFrame;
    if (out.curveOffset.size() != count)
        out.curveOffset.assign(count, 0.0f);
    for (auto frame = b.beginFrame; frame < b.endFrame && frame < f.size(); ++frame)
        out.curveOffset[frame - b.beginFrame] += static_cast<float>(shapeOffsetAt(f, b, frame));   // b(굳히기 전) 기준으로 계산
    out.flattenAmount = 0.0;
    out.tidyAmount = 0.0;
    out.driftAmount = 0.0;
    out.vibratoDepth = 0.0;
    return out;
}

double editedPitchAt(const VocalFeatures& f, const std::vector<VocalBlock>& blocks, size_t index, size_t frame) noexcept
{
    const auto& b = blocks[index];
    if (frame < b.beginFrame || frame >= b.endFrame || frame >= f.size() || f.midiF0[frame] <= 0.0)
        return 0.0;
    auto pitch = f.midiF0[frame] + effectivePitchShift(blocks, index, sourceToDisplaySec(b, f.timeSec[frame])) + shapeOffsetAt(f, b, frame);
    if (b.curveOffset.size() == b.endFrame - b.beginFrame)
        pitch += static_cast<double>(b.curveOffset[frame - b.beginFrame]);
    return pitch;
}

double curveFrameDisplaySec(const VocalFeatures& f, const VocalBlock& b, size_t frame) noexcept
{
    auto sourceSec = f.timeSec[frame];
    if (b.curveWarp.size() == b.endFrame - b.beginFrame && frame >= b.beginFrame && frame < b.endFrame)
        sourceSec += static_cast<double>(b.curveWarp[frame - b.beginFrame]) * f.framePeriodMs / 1000.0;
    return sourceToDisplaySec(b, sourceSec);
}

VocalBlock alignedToNoteCenter(const VocalBlock& block) noexcept
{
    auto aligned = block;
    if (! block.isConsonant)
    {
        const auto centre = block.baseMidi + block.pitchOffset;
        aligned.pitchShift = std::round(centre) - centre;
    }
    return aligned;
}

double averagePitchMidi(const std::vector<VocalBlock>& blocks) noexcept
{
    double sum = 0.0, weight = 0.0;
    for (const auto& b : blocks)
        if (! b.isConsonant)
        {
            const auto duration = std::max(0.0, b.endSec - b.startSec);
            sum += (b.baseMidi + b.pitchOffset) * duration;
            weight += duration;
        }
    return weight > 0.0 ? sum / weight : 60.0;
}

double blockPitchAt(const VocalFeatures& f, const VocalBlock& b, size_t frame) noexcept
{
    if (frame < b.beginFrame || frame >= b.endFrame || frame >= f.size() || f.midiF0[frame] <= 0.0)
        return 0.0;
    auto pitch = f.midiF0[frame] + b.pitchShift + shapeOffsetAt(f, b, frame);
    if (b.curveOffset.size() == b.endFrame - b.beginFrame)
        pitch += static_cast<double>(b.curveOffset[frame - b.beginFrame]);
    return pitch;
}

double effectiveGainDb(const std::vector<VocalBlock>& blocks, size_t index, double t) noexcept
{
    const auto& cur = blocks[index];
    auto gain = cur.gainDb;

    if (index > 0 && touching(blocks[index - 1], cur))
    {
        const auto& left = blocks[index - 1];
        const auto joint = jointTime(left, cur);
        const auto half = transitionHalfWidth(left, cur);
        if (t < joint + half)
            gain = blendAcross(left.gainDb, cur.gainDb, joint, half, t);
    }
    if (index + 1 < blocks.size() && touching(cur, blocks[index + 1]))
    {
        const auto& right = blocks[index + 1];
        const auto joint = jointTime(cur, right);
        const auto half = transitionHalfWidth(cur, right);
        if (t > joint - half)
            gain = blendAcross(cur.gainDb, right.gainDb, joint, half, t);
    }
    return gain;
}

VocalBlock movedInTime(const std::vector<VocalBlock>& blocks, size_t index, double deltaSec)
{
    auto block = blocks[index];
    auto lo = -block.displayStartSec();   // 시간 0 앞으로는 못 감
    auto hi = 1e12;
    if (index > 0)
        lo = std::max(lo, blocks[index - 1].displayEndSec() - block.displayStartSec());
    if (index + 1 < blocks.size())
        hi = blocks[index + 1].displayStartSec() - block.displayEndSec();

    const auto delta = lo > hi ? 0.0 : std::clamp(deltaSec, lo, hi);
    block.startOffsetSec += delta;
    block.endOffsetSec += delta;
    return block;
}

VocalBlock resizedInTime(const std::vector<VocalBlock>& blocks, size_t index, BlockEdge edge, double deltaSec)
{
    auto block = blocks[index];
    if (edge == BlockEdge::Front)
    {
        const auto lowest = index > 0 ? blocks[index - 1].displayEndSec() : 0.0;
        const auto highest = block.displayEndSec() - minBlockSeconds;
        const auto target = std::clamp(block.displayStartSec() + deltaSec, std::min(lowest, highest), highest);
        block.startOffsetSec = target - block.startSec;
    }
    else
    {
        const auto lowest = block.displayStartSec() + minBlockSeconds;
        const auto highest = index + 1 < blocks.size() ? blocks[index + 1].displayStartSec() : 1e12;
        const auto target = std::clamp(block.displayEndSec() + deltaSec, lowest, std::max(lowest, highest));
        block.endOffsetSec = target - block.endSec;
    }
    return block;
}

namespace
{
void shiftBlock(VocalBlock& block, double deltaSec) noexcept
{
    block.startOffsetSec += deltaSec;
    block.endOffsetSec += deltaSec;
}

// 선택되지 않은 블록이 겹치면 길이를 유지한 채 밀어낸다 (오른쪽 먼저, 그다음 왼쪽)
void pushOverlaps(std::vector<VocalBlock>& blocks, const std::vector<bool>& selected)
{
    for (size_t i = 1; i < blocks.size(); ++i)
        if (! selected[i] && blocks[i].displayStartSec() < blocks[i - 1].displayEndSec())
            shiftBlock(blocks[i], blocks[i - 1].displayEndSec() - blocks[i].displayStartSec());
    for (size_t i = blocks.size(); i-- > 1;)
        if (! selected[i - 1] && blocks[i - 1].displayEndSec() > blocks[i].displayStartSec())
            shiftBlock(blocks[i - 1], blocks[i].displayStartSec() - blocks[i - 1].displayEndSec());
}

bool pushedArrangementValid(const std::vector<VocalBlock>& blocks, const std::vector<size_t>& indices)
{
    for (const auto& b : blocks)
        if (b.displayStartSec() < -1e-9)
            return false;
    for (const auto i : indices)
        if (blocks[i].displayEndSec() - blocks[i].displayStartSec() < minBlockSeconds - 1e-9)
            return false;
    for (size_t i = 0; i + 1 < blocks.size(); ++i)
        if (blocks[i + 1].displayStartSec() < blocks[i].displayEndSec() - 1e-6)
            return false;
    return true;
}

bool sameTiming(const VocalBlock& a, const VocalBlock& b) noexcept
{
    return a.startOffsetSec == b.startOffsetSec && a.endOffsetSec == b.endOffsetSec;
}

// candidate(fraction)은 요청한 편집의 fraction(0~1)만큼 적용한 선택 블록들을 만든다.
// 이웃은 밀려나고, 시간 0 앞으로 밀리게 되는 경우에는 가능한 가장 큰 fraction만 적용한다.
template <class Candidate>
std::vector<BlockUpdate> pushedWithLargestValid(const std::vector<VocalBlock>& blocks, const std::vector<size_t>& indices,
                                                Candidate candidate)
{
    std::vector<bool> selected(blocks.size(), false);
    for (const auto i : indices)
        selected[i] = true;

    const auto apply = [&](double fraction, std::vector<VocalBlock>& all)
    {
        all = blocks;
        auto edited = candidate(fraction);
        for (size_t k = 0; k < indices.size(); ++k)
            all[indices[k]] = std::move(edited[k]);
        pushOverlaps(all, selected);
        return pushedArrangementValid(all, indices);
    };

    std::vector<VocalBlock> result;
    if (! apply(1.0, result))
    {
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 24; ++i)
        {
            const auto mid = 0.5 * (lo + hi);
            (apply(mid, result) ? lo : hi) = mid;
        }
        apply(lo, result);
    }

    std::vector<BlockUpdate> updates;
    for (size_t i = 0; i < result.size(); ++i)
        if (selected[i] || ! sameTiming(result[i], blocks[i]))
            updates.push_back({ i, std::move(result[i]) });
    return updates;
}

std::vector<VocalBlock> pick(const std::vector<VocalBlock>& blocks, const std::vector<size_t>& indices)
{
    std::vector<VocalBlock> out;
    for (const auto i : indices)
        out.push_back(blocks[i]);
    return out;
}

std::vector<BlockUpdate> unchanged(const std::vector<VocalBlock>& blocks, const std::vector<size_t>& indices)
{
    std::vector<BlockUpdate> out;
    for (const auto i : indices)
        out.push_back({ i, blocks[i] });
    return out;
}
} // namespace

std::vector<BlockUpdate> groupMovedInTime(const std::vector<VocalBlock>& blocks, const std::vector<size_t>& indices, double deltaSec)
{
    if (indices.empty())
        return {};
    return pushedWithLargestValid(blocks, indices, [&](double fraction)
    {
        auto out = pick(blocks, indices);
        for (auto& b : out)
            shiftBlock(b, deltaSec * fraction);
        return out;
    });
}

std::vector<BlockUpdate> groupScaledInTime(const std::vector<VocalBlock>& blocks, const std::vector<size_t>& indices,
                                           BlockEdge edge, double deltaSec)
{
    if (indices.empty())
        return {};

    double start = 1e12, end = -1e12;
    for (const auto i : indices)
    {
        start = std::min(start, blocks[i].displayStartSec());
        end = std::max(end, blocks[i].displayEndSec());
    }
    const auto span = end - start;
    if (span <= 0.0)
        return unchanged(blocks, indices);

    const auto anchor = edge == BlockEdge::Front ? end : start;
    const auto movedEdge = (edge == BlockEdge::Front ? start : end) + deltaSec;
    const auto scale = std::max(0.01, std::abs(movedEdge - anchor) / span);
    const auto sign = (edge == BlockEdge::Front ? movedEdge <= anchor : movedEdge >= anchor) ? 1.0 : 0.0;   // 반대편을 넘기면 최소 크기로
    return pushedWithLargestValid(blocks, indices, [&](double fraction)
    {
        const auto factor = 1.0 + ((sign > 0.0 ? scale : 0.01) - 1.0) * fraction;
        auto out = pick(blocks, indices);
        for (auto& b : out)
        {
            const auto s = anchor + (b.displayStartSec() - anchor) * factor;
            const auto e = anchor + (b.displayEndSec() - anchor) * factor;
            b.startOffsetSec = s - b.startSec;
            b.endOffsetSec = e - b.endSec;
        }
        return out;
    });
}

bool LinkedEdit::isEmpty() const noexcept
{
    constexpr double eps = 1e-9;
    const auto timing = std::abs(newStart - oldStart) > eps || std::abs(newEnd - oldEnd) > eps;
    const auto values = std::abs(pitchShift) > eps || std::abs(gainDb) > eps || std::abs(vibratoDepth) > eps
                     || std::abs(vibratoRateHz) > eps || std::abs(flattenAmount) > eps;
    return ! timing && ! values && ! snapToNote;
}

LinkedEdit makeLinkedEdit(const std::vector<std::pair<VocalBlock, VocalBlock>>& changed)
{
    LinkedEdit edit;
    if (changed.empty())
        return edit;

    edit.oldStart = edit.newStart = 1e12;
    edit.oldEnd = edit.newEnd = -1e12;
    for (const auto& [before, after] : changed)
    {
        edit.oldStart = std::min(edit.oldStart, before.displayStartSec());
        edit.oldEnd = std::max(edit.oldEnd, before.displayEndSec());
        edit.newStart = std::min(edit.newStart, after.displayStartSec());
        edit.newEnd = std::max(edit.newEnd, after.displayEndSec());
    }
    // 값 변화는 기준 블록(첫 번째 유성 블록)의 차이를 쓴다
    const auto voiced = std::find_if(changed.begin(), changed.end(), [](const auto& p) { return ! p.first.isConsonant; });
    const auto& reference = voiced != changed.end() ? *voiced : changed.front();
    edit.pitchShift = reference.second.pitchShift - reference.first.pitchShift;
    edit.gainDb = reference.second.gainDb - reference.first.gainDb;
    edit.vibratoDepth = reference.second.vibratoDepth - reference.first.vibratoDepth;
    edit.vibratoRateHz = reference.second.vibratoRateHz - reference.first.vibratoRateHz;
    edit.flattenAmount = reference.second.flattenAmount - reference.first.flattenAmount;

    // 음정 맞춤(더블클릭)은 다른 트랙에 이동량으로 옮기지 않는다. 트랙마다 자기 녹음의 가장 가까운 음정으로 맞춘다.
    edit.snapToNote = std::abs(edit.pitchShift) > 1e-9
        && std::all_of(changed.begin(), changed.end(), [](const auto& p)
           {
               return p.first.isConsonant || std::abs(p.second.pitchShift - alignedToNoteCenter(p.first).pitchShift) < 1e-9;
           });
    return edit;
}

std::vector<BlockUpdate> applyLinkedEdit(const std::vector<VocalBlock>& blocks, const LinkedEdit& edit)
{
    if (edit.isEmpty())
        return {};

    std::vector<size_t> indices;
    for (size_t i = 0; i < blocks.size(); ++i)
        if (std::min(blocks[i].displayEndSec(), edit.oldEnd) - std::max(blocks[i].displayStartSec(), edit.oldStart) > 1e-9)
            indices.push_back(i);
    if (indices.empty())
        return {};

    const auto oldSpan = edit.oldEnd - edit.oldStart;
    const auto warp = [&](double t, double fraction)
    {
        const auto newStart = edit.oldStart + fraction * (edit.newStart - edit.oldStart);
        const auto newEnd = edit.oldEnd + fraction * (edit.newEnd - edit.oldEnd);
        const auto s = oldSpan > 1e-9 ? (newEnd - newStart) / oldSpan : 1.0;
        if (t <= edit.oldStart)
            return t + (newStart - edit.oldStart);
        if (t >= edit.oldEnd)
            return t + (newEnd - edit.oldEnd);
        return newStart + (t - edit.oldStart) * s;
    };

    return pushedWithLargestValid(blocks, indices, [&](double fraction)
    {
        auto out = pick(blocks, indices);
        for (auto& b : out)
        {
            const auto s = warp(b.displayStartSec(), fraction);
            const auto e = std::max(s + minBlockSeconds, warp(b.displayEndSec(), fraction));
            b.startOffsetSec = s - b.startSec;
            b.endOffsetSec = e - b.endSec;
            if (b.isConsonant)
                continue;
            b.pitchShift = edit.snapToNote ? alignedToNoteCenter(b).pitchShift : std::clamp(b.pitchShift + edit.pitchShift, -24.0, 24.0);
            b.vibratoDepth = std::clamp(b.vibratoDepth + edit.vibratoDepth, 0.0, maxVibratoDepth);
            b.vibratoRateHz = std::clamp(b.vibratoRateHz + edit.vibratoRateHz, minVibratoRate, maxVibratoRate);
            b.flattenAmount = std::clamp(b.flattenAmount + edit.flattenAmount, 0.0, 1.0);
        }
        for (auto& b : out)
            b.gainDb = std::clamp(b.gainDb + edit.gainDb, -24.0, 24.0);
        return out;
    });
}

std::vector<size_t> findCurveHandles(const VocalFeatures& f, const VocalBlock& b, double thresholdSemitones,
                                     size_t minSpacingFrames)
{
    const auto curve = filledCurve(f, b);
    const auto smooth = movingAverage(curve, 3);
    std::vector<size_t> handles;
    for (const auto relative : zigzagExtremes(smooth, thresholdSemitones))
    {
        const auto fromEnd = smooth.size() - 1 - relative;
        if (relative < minSpacingFrames || fromEnd < minSpacingFrames)
            continue;
        const auto frame = b.beginFrame + relative;
        if (! handles.empty() && frame - handles.back() < minSpacingFrames)
            continue;
        handles.push_back(frame);
    }
    return handles;
}

std::vector<size_t> allCurveHandles(const VocalFeatures& f, const VocalBlock& b, size_t minSpacingFrames)
{
    std::vector<size_t> users;
    for (const auto frame : b.userHandles)
        if (frame > b.beginFrame && frame + 1 < b.endFrame)
            users.push_back(frame);
    std::sort(users.begin(), users.end());

    // 처음과 끝도 항상 핸들: 시작/끝 음정과 위치를 조정할 수 있다
    const auto hasEnds = b.endFrame > b.beginFrame + 2 * minSpacingFrames;
    if (hasEnds)
        users.erase(std::remove_if(users.begin(), users.end(), [&](size_t u) { return u < b.beginFrame + minSpacingFrames / 2 || u + minSpacingFrames / 2 >= b.endFrame; }), users.end());
    auto handles = users;
    if (hasEnds)
    {
        handles.push_back(b.beginFrame);
        handles.push_back(b.endFrame - 1);
    }
    for (const auto frame : findCurveHandles(f, b, 0.3, minSpacingFrames))
    {
        const auto near = std::any_of(users.begin(), users.end(), [&](size_t u) { return (u > frame ? u - frame : frame - u) < minSpacingFrames; });
        if (! near)
            handles.push_back(frame);
    }
    std::sort(handles.begin(), handles.end());
    return handles;
}

VocalBlock withUserHandleToggled(const VocalBlock& block, size_t frame, size_t removeRadiusFrames, size_t minSpacingFrames)
{
    auto out = block;
    const auto it = std::find_if(out.userHandles.begin(), out.userHandles.end(),
                                 [&](size_t h) { return (h > frame ? h - frame : frame - h) <= removeRadiusFrames; });
    if (it != out.userHandles.end())
    {
        out.userHandles.erase(it);
        return out;
    }
    // 짧은 블롭에서도 추가할 수 있도록 가장자리 여백은 블롭 길이의 1/4을 넘지 않는다
    const auto length = block.endFrame > block.beginFrame ? block.endFrame - block.beginFrame : 0;
    const auto margin = std::min(minSpacingFrames, std::max<size_t>(1, length / 4));
    if (length < 3 || frame < block.beginFrame + margin || frame + margin >= block.endFrame)
        return out;
    out.userHandles.push_back(frame);
    std::sort(out.userHandles.begin(), out.userHandles.end());
    return out;
}

VocalBlock withCurveHandleDragged(const VocalBlock& origin, const std::vector<size_t>& handles, size_t handleIndex,
                                  double deltaSemitones, double deltaFrames)
{
    auto block = origin;
    if (handleIndex >= handles.size() || block.endFrame <= block.beginFrame)
        return block;

    const auto count = block.endFrame - block.beginFrame;
    if (block.curveOffset.size() != count)
        block.curveOffset.assign(count, 0.0f);
    if (block.curveWarp.size() != count)
        block.curveWarp.assign(count, 0.0f);

    const auto centre = handles[handleIndex];
    const auto before = handleIndex > 0 ? handles[handleIndex - 1] : block.beginFrame;
    const auto after = handleIndex + 1 < handles.size() ? handles[handleIndex + 1] : block.endFrame - 1;
    // 시간 이동은 이웃 핸들까지 거리의 60%로 제한해 곡선이 뒤집히지 않게 한다
    // (처음/끝 핸들은 바깥쪽 이웃이 없으므로 안쪽 이웃까지의 거리만 본다)
    const auto toBefore = handleIndex > 0 ? centre - before : after - centre;
    const auto toAfter = handleIndex + 1 < handles.size() ? after - centre : centre - before;
    const auto limit = 0.6 * static_cast<double>(std::min(toBefore, toAfter));
    deltaFrames = std::clamp(deltaFrames, -limit, limit);
    for (auto frame = before; frame <= after && frame < block.endFrame; ++frame)
    {
        double weight = 1.0;
        if (frame < centre && centre > before)
            weight = 0.5 * (1.0 - std::cos(pi * static_cast<double>(frame - before) / static_cast<double>(centre - before)));
        else if (frame > centre && after > centre)
            weight = 0.5 * (1.0 + std::cos(pi * static_cast<double>(frame - centre) / static_cast<double>(after - centre)));
        block.curveOffset[frame - block.beginFrame] += static_cast<float>(deltaSemitones * weight);
        block.curveWarp[frame - block.beginFrame] += static_cast<float>(deltaFrames * weight);
    }
    return block;
}

std::vector<BlockUpdate> withBaseCorrection(const std::vector<VocalBlock>& blocks, bool on)
{
    std::vector<BlockUpdate> updates;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        if (on == (blocks[i].tidyAmount > 0.0))
            continue;
        auto block = blocks[i];
        block.tidyAmount = on ? defaultTidyAmount : 0.0;
        updates.push_back({ i, std::move(block) });
    }
    return updates;
}

int nearestScaleNote(double midi, const PitchScale& scale) noexcept
{
    const auto nearest = static_cast<int>(std::lround(midi));
    if (scale.root < 0)
        return nearest;
    static constexpr bool major[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
    static constexpr bool minor[12] = { true, false, true, true, false, true, false, true, true, false, true, false };
    const auto& steps = scale.minor ? minor : major;
    const auto inScale = [&](int note) { return steps[((note - scale.root) % 12 + 12) % 12]; };
    for (int distance = 0; distance <= 6; ++distance)
    {
        // 같은 거리면 실제 음에 더 가까운 쪽
        const auto below = nearest - distance, above = nearest + distance;
        const auto first = std::abs(midi - below) <= std::abs(midi - above) ? below : above;
        const auto second = first == below ? above : below;
        if (inScale(first))
            return first;
        if (inScale(second))
            return second;
    }
    return nearest;
}

std::vector<BlockUpdate> withPitchBalance(const std::vector<VocalBlock>& blocks, const std::vector<size_t>& indices,
                                          const PitchBalance& balance)
{
    std::vector<BlockUpdate> updates;
    for (const auto i : indices)
    {
        if (i >= blocks.size() || blocks[i].isConsonant)
            continue;
        auto block = blocks[i];
        const auto centre = block.baseMidi + block.pitchOffset;
        const auto target = nearestScaleNote(centre, balance.scale);
        block.centerTargetMidi = target == block.baseMidi ? -1 : target;
        const auto withinTolerance = std::abs(centre - target) * 100.0 < balance.toleranceCents;
        block.tidyAmount = withinTolerance ? 0.0 : std::clamp(balance.center, 0.0, 1.0);
        block.driftAmount = std::clamp(balance.drift, 0.0, 1.0);
        updates.push_back({ i, std::move(block) });
    }
    return updates;
}
