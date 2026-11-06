#include "ReferenceFit.h"
#include <algorithm>
#include <optional>
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

double referenceCurvePitchOver(const VocalFeatures& curve, double startSec, double endSec, double expectedMidi)
{
    const auto hop = curve.framePeriodMs / 1000.0;
    const auto first = static_cast<size_t>(std::max(0.0, std::ceil(startSec / hop)));
    const auto last = std::min(curve.size(), static_cast<size_t>(std::max(0.0, std::floor(endSec / hop))) + 1);
    if (last <= first)
        return -1.0;
    std::vector<double> voiced;
    for (auto t = first; t < last; ++t)
        if (curve.midiF0[t] > 0.0)
        {
            // 분리 보컬은 프레임마다 옥타브가 튀기도 한다: 기대 음에서 ±6반음 안으로 모두 접는다
            const auto midi = curve.midiF0[t];
            voiced.push_back(expectedMidi > 0.0 ? midi - 12.0 * std::round((midi - expectedMidi) / 12.0) : midi);
        }
    if (voiced.size() < 3 || 2 * voiced.size() < last - first)
        return -1.0;
    const auto mid = voiced.begin() + static_cast<long>(voiced.size() / 2);
    std::nth_element(voiced.begin(), mid, voiced.end());
    return *mid;
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
// 자음 블록은 길이를 그대로, 짧은 틈은 원래 길이로: 앞 유성 블록의 끝을 옮겨 맞춘다 (앞이 자음이면 뒤 블록의 시작을 옮긴다)
void keepGapsRigid(std::vector<VocalBlock>& fitted, const std::vector<VocalBlock>& baseline)
{
    constexpr double shortGapSec = 0.25;
    const auto setEnd = [](VocalBlock& b, double end) { b.endOffsetSec = end - b.endSec; };
    const auto setStart = [](VocalBlock& b, double start) { b.startOffsetSec = start - b.startSec; };
    // 자음과 아주 짧은 유성 블록(늘일 모음이 거의 없다: 어택·클릭이 대부분)은 길이를 그대로 두고 옮기기만 한다
    constexpr double shortVoicedSec = 0.1;
    const auto rigid = [&](size_t i) { return baseline[i].isConsonant || baseline[i].displayEndSec() - baseline[i].displayStartSec() < shortVoicedSec; };
    for (size_t i = 0; i < fitted.size(); ++i)
        if (rigid(i))
            setEnd(fitted[i], fitted[i].displayStartSec() + (baseline[i].displayEndSec() - baseline[i].displayStartSec()));
    for (size_t i = 0; i + 1 < fitted.size(); ++i)
    {
        const auto gap = baseline[i + 1].displayStartSec() - baseline[i].displayEndSec();
        if (gap < 0.0 || gap > shortGapSec)
            continue;
        auto& a = fitted[i];
        auto& b = fitted[i + 1];
        // 맡는 유성 블록은 원래 길이의 0.8~1.25배 안에서만 (그 밖이면 틈이 조금 늘거나 준다)
        constexpr double shortest = 0.8, longest = 1.25;
        if (! rigid(i))
        {
            const auto length = baseline[i].displayEndSec() - baseline[i].displayStartSec();
            const auto lo = a.displayStartSec() + std::max(minBlockSeconds, shortest * length), hi = a.displayStartSec() + longest * length;
            setEnd(a, std::clamp(b.displayStartSec() - gap, lo, std::max(lo, hi)));
        }
        else if (! rigid(i + 1))
        {
            const auto length = baseline[i + 1].displayEndSec() - baseline[i + 1].displayStartSec();
            const auto lo = b.displayEndSec() - longest * length, hi = b.displayEndSec() - std::max(minBlockSeconds, shortest * length);
            setStart(b, std::clamp(a.displayEndSec() + gap, std::min(lo, hi), hi));
        }
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

// 반주 섞인 원곡: 뽑은 멜로디에서 근거가 충분한 블록만 옮긴다
void fitPitchToMelody(std::vector<VocalBlock>& fitted, const std::vector<VocalBlock>& baseline, double targetOffset, const TimeMap& map,
                      const MelodyTrack& melody, double referenceOffset, const TrackRoles& roles)
{
    const MelodyGate gate;
    for (size_t i = 0; i < fitted.size(); ++i)
    {
        auto& block = fitted[i];
        if (block.isConsonant)
            continue;
        block.transitionAfter = std::clamp(roles.smoothAmount, 0.0, 1.0);
        const auto centre = baseline[i].baseMidi + baseline[i].pitchOffset;
        const auto [start, end] = referenceSpan(baseline[i], targetOffset, map);
        const auto referenceMidi = melodyPitchOver(melody, start - referenceOffset, end - referenceOffset, gate);
        if (referenceMidi < 0.0 || std::abs(referenceMidi - centre) > gate.maxShiftSemitones)
            continue;
        block.pitchShift = snappedPitchShift(centre, referenceMidi, roles.relation, roles.pitchAmount);
    }
}

void fitPitch(std::vector<VocalBlock>& fitted, const std::vector<VocalBlock>& baseline, double targetOffset, const TimeMap& map,
              const std::vector<VocalBlock>& referenceBlocks, double referenceOffset, const TrackRoles& roles,
              const VocalFeatures* curve)
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
        // 곡선이 있으면 그 구간의 실제 음(중앙값)을 쓴다: 블록 경계가 달라 이웃 음이 섞이는 일을 막는다
        auto referenceMidi = curve != nullptr ? referenceCurvePitchOver(*curve, start - referenceOffset, end - referenceOffset, centre - interval) : -1.0;
        if (referenceMidi < 0.0)
            referenceMidi = referencePitchOver(referenceBlocks, referenceOffset, start, end, centre - interval);
        if (referenceMidi < 0.0)
            continue;
        // 귀에 안 들리는 차이(8센트 미만)는 옮기지 않는다: 원래 소리를 그대로 두는 편이 음질에 낫다
        const auto shift = snappedPitchShift(centre, referenceMidi, roles.relation, roles.pitchAmount);
        // 한도를 넘게 다르면 꺾임·장식음이 기준과 다르게 불린 것: 억지로 옮기면 소리가 깨진다
        const auto limit = std::clamp(roles.maxShiftSemitones, 0.0, maxFitShiftSemitones);
        if (std::abs(shift) >= 0.08 && std::abs(shift) <= limit + 1e-9 && ! isRunPassingNote(baseline, i))
            block.pitchShift = shift;
    }
}
} // namespace

