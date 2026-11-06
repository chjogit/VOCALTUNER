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

// delta: 표시 프레임마다 (편집 음정 - 그 원본 프레임의 음정). 박자가 바뀐 프레임에서 목표 음정을 원본 음정 + delta로 다시 맞출 때 쓴다
void scatterBlockPitch(EditGrid& grid, std::vector<double>& delta, const VocalFeatures& f, const std::vector<VocalBlock>& blocks,
                       size_t index, double period)
{
    const auto& b = blocks[index];
    long previousIndex = -1;
    double previousMidi = 0.0, previousChange = 0.0;
    for (auto frame = b.beginFrame; frame < std::min(b.endFrame, f.size()); ++frame)
    {
        const auto midi = editedPitchAt(f, blocks, index, frame);
        if (midi <= 0.0)
            continue;
        const auto at = std::lround(curveFrameDisplaySec(f, b, frame) / period);
        if (at < 0 || static_cast<size_t>(at) >= grid.size())
            continue;
        const auto change = f.midiF0[frame] > 0.0 ? midi - f.midiF0[frame] : 0.0;
        if (previousIndex >= 0 && at - previousIndex > 1 && static_cast<size_t>(at - previousIndex) <= maxGapFrames)
            for (auto k = previousIndex + 1; k < at; ++k)
            {
                const auto x = static_cast<double>(k - previousIndex) / static_cast<double>(at - previousIndex);
                grid.targetMidi[static_cast<size_t>(k)] = previousMidi + (midi - previousMidi) * x;
                delta[static_cast<size_t>(k)] = previousChange + (change - previousChange) * x;
            }
        grid.targetMidi[static_cast<size_t>(at)] = midi;
        delta[static_cast<size_t>(at)] = change;
        previousIndex = at;
        previousMidi = midi;
        previousChange = change;
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

void fillBlockFormant(EditGrid& grid, const VocalBlock& b, double period)
{
    const auto amount = std::clamp(b.formantAmount, 0.0, 1.0);
    if (amount >= 1.0)
        return;
    const auto first = std::max(0L, std::lround(b.displayStartSec() / period));
    const auto last = std::min(static_cast<long>(grid.size()), std::lround(b.displayEndSec() / period));
    for (auto i = first; i < last; ++i)
        grid.formantAmount[static_cast<size_t>(i)] = amount;
    grid.formantEdited = grid.formantEdited || first < last;
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
    grid.formantAmount.assign(n, 1.0);
    if (n == 0)
        return grid;
    grid.lastSourceFrame = static_cast<int>(f.size()) - 1;
    grid.timeMap = std::make_shared<const EditTimeMap>(blocks);
    fillSourceFrames(grid, f, *grid.timeMap, period);
    for (size_t i = 0; i < grid.size() && ! grid.timingChanged; ++i)
        grid.timingChanged = grid.sourceFrame[i] != std::min(static_cast<int>(i), grid.lastSourceFrame);
    std::vector<double> delta(n, 0.0);
    for (size_t k = 0; k < blocks.size(); ++k)
    {
        scatterBlockPitch(grid, delta, f, blocks, k, period);
        fillBlockGain(grid, blocks, k, period);
        fillBlockFormant(grid, blocks[k], period);
    }
    // 박자가 바뀐 유성 프레임: 목표 음정을 '이 프레임이 실제로 가져오는 원본 프레임의 음정 + 편집량'으로.
    // 위에서 흩뿌린 값은 원본 프레임을 표시 자리로 반올림해 놓은 것이라, 가져오는 프레임(sourceFrame, 다른 반올림)과 한 칸씩 어긋나
    // 박자만 바꿔도 프레임마다 음정 변화(빠른 음 변화에서 100센트 넘게)를 요청했다 -> 블록 안의 계단 소리.
    // 박자가 그대로인 프레임은 건드리지 않는다 (박자 편집이 없으면 결과가 이전과 같다).
    for (size_t i = 0; i < n; ++i)
    {
        const auto j = static_cast<size_t>(grid.sourceFrame[i]);
        if (grid.sourceFrame[i] != std::min(static_cast<int>(i), grid.lastSourceFrame) && grid.targetMidi[i] > 0.0 && f.midiF0[j] > 0.0)
            grid.targetMidi[i] = f.midiF0[j] + delta[i];
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

