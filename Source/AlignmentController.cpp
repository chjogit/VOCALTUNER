#include "AlignmentController.h"
#include <algorithm>

namespace
{
juce::String u8(const char* text) { return juce::String::fromUTF8(text); }
}

double AlignmentController::offsetOf(const juce::ARAPlaybackRegion* region) noexcept
{
    return region->getStartInPlaybackTime() - region->getStartInAudioModificationTime();
}

juce::ARAAudioSource* AlignmentController::sourceOf(const juce::ARAPlaybackRegion* region) noexcept
{
    return region != nullptr ? region->getAudioModification()->getAudioSource() : nullptr;
}

AlignmentController::TrackKey AlignmentController::trackOf(const juce::ARAPlaybackRegion* region) noexcept
{
    if (region == nullptr)
        return nullptr;
    if (const auto* sequence = region->getRegionSequence<juce::ARARegionSequence>())
        return sequence;
    return region;
}

TrackSettings& AlignmentController::settings(const juce::ARAPlaybackRegion* region)
{
    return tracks[trackOf(region)];
}

void AlignmentController::setReference(const juce::ARAPlaybackRegion* region)
{
    referenceTrack = trackOf(region);
    cache.clear();
}

void AlignmentController::prune(const std::vector<const juce::ARAPlaybackRegion*>& liveRegions)
{
    live = liveRegions;
    const auto isLiveTrack = [this](TrackKey key)
    {
        return std::any_of(live.begin(), live.end(), [key](const juce::ARAPlaybackRegion* r) { return trackOf(r) == key; });
    };
    for (auto it = tracks.begin(); it != tracks.end();)
        it = isLiveTrack(it->first) ? std::next(it) : tracks.erase(it);
    for (auto it = baselines.begin(); it != baselines.end();)
        it = isLive(it->first) ? std::next(it) : baselines.erase(it);
    for (auto it = cache.begin(); it != cache.end();)
        it = isLive(it->first) ? std::next(it) : cache.erase(it);
    if (referenceTrack != nullptr && ! isLiveTrack(referenceTrack))
        setReference(nullptr);
}

bool AlignmentController::isLive(const juce::ARAPlaybackRegion* r) const
{
    return std::find(live.begin(), live.end(), r) != live.end();
}

const juce::ARAPlaybackRegion* AlignmentController::referenceFor(const juce::ARAPlaybackRegion* target) const
{
    const juce::ARAPlaybackRegion* best = nullptr;
    double bestOverlap = 0.0;
    for (const auto* r : live)
    {
        if (trackOf(r) != referenceTrack)
            continue;
        const auto overlap = std::min(r->getEndInPlaybackTime(), target->getEndInPlaybackTime())
                           - std::max(r->getStartInPlaybackTime(), target->getStartInPlaybackTime());
        if (overlap > bestOverlap)
        {
            best = r;
            bestOverlap = overlap;
        }
    }
    return best;
}

namespace
{
std::vector<TimedWord> inSongTime(const std::vector<TimedWord>& words, double offset)
{
    auto out = words;
    for (auto& w : out)
    {
        w.startSec += offset;
        w.endSec += offset;
    }
    return out;
}
}

std::vector<WordAnchor> AlignmentController::anchorsFor(const AlignmentData& referenceData, double referenceOffset,
                                                        const AlignmentData& targetData, double targetOffset)
{
    if (referenceData.words.empty() || targetData.words.empty())
        return {};
    return matchWords(inSongTime(referenceData.words, referenceOffset), inSongTime(targetData.words, targetOffset));
}

AlignmentController::Prepared AlignmentController::prepare(const juce::ARAPlaybackRegion* target) const
{
    Prepared p;
    if (target == nullptr)
        return p.fail(u8("맞출 트랙이 없습니다"));
    if (referenceTrack == nullptr)
        return p.fail(u8("먼저 ‘기준’ 트랙을 지정하세요"));
    if (isReference(target))
        return p.fail(u8("기준 트랙은 맞출 수 없습니다"));
    p.reference = referenceFor(target);
    if (p.reference == nullptr)
        return p.fail(u8("이 클립과 겹치는 기준 트랙 클립이 없습니다"));
    const auto found = tracks.find(trackOf(target));
    if (found == tracks.end() || (! found->second.roles.timing && ! found->second.roles.pitch))
        return p.fail(u8("박자 또는 음정 역할을 켜세요"));

    p.targetData = analysis.getAlignment(sourceOf(target));
    p.referenceData = analysis.getAlignment(sourceOf(p.reference));
    p.ready = p.targetData && p.referenceData && analysis.getBlocks(sourceOf(target)) && analysis.getBlocks(sourceOf(p.reference));
    p.ok = true;
    return p;
}

