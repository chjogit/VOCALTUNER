#include "BlockHistory.h"
#include <tuple>

namespace
{
template <class Block>
auto editFields(Block& b)
{
    return std::tie(b.pitchShift, b.gainDb, b.startOffsetSec, b.endOffsetSec, b.curveOffset, b.curveWarp, b.userHandles,
                    b.transitionAfter, b.vibratoDepth, b.vibratoRateHz, b.flattenAmount, b.tidyAmount, b.driftAmount,
                    b.centerTargetMidi);
}
} // namespace

bool sameEdits(const VocalBlock& a, const VocalBlock& b)
{
    return editFields(a) == editFields(b);
}

VocalBlock withEditsOf(const VocalBlock& structure, const VocalBlock& edits)
{
    auto result = structure;
    editFields(result) = editFields(edits);
    return result;
}

const VocalBlock* findSameBlock(const std::vector<VocalBlock>& blocks, const VocalBlock& block)
{
    for (const auto& b : blocks)
        if (b.beginFrame == block.beginFrame && b.endFrame == block.endFrame)
            return &b;
    return nullptr;
}

VocalBlock firstStateOf(const VocalBlock& current, const std::vector<VocalBlock>& original, double baseTidy)
{
    const auto* first = findSameBlock(original, current);
    auto result = withEditsOf(current, first != nullptr ? *first : VocalBlock {});
    result.tidyAmount = baseTidy;
    return result;
}

std::optional<VocalBlock> previousStateOf(const VocalBlock& current, const std::vector<BlockSnapshot>& undoStack)
{
    for (auto it = undoStack.rbegin(); it != undoStack.rend(); ++it)
    {
        if (*it == nullptr)
            continue;
        const auto* earlier = findSameBlock(**it, current);
        if (earlier == nullptr)
            return std::nullopt;    // 이 블록이 생기기 전 (자르기 이전)까지 왔다
        if (! sameEdits(*earlier, current))
            return withEditsOf(current, *earlier);
    }
    return std::nullopt;
}
