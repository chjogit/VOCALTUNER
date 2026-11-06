#include "AlignmentController.h"
#include "BlockHistory.h"
#include "Alignment/GuidedTiming.h"
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

int AlignmentController::referenceSlot(const juce::ARAPlaybackRegion* region) const
{
    if (region == nullptr)
        return 0;
    const auto found = referenceSlots.find(trackOf(region));
    return found != referenceSlots.end() ? found->second : 0;
}

void AlignmentController::setReferenceSlot(const juce::ARAPlaybackRegion* region, int slot)
{
    const auto key = trackOf(region);
    for (auto it = referenceSlots.begin(); it != referenceSlots.end();)
        it = it->first == key || it->second == slot ? referenceSlots.erase(it) : std::next(it);
    if (key != nullptr && slot > 0)
        referenceSlots[key] = slot;
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
    for (auto it = referenceSlots.begin(); it != referenceSlots.end();)
        it = isLiveTrack(it->first) ? std::next(it) : referenceSlots.erase(it);
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
        if (! isReference(r))
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
    if (referenceSlots.empty())
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
    if (const auto features = analysis.getFeatures(sourceOf(p.reference)))
        p.referenceIsMix = looksLikeMix(features->rms);
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

AlignmentController::AlignPlan AlignmentController::planFor(const juce::ARAPlaybackRegion* target, const Prepared& p, bool withMelody) const
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
    plan.referenceIsMix = p.referenceIsMix;
    plan.referenceOffset = offsetOf(p.reference);
    plan.targetOffset = offsetOf(target);
    if (withMelody)
    {
        plan.referenceReader = std::make_shared<AnalysisTask>(*sourceOf(p.reference), false);
        plan.targetFeatures = analysis.getFeatures(sourceOf(target));
        plan.referenceFeatures = analysis.getFeatures(sourceOf(p.reference));
    }
    plan.phraseTiming = phraseTiming;
    return plan;
}

AlignmentController::Computed AlignmentController::runPlan(AlignPlan& plan)
{
    if (! plan.guide.empty())
        plan.options.guide = &plan.guide;
    Computed out { alignToReference(plan.reference, plan.target, plan.options), nullptr };
    if (! out.result.ok)
        return out;
    // 소리 모양 정렬(DTW)은 반주·번짐·발음 차이까지 맞추려 들어 박자를 0.1~1초씩 엉뚱하게 민다 (분리 보컬 기준에서
    // 블록 94%를 늘이고 줄임). 박자는 따라 부른 음으로 찾고(확실한 곳만), 그럴 수 없는 동기 맞추기에서는 옮기지 않는다
    out.result.targetToReference = {};
    if (plan.referenceReader != nullptr && plan.targetFeatures != nullptr)
    {
        auto& reader = *plan.referenceReader;
        const StereoReader read = [&reader](int64_t start, int count, float* l, float* r) { reader.readStereo(start, count, l, r); };
        const auto cancelled = [&plan] { return plan.cancelled && plan.cancelled(); };
        GuidedTimingOptions timing;
        timing.shouldCancel = cancelled;
        out.timingFound = true;
        if (plan.phraseTiming && ! plan.referenceIsMix && plan.referenceFeatures != nullptr)
        {
            PhraseTimingOptions phrase;
            phrase.shouldCancel = cancelled;
            out.result.targetToReference = phraseTimeMap(*plan.referenceFeatures, plan.referenceOffset, *plan.targetFeatures,
                                                         plan.targetOffset, phrase);
        }
        else
            out.result.targetToReference = guidedTimeMap(reader.getNumSamples(), reader.getSampleRate(), read, *plan.targetFeatures,
                                                         plan.targetOffset, plan.referenceOffset, timing);
        if (plan.referenceIsMix)
            out.melody = std::make_shared<const MelodyTrack>(melodyForTarget(reader.getNumSamples(), reader.getSampleRate(), read, *plan.targetFeatures,
                                                                         plan.targetOffset, out.result.targetToReference, plan.referenceOffset, cancelled));
    }
    return out;
}

