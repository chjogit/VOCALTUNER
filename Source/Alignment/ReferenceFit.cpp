#include "ReferenceFit.h"
#include <algorithm>
#include <cmath>

VocalBlock withTimingFitted(const VocalBlock& block, double targetOffsetSec, const TimeMap& map, double amount)
{
    // 맞춘 위치는 항상 오디오의 원래 위치(startSec/endSec)에서 구한다. 여러 번 맞춰도 같은 결과가 나온다.
    auto out = block;
    const auto start = block.displayStartSec();
    const auto end = block.displayEndSec();
    const auto fittedStart = map.map(block.startSec + targetOffsetSec) - targetOffsetSec;
    const auto fittedEnd = map.map(block.endSec + targetOffsetSec) - targetOffsetSec;
    amount = std::clamp(amount, 0.0, 1.0);
    const auto newStart = std::max(0.0, start + amount * (fittedStart - start));
    const auto newEnd = std::max(newStart + minBlockSeconds, end + amount * (fittedEnd - end));
    out.startOffsetSec = newStart - block.startSec;
    out.endOffsetSec = newEnd - block.endSec;
    return out;
}

double foldedToOctaveOf(double midi, double expectedMidi) noexcept
{
    const auto deviation = midi - expectedMidi;
    return std::abs(deviation) >= octaveErrorSemitones ? midi - 12.0 * std::round(deviation / 12.0) : midi;
}

double referencePitchOver(const std::vector<VocalBlock>& ref, double refOffset, double startSong, double endSong, double expectedMidi)
{
    double sum = 0.0, weight = 0.0;
    for (const auto& b : ref)
    {
        if (b.isConsonant)
            continue;
        const auto overlap = std::min(endSong, b.displayEndSec() + refOffset) - std::max(startSong, b.displayStartSec() + refOffset);
        if (overlap <= 0.0)
            continue;
        const auto centre = expectedMidi > 0.0 ? foldedToOctaveOf(b.displayCenterMidi(), expectedMidi) : b.displayCenterMidi();
        sum += centre * overlap;
        weight += overlap;
    }
    return weight > 0.0 ? sum / weight : -1.0;
}

double snappedPitchShift(double targetCentre, double referenceMidi, PitchRelation relation, double amount)
{
    const auto interval = targetCentre - referenceMidi;
    const auto snapped = relation == PitchRelation::Unison ? 12.0 * std::round(interval / 12.0) : std::round(interval);
    return std::clamp(amount, 0.0, 1.0) * (snapped - interval);
}

namespace
{
// 기준 + amount*(목표-기준) 이 되도록 타이밍 오프셋을 정리해, 겹침/너무 짧음을 막는다
void repairOrder(std::vector<VocalBlock>& blocks)
{
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        auto& b = blocks[i];
        if (i > 0 && b.displayStartSec() < blocks[i - 1].displayEndSec())
            b.startOffsetSec = blocks[i - 1].displayEndSec() - b.startSec;
        if (b.displayEndSec() < b.displayStartSec() + minBlockSeconds)
            b.endOffsetSec = b.displayStartSec() + minBlockSeconds - b.endSec;
    }
}
// 레퍼런스에서 이 블롭에 해당하는 구간 (원래 위치를 대응시킨 시각)
std::pair<double, double> referenceSpan(const VocalBlock& b, double targetOffset, const TimeMap& map)
{
    if (map.empty())
        return { b.startSec + targetOffset, b.endSec + targetOffset };
    return { map.map(b.startSec + targetOffset), map.map(b.endSec + targetOffset) };
}

// 트랙 전체에서 타깃이 레퍼런스와 보통 몇 반음 떨어져 있는지 (중간값). 화음은 반음, 같은 음은 옥타브 단위로 반올림.
double typicalInterval(const std::vector<VocalBlock>& baseline, double targetOffset, const TimeMap& map,
                       const std::vector<VocalBlock>& referenceBlocks, double referenceOffset, PitchRelation relation)
{
    std::vector<double> intervals;
    for (const auto& b : baseline)
    {
        if (b.isConsonant)
            continue;
        const auto [start, end] = referenceSpan(b, targetOffset, map);
        if (const auto ref = referencePitchOver(referenceBlocks, referenceOffset, start, end); ref > 0.0)
            intervals.push_back(b.baseMidi + b.pitchOffset - ref);
    }
    if (intervals.empty())
        return 0.0;
    std::nth_element(intervals.begin(), intervals.begin() + static_cast<long>(intervals.size() / 2), intervals.end());
    const auto median = intervals[intervals.size() / 2];
    return relation == PitchRelation::Unison ? 12.0 * std::round(median / 12.0) : std::round(median);
}

void fitPitch(std::vector<VocalBlock>& fitted, const std::vector<VocalBlock>& baseline, double targetOffset, const TimeMap& map,
              const std::vector<VocalBlock>& referenceBlocks, double referenceOffset, const TrackRoles& roles)
{
    const auto interval = typicalInterval(baseline, targetOffset, map, referenceBlocks, referenceOffset, roles.relation);
    for (size_t i = 0; i < fitted.size(); ++i)
    {
        auto& block = fitted[i];
        if (block.isConsonant)
            continue;
        block.transitionAfter = std::clamp(roles.smoothAmount, 0.0, 1.0);
        const auto centre = baseline[i].baseMidi + baseline[i].pitchOffset;
        const auto [start, end] = referenceSpan(baseline[i], targetOffset, map);
        const auto referenceMidi = referencePitchOver(referenceBlocks, referenceOffset, start, end, centre - interval);
        if (referenceMidi < 0.0)
            continue;
        block.pitchShift = snappedPitchShift(centre, referenceMidi, roles.relation, roles.pitchAmount);
    }
}
} // namespace

std::vector<BlockUpdate> fitToReference(const std::vector<VocalBlock>& baseline, double targetOffset, const TimeMap& map,
                                        const std::vector<VocalBlock>& referenceBlocks, double referenceOffset, const TrackRoles& roles)
{
    auto fitted = baseline;
    if (roles.timing && ! map.empty())
    {
        for (auto& block : fitted)
            block = withTimingFitted(block, targetOffset, map, roles.timingAmount);
        repairOrder(fitted);
    }

    if (roles.pitch)
        fitPitch(fitted, baseline, targetOffset, map, referenceBlocks, referenceOffset, roles);

    std::vector<BlockUpdate> updates;
    for (size_t i = 0; i < fitted.size(); ++i)
        updates.push_back({ i, std::move(fitted[i]) });
    return updates;
}