namespace
{
bool isShortVoiced(const VocalBlock& b) { return ! b.isConsonant && b.endSec - b.startSec < runPassingNoteSec; }

// index에서 direction 쪽으로 가장 가까운 유성 블롭. 맞닿아 있지 않으면 nullopt
std::optional<size_t> touchingVoiced(const std::vector<VocalBlock>& blocks, size_t index, int direction)
{
    for (auto i = static_cast<long>(index) + direction; i >= 0 && i < static_cast<long>(blocks.size()); i += direction)
    {
        const auto& n = blocks[static_cast<size_t>(i)];
        if (n.isConsonant)
            continue;
        const auto& b = blocks[index];
        const auto gap = direction < 0 ? b.startSec - n.endSec : n.startSec - b.endSec;
        return gap < runNeighbourGapSec ? std::optional<size_t>(static_cast<size_t>(i)) : std::nullopt;
    }
    return std::nullopt;
}
} // namespace

bool isRunPassingNote(const std::vector<VocalBlock>& blocks, size_t index)
{
    return isShortVoiced(blocks[index]) && touchingVoiced(blocks, index, -1) && touchingVoiced(blocks, index, +1);
}

std::vector<BlockUpdate> fitToReference(const std::vector<VocalBlock>& baseline, double targetOffset, const TimeMap& map,
                                        const std::vector<VocalBlock>& referenceBlocks, double referenceOffset, const TrackRoles& roles,
                                        const MelodyTrack* melody, const VocalFeatures* referenceCurve)
{
    auto fitted = baseline;
    if (roles.timing && ! map.empty())
    {
        for (auto& block : fitted)
            block = withTimingFitted(block, targetOffset, map, roles.timingAmount);
        if (roles.rigidGaps)
            keepGapsRigid(fitted, baseline);
        repairOrder(fitted);
    }

    if (roles.pitch && melody != nullptr)
        fitPitchToMelody(fitted, baseline, targetOffset, map, *melody, referenceOffset, roles);
    else if (roles.pitch)
        fitPitch(fitted, baseline, targetOffset, map, referenceBlocks, referenceOffset, roles, referenceCurve);

    std::vector<BlockUpdate> updates;
    for (size_t i = 0; i < fitted.size(); ++i)
        updates.push_back({ i, std::move(fitted[i]) });
    return updates;
}