void AlignmentController::storeMap(const juce::ARAPlaybackRegion* target, const Prepared& p, size_t anchorCount, Computed computed)
{
    cache[target] = { p.reference, p.referenceData, p.targetData, offsetOf(p.reference), offsetOf(target),
                      std::move(computed.result), anchorCount, std::move(computed.melody), computed.timingFound };
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
        auto plan = planFor(target, p, false);   // 멜로디 추출은 백그라운드 맞추기에서만 (여기선 음정을 옮기지 않음)
        storeMap(target, p, plan.anchorCount, runPlan(plan));
    }
    return apply(target, p, newBaseline, beginGesture);
}

// "박자 -46ms · 음정 +15¢": 유성 블록이 평균 얼마나 움직였는지 (앞당김 = 음수)
juce::String AlignmentController::changeSummary(const std::vector<VocalBlock>& before, const std::vector<BlockUpdate>& updates)
{
    double timing = 0.0, cents = 0.0;
    int count = 0;
    for (const auto& u : updates)
    {
        if (u.index >= before.size() || u.block.isConsonant)
            continue;
        timing += u.block.displayStartSec() - before[u.index].displayStartSec();
        cents += 100.0 * (u.block.displayCenterMidi() - before[u.index].displayCenterMidi());
        ++count;
    }
    const auto n = juce::jmax(1, count);
    const auto sign = [](int v) { return (v > 0 ? "+" : "") + juce::String(v); };
    return u8("박자 ") + sign(juce::roundToInt(1000.0 * timing / n)) + "ms" + u8(" · 음정 ") + sign(juce::roundToInt(cents / n)) + juce::String::fromUTF8("¢");
}

namespace
{
int movedBlocks(const std::vector<VocalBlock>& before, const std::vector<BlockUpdate>& updates)
{
    return static_cast<int>(std::count_if(updates.begin(), updates.end(), [&before](const BlockUpdate& u)
    {
        return u.index < before.size() && std::abs(u.block.pitchShift - before[u.index].pitchShift) > 0.01;
    }));
}
}

namespace
{
// 맞추기의 출발점: 지금 블록들을 처음 분석 상태로 되돌린 것 (여러 번 눌러도 맞춤이 겹쳐 쌓이지 않는다)
std::shared_ptr<const std::vector<VocalBlock>> originalStateOf(const std::vector<VocalBlock>& current, const BlockSnapshot& original,
                                                               bool baseCorrection)
{
    if (original == nullptr)
        return std::make_shared<const std::vector<VocalBlock>>(current);
    const auto baseTidy = baseCorrection ? defaultTidyAmount : 0.0;
    std::vector<VocalBlock> out;
    out.reserve(current.size());
    for (const auto& block : current)
        out.push_back(firstStateOf(block, *original, baseTidy));
    return std::make_shared<const std::vector<VocalBlock>>(std::move(out));
}
} // namespace

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
        baseline = originalStateOf(*targetBlocks, analysis.getOriginalBlocks(targetSource), settings(target).baseCorrection);

    static const MelodyTrack noMelody;
    const auto& melody = cache[target].melody;
    const auto* melodyToUse = p.referenceIsMix ? (melody != nullptr ? melody.get() : &noMelody) : nullptr;
    const auto referenceCurve = p.referenceIsMix ? nullptr : analysis.getFeatures(sourceOf(p.reference));
    const auto updates = fitToReference(*baseline, offsetOf(target), aligned->targetToReference, *referenceBlocks,
                                        offsetOf(p.reference), settings(target).roles, melodyToUse, referenceCurve.get());
    if (beginGesture)
    {
        if (onBeforeApply)
            onBeforeApply(targetSource);
        analysis.beginEditGesture(targetSource);
    }
    if (! analysis.replaceBlocks(targetSource, updates))
        return { false, u8("블록을 바꾸지 못했습니다") };
    const auto anchorCount = cache[target].anchorCount;
    auto how = anchorCount > 0 ? u8(" · 가사 앵커 ") + juce::String((int) anchorCount) + u8("개")
                               : (p.referenceData->whisperUsed ? u8(" · 가사 겹침 부족") : u8(" · 소리 모양으로 맞춤"));
    if (p.referenceIsMix)
        how << (melody != nullptr ? u8(" · 원곡 음원: 확실한 음만 ") + juce::String(movedBlocks(*baseline, updates)) + u8("곳 맞춤")
                                  : u8(" · 원곡 음원: 음정은 ‘맞추기’로 다시 맞추세요"));
    return { true, u8("맞춤 완료 · ") + changeSummary(*targetBlocks, updates) + how };
}
