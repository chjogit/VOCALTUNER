#include "BlockEditing.h"
#include "BlockTransforms.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
bool hasVoicedFrames(const VocalFeatures& f, size_t begin, size_t end)
{
    return std::any_of(f.midiF0.begin() + static_cast<long>(begin), f.midiF0.begin() + static_cast<long>(end),
                       [](double midi) { return midi > 0.0; });
}

double medianVoicedMidi(const VocalFeatures& f, size_t begin, size_t end)
{
    std::vector<double> voiced;
    for (auto i = begin; i < end; ++i)
        if (f.midiF0[i] > 0.0)
            voiced.push_back(f.midiF0[i]);
    std::sort(voiced.begin(), voiced.end());
    const auto mid = voiced.size() / 2;
    return voiced.size() % 2 == 1 ? voiced[mid] : 0.5 * (voiced[mid - 1] + voiced[mid]);
}

// 구간과 피치를 features에서 다시 계산
void refreshBlock(VocalBlock& block, const VocalFeatures& f, size_t begin, size_t end)
{
    block.beginFrame = begin;
    block.endFrame = end;
    block.startSec = f.timeSec[begin];
    block.endSec = f.timeSec[end - 1] + f.framePeriodMs / 1000.0;
    const auto median = medianVoicedMidi(f, begin, end);
    block.baseMidi = static_cast<int>(std::lround(median));
    block.pitchOffset = median - block.baseMidi;
}

// 자음 블록은 붙어 있는 모음 블록의 음을 따른다 (파서와 같은 규칙)
void normalizeAfterEdit(std::vector<VocalBlock>& blocks)
{
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        blocks[i].id = static_cast<int>(i);
        if (! blocks[i].isConsonant)
            continue;
        const auto hasNext = i + 1 < blocks.size() && ! blocks[i + 1].isConsonant;
        if (hasNext && blocks[i + 1].beginFrame == blocks[i].endFrame)
            blocks[i].baseMidi = blocks[i + 1].baseMidi;
        else if (i > 0 && ! blocks[i - 1].isConsonant)
            blocks[i].baseMidi = blocks[i - 1].baseMidi;
    }
}

// 프레임별 편집 배열을 자른 위치(split)에서 둘로 나누거나, 합칠 때 이어 붙임 (비어 있으면 비어 있는 채로)
void splitPerFrame(const std::vector<float>& source, size_t count, size_t at, std::vector<float>& left, std::vector<float>& right)
{
    left.clear();
    right.clear();
    if (source.size() != count)
        return;
    left.assign(source.begin(), source.begin() + static_cast<long>(at));
    right.assign(source.begin() + static_cast<long>(at), source.end());
}

std::vector<float> joinPerFrame(const std::vector<float>& a, size_t aBegin, size_t aEnd, const std::vector<float>& b,
                                size_t bBegin, size_t bEnd)
{
    if (a.empty() && b.empty())
        return {};
    std::vector<float> out(bEnd - aBegin, 0.0f);
    if (a.size() == aEnd - aBegin)
        std::copy(a.begin(), a.end(), out.begin());
    if (b.size() == bEnd - bBegin)
        std::copy(b.begin(), b.end(), out.begin() + static_cast<long>(bBegin - aBegin));
    return out;
}

bool isValidFeatures(const VocalFeatures& f)
{
    return f.size() > 0 && f.midiF0.size() == f.size();
}
} // namespace

bool splitBlockAt(std::vector<VocalBlock>& blocks, const VocalFeatures& features, size_t frame,
                  const EditOptions& options)
{
    if (! isValidFeatures(features))
        return false;

    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto block = blocks[i];
        if (block.isConsonant || frame <= block.beginFrame || frame >= block.endFrame || block.endFrame > features.size())
            continue;
        if (frame - block.beginFrame < options.minPieceFrames || block.endFrame - frame < options.minPieceFrames)
            return false;
        if (! hasVoicedFrames(features, block.beginFrame, frame) || ! hasVoicedFrames(features, frame, block.endFrame))
            return false;

        // 자른 지점의 표시 시각을 편집 전에 구해 두면 양쪽 조각의 타이밍 편집이 이어진다
        const auto cutDisplaySec = sourceToDisplaySec(block, features.timeSec[frame]);
        VocalBlock right = block;
        right.isContinuation = true;
        right.lyric.clear();
        refreshBlock(blocks[i], features, block.beginFrame, frame);
        refreshBlock(right, features, frame, block.endFrame);
        blocks[i].endOffsetSec = cutDisplaySec - blocks[i].endSec;
        right.startOffsetSec = cutDisplaySec - right.startSec;
        const auto count = block.endFrame - block.beginFrame;
        splitPerFrame(block.curveOffset, count, frame - block.beginFrame, blocks[i].curveOffset, right.curveOffset);
        splitPerFrame(block.curveWarp, count, frame - block.beginFrame, blocks[i].curveWarp, right.curveWarp);
        blocks[i].userHandles.clear();
        right.userHandles.clear();
        for (const auto handle : block.userHandles)
            (handle < frame ? blocks[i] : right).userHandles.push_back(handle);
        blocks[i].transitionAfter = 0.5;   // 새로 생긴 경계의 기본 이음 정도
        blocks.insert(blocks.begin() + static_cast<long>(i) + 1, std::move(right));
        normalizeAfterEdit(blocks);
        return true;
    }
    return false;
}