const AlignResult* AlignmentController::cachedMap(const juce::ARAPlaybackRegion* target, const Prepared& p) const
{
    const auto found = cache.find(target);
    if (found == cache.end())
        return nullptr;
    const auto& c = found->second;
    const auto valid = c.reference == p.reference && c.referenceData == p.referenceData && c.targetData == p.targetData
                    && juce::approximatelyEqual(c.referenceOffset, offsetOf(p.reference))
                    && juce::approximatelyEqual(c.targetOffset, offsetOf(target)) && c.result.ok;
    return valid ? &c.result : nullptr;
}

AlignmentController::AlignPlan AlignmentController::planFor(const juce::ARAPlaybackRegion* target, const Prepared& p)
{
    AlignPlan plan;
    plan.referenceData = p.referenceData;
    plan.targetData = p.targetData;
    plan.reference = { &p.referenceData->frames, &p.referenceData->kinds, offsetOf(p.reference) };
    plan.target = { &p.targetData->frames, &p.targetData->kinds, offsetOf(target) };
    const auto anchors = anchorsFor(*p.referenceData, offsetOf(p.reference), *p.targetData, offsetOf(target));
    plan.guide = guideFromAnchors(anchors);
    plan.anchorCount = plan.guide.empty() ? 0 : anchors.size();
    if (! plan.guide.empty())
        plan.options.maxShiftSec = 0.4;      // 가사로 대략 맞춰 두었으므로 좁게 탐색
    return plan;
}

AlignResult AlignmentController::runPlan(AlignPlan& plan)
{
    if (! plan.guide.empty())
        plan.options.guide = &plan.guide;
    return alignToReference(plan.reference, plan.target, plan.options);
}

void AlignmentController::storeMap(const juce::ARAPlaybackRegion* target, const Prepared& p, size_t anchorCount, AlignResult result)
{
    cache[target] = { p.reference, p.referenceData, p.targetData, offsetOf(p.reference), offsetOf(target), std::move(result), anchorCount };
}

AlignmentController::Outcome AlignmentController::fit(const juce::ARAPlaybackRegion* target, bool newBaseline, bool beginGesture)
{
    const auto p = prepare(target);
    if (! p.ok)
        return { false, p.message };
    if (! p.ready)
        return { false, u8("발음·호흡 분석이 아직 끝나지 않았습니다") };
    if (cachedMap(target, p) == nullptr)
    {
        auto plan = planFor(target, p);
        storeMap(target, p, plan.anchorCount, runPlan(plan));
    }
    return apply(target, p, newBaseline, beginGesture);
}

AlignmentController::Outcome AlignmentController::apply(const juce::ARAPlaybackRegion* target, const Prepared& p, bool newBaseline, bool beginGesture)
{
    const auto* aligned = cachedMap(target, p);
    if (aligned == nullptr)
        return { false, u8("두 트랙이 겹치는 구간에서 정렬하지 못했습니다") };

    auto* targetSource = sourceOf(target);
    const auto targetBlocks = analysis.getBlocks(targetSource);
    const auto referenceBlocks = analysis.getBlocks(sourceOf(p.reference));
    if (! targetBlocks || ! referenceBlocks)
        return { false, u8("발음·호흡 분석이 아직 끝나지 않았습니다") };
    auto& baseline = baselines[target];
    if (newBaseline || baseline == nullptr)
        baseline = targetBlocks;

    const auto updates = fitToReference(*baseline, offsetOf(target), aligned->targetToReference, *referenceBlocks,
                                        offsetOf(p.reference), settings(target).roles);
    if (beginGesture)
    {
        if (onBeforeApply)
            onBeforeApply(targetSource);
        analysis.beginEditGesture(targetSource);
    }
    if (! analysis.replaceBlocks(targetSource, updates))
        return { false, u8("블록을 바꾸지 못했습니다") };
    const auto anchorCount = cache[target].anchorCount;
    const auto how = anchorCount > 0 ? u8(", 가사 앵커 ") + juce::String((int) anchorCount) + u8("개")
                                     : (p.referenceData->whisperUsed ? u8(", 가사 겹침 부족") : u8(", Whisper 없음: 소리 모양만 사용"));
    return { true, u8("맞춤 완료 (평균 차이 ") + juce::String(aligned->meanCost, 2) + how + ")" };
}
