#include "EditGrid.h"
#include <algorithm>
#include <cmath>
#include "BlockTransforms.h"

namespace
{
constexpr size_t maxGapFrames = 8;

size_t frameCountFor(size_t samples, int sampleRate, double periodSec) noexcept
{
    return static_cast<size_t>(std::ceil(static_cast<double>(samples) / sampleRate / periodSec)) + 1;
}

void fillSourceFrames(EditGrid& grid, const VocalFeatures& f, const EditTimeMap& map, double period)
{
    const auto last = static_cast<int>(f.size()) - 1;
    for (size_t i = 0; i < grid.size(); ++i)
    {
        const auto j = static_cast<int>(std::lround(map.toSource(static_cast<double>(i) * period) / period));
        grid.sourceFrame[i] = std::clamp(j, 0, last);
        grid.targetMidi[i] = f.midiF0[static_cast<size_t>(grid.sourceFrame[i])];
        grid.originalMidi[i] = f.midiF0[std::min(i, f.size() - 1)];
    }
}

void scatterBlockPitch(EditGrid& grid, const VocalFeatures& f, const std::vector<VocalBlock>& blocks, size_t index, double period)
{
    const auto& b = blocks[index];
    long previousIndex = -1;
    double previousMidi = 0.0;
    for (auto frame = b.beginFrame; frame < std::min(b.endFrame, f.size()); ++frame)
    {
        const auto midi = editedPitchAt(f, blocks, index, frame);
        if (midi <= 0.0)
            continue;
        const auto at = std::lround(curveFrameDisplaySec(f, b, frame) / period);
        if (at < 0 || static_cast<size_t>(at) >= grid.size())
            continue;
        if (previousIndex >= 0 && at - previousIndex > 1 && static_cast<size_t>(at - previousIndex) <= maxGapFrames)
            for (auto k = previousIndex + 1; k < at; ++k)
                grid.targetMidi[static_cast<size_t>(k)] =
                    previousMidi + (midi - previousMidi) * static_cast<double>(k - previousIndex) / static_cast<double>(at - previousIndex);
        grid.targetMidi[static_cast<size_t>(at)] = midi;
        previousIndex = at;
        previousMidi = midi;
    }
}

void fillBlockGain(EditGrid& grid, const std::vector<VocalBlock>& blocks, size_t index, double period)
{
    const auto& b = blocks[index];
    const auto first = std::max(0L, std::lround(b.displayStartSec() / period));
    const auto last = std::min(static_cast<long>(grid.size()), std::lround(b.displayEndSec() / period));
    for (auto i = first; i < last; ++i)
        grid.gainDb[static_cast<size_t>(i)] = effectiveGainDb(blocks, index, static_cast<double>(i) * period);
}
} // namespace

EditGrid buildGrid(size_t samples, int sampleRate, const VocalFeatures& f, const std::vector<VocalBlock>& blocks)
{
    const auto period = f.framePeriodMs / 1000.0;
    EditGrid grid;
    const auto n = f.size() == 0 ? size_t{0} : frameCountFor(samples, sampleRate, period);
    grid.sourceFrame.assign(n, 0);
    grid.targetMidi.assign(n, 0.0);
    grid.originalMidi.assign(n, 0.0);
    grid.gainDb.assign(n, 0.0);
    if (n == 0)
        return grid;
    grid.lastSourceFrame = static_cast<int>(f.size()) - 1;
    grid.timeMap = std::make_shared<const EditTimeMap>(blocks);
    fillSourceFrames(grid, f, *grid.timeMap, period);
    for (size_t i = 0; i < grid.size() && ! grid.timingChanged; ++i)
        grid.timingChanged = grid.sourceFrame[i] != std::min(static_cast<int>(i), grid.lastSourceFrame);
    for (size_t k = 0; k < blocks.size(); ++k)
    {
        scatterBlockPitch(grid, f, blocks, k, period);
        fillBlockGain(grid, blocks, k, period);
    }
    return grid;
}

// 원래 무성이던 프레임은 음정을 따지지 않는다: 블록 안 짧은 틈을 이어 둔 음정 때문에 편집 없는 구간까지 다시 합성하던 문제
bool frameChanges(const EditGrid& g, size_t i, double tolerance) noexcept
{
    if (g.sourceFrame[i] != std::min(static_cast<int>(i), g.lastSourceFrame))
        return true;
    return g.originalMidi[i] > 0.0 && std::abs(g.targetMidi[i] - g.originalMidi[i]) > tolerance;
}

bool rangeChanges(const EditGrid& g, size_t begin, size_t end, double tolerance) noexcept
{
    for (auto i = begin; i < end; ++i)
        if (frameChanges(g, i, tolerance))
            return true;
    return false;
}