std::optional<size_t> findJointNear(const std::vector<VocalBlock>& blocks, size_t frame, size_t toleranceFrames,
                                    const EditOptions& options)
{
    std::optional<size_t> best;
    double bestDistance = 0.0;
    for (size_t i = 0; i + 1 < blocks.size(); ++i)
    {
        const auto& left = blocks[i];
        const auto& right = blocks[i + 1];
        if (left.isConsonant || right.isConsonant || right.beginFrame < left.endFrame
            || right.beginFrame - left.endFrame > options.maxJoinGapFrames)
            continue;

        const auto joint = 0.5 * static_cast<double>(left.endFrame + right.beginFrame);
        const auto distance = std::abs(joint - static_cast<double>(frame));
        if (distance <= static_cast<double>(toleranceFrames) && (! best || distance < bestDistance))
        {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

bool joinBlocksAt(std::vector<VocalBlock>& blocks, const VocalFeatures& features, size_t leftIndex,
                  const EditOptions& options)
{
    if (! isValidFeatures(features) || leftIndex + 1 >= blocks.size())
        return false;

    const auto& left = blocks[leftIndex];
    const auto& right = blocks[leftIndex + 1];
    if (left.isConsonant || right.isConsonant || right.beginFrame < left.endFrame
        || right.beginFrame - left.endFrame > options.maxJoinGapFrames || right.endFrame > features.size())
        return false;

    const auto leftFrames = static_cast<double>(left.endFrame - left.beginFrame);
    const auto rightFrames = static_cast<double>(right.endFrame - right.beginFrame);
    const auto weight = leftFrames / (leftFrames + rightFrames);

    auto curve = joinPerFrame(left.curveOffset, left.beginFrame, left.endFrame, right.curveOffset, right.beginFrame, right.endFrame);
    auto warp = joinPerFrame(left.curveWarp, left.beginFrame, left.endFrame, right.curveWarp, right.beginFrame, right.endFrame);
    auto handles = left.userHandles;
    handles.insert(handles.end(), right.userHandles.begin(), right.userHandles.end());
    const auto mergedTransition = right.transitionAfter;

    const auto merged = blocks[leftIndex];
    const auto mergedShift = weight * left.pitchShift + (1.0 - weight) * right.pitchShift;
    const auto mergedGain = weight * left.gainDb + (1.0 - weight) * right.gainDb;
    const auto mergedDepth = weight * left.vibratoDepth + (1.0 - weight) * right.vibratoDepth;
    const auto mergedRate = weight * left.vibratoRateHz + (1.0 - weight) * right.vibratoRateHz;
    const auto mergedFlatten = weight * left.flattenAmount + (1.0 - weight) * right.flattenAmount;
    const auto mergedEndOffset = right.endOffsetSec;
    refreshBlock(blocks[leftIndex], features, merged.beginFrame, right.endFrame);
    blocks[leftIndex].pitchShift = mergedShift;
    blocks[leftIndex].gainDb = mergedGain;
    blocks[leftIndex].vibratoDepth = mergedDepth;
    blocks[leftIndex].flattenAmount = mergedFlatten;
    blocks[leftIndex].vibratoRateHz = mergedRate;
    blocks[leftIndex].endOffsetSec = mergedEndOffset;
    blocks[leftIndex].curveOffset = std::move(curve);
    blocks[leftIndex].curveWarp = std::move(warp);
    blocks[leftIndex].userHandles = std::move(handles);
    blocks[leftIndex].transitionAfter = mergedTransition;
    blocks.erase(blocks.begin() + static_cast<long>(leftIndex) + 1);
    normalizeAfterEdit(blocks);
    return true;
}

BlockEdit editBlocksAt(std::vector<VocalBlock>& blocks, const VocalFeatures& features, size_t frame,
                       size_t toleranceFrames, const EditOptions& options)
{
    if (const auto joint = findJointNear(blocks, frame, toleranceFrames, options))
        return joinBlocksAt(blocks, features, *joint, options) ? BlockEdit::Join : BlockEdit::None;
    return splitBlockAt(blocks, features, frame, options) ? BlockEdit::Split : BlockEdit::None;
}

std::optional<size_t> findJointNearDisplayTime(const std::vector<VocalBlock>& blocks, double displaySec, double toleranceSec,
                                               const EditOptions& options)
{
    std::optional<size_t> best;
    double bestDistance = 0.0;
    for (size_t i = 0; i + 1 < blocks.size(); ++i)
    {
        const auto& left = blocks[i];
        const auto& right = blocks[i + 1];
        if (left.isConsonant || right.isConsonant || right.beginFrame < left.endFrame
            || right.beginFrame - left.endFrame > options.maxJoinGapFrames)
            continue;
        const auto distance = std::abs(0.5 * (left.displayEndSec() + right.displayStartSec()) - displaySec);
        if (distance <= toleranceSec && (! best || distance < bestDistance))
        {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

BlockEdit editBlocksAtDisplayTime(std::vector<VocalBlock>& blocks, const VocalFeatures& features, double displaySec,
                                  double toleranceSec, const EditOptions& options)
{
    if (! isValidFeatures(features))
        return BlockEdit::None;

    const auto joint = findJointNearDisplayTime(blocks, displaySec, toleranceSec, options);
    if (joint)
        return joinBlocksAt(blocks, features, *joint, options) ? BlockEdit::Join : BlockEdit::None;

    for (const auto& block : blocks)
    {
        if (block.isConsonant || displaySec <= block.displayStartSec() || displaySec >= block.displayEndSec())
            continue;
        const auto sourceSec = displayToSourceSec(block, displaySec);
        const auto frame = std::round((sourceSec - features.timeSec.front()) * 1000.0 / features.framePeriodMs);
        return frame > 0.0 && splitBlockAt(blocks, features, static_cast<size_t>(frame), options) ? BlockEdit::Split : BlockEdit::None;
    }
    return BlockEdit::None;
}
