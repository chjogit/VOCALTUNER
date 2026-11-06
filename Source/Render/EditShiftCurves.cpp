#include "EditShiftCurves.h"
#include <algorithm>
#include <cmath>
#include "ConsonantShield.h"
#include "VowelWeight.h"
#include "PsolaGrain.h"

// 합성기가 구간을 앞뒤 무성 틈까지 넓혀 계산하므로, 이동량도 같은 만큼 구해 둔다.
// 구간 밖 이동량이 0이면 어느 구간으로 렌더하느냐에 따라 음정과 위상이 달라져 이어 붙이는 곳이 어긋난다.
ChunkSpan spanToQuiet(const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span)
{
    const auto quiet = 2 * psola::activeDilateFrames + 4;
    const auto limit = static_cast<size_t>(psola::maxExpandSec / (f.framePeriodMs / 1000.0));
    const auto voiced = [&](size_t i) { return f.midiF0[static_cast<size_t>(grid.sourceFrame[i])] > 0.0; };
    ChunkSpan wide = span;
    for (size_t run = 0, walked = 0; wide.begin > 0 && run < quiet && walked < limit; --wide.begin, ++walked)
        run = voiced(wide.begin - 1) ? 0 : run + 1;
    for (size_t run = 0, walked = 0; wide.end < grid.size() && run < quiet && walked < limit; ++wide.end, ++walked)
        run = voiced(wide.end) ? 0 : run + 1;
    return wide;
}

// 프레임마다 옮길 반음 (원본 프레임 기준). 원래 무성이거나 tolerance 이하면 0
std::vector<double> psolaShiftCurve(const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span, double tolerance)
{
    std::vector<double> shift(f.size(), 0.0);
    for (auto i = span.begin; i < std::min(span.end, f.size()); ++i)
    {
        const auto d = grid.targetMidi[i] - grid.originalMidi[i];
        if (grid.originalMidi[i] > 0.0 && grid.targetMidi[i] > 0.0 && std::abs(d) > tolerance)
            shift[i] = d;
    }
    return shift;
}

// 프레임마다 포먼트를 원본 자리에 두는 정도 (원본 프레임 기준, 박자 편집이 없을 때 표시 프레임 = 원본 프레임). 편집이 없으면 비어 있다
std::vector<double> psolaFormantCurve(const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span)
{
    if (! grid.formantEdited)
        return {};
    std::vector<double> amount(f.size(), 1.0);
    for (auto i = span.begin; i < std::min({ span.end, f.size(), grid.size() }); ++i)
        amount[i] = grid.formantAmount[i];
    return amount;
}

// 원본 프레임 기준 이동량에 자음 보호(와 vowelFocus)를 적용
std::vector<double> shieldedBySource(const AudioPlanes& src, const VocalFeatures& f, const EditGrid& grid, const std::vector<double>& bySource,
                                     size_t lo, size_t hi)
{
    const auto period = f.framePeriodMs / 1000.0;
    auto shielded = shieldedShift(src.channels[0].data(), src.channels[0].size(), src.sampleRate, period, bySource, lo, hi);
    if (grid.vowelFocus)
        applyVowelFocus(shielded, src.channels[0].data(), src.channels[0].size(), src.sampleRate, period, f.midiF0.data(), f.size(), lo, hi);
    return shielded;
}

// 박자가 바뀐 구간의 이동량 (출력 프레임 기준). 원본 음정은 가져오는 원본 프레임의 것. 자음 보호는 원본 시간 쪽에서 판단한다
std::vector<double> warpShiftCurve(const AudioPlanes& src, const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span, double tolerance)
{
    std::vector<double> out(grid.size(), 0.0);
    std::vector<double> bySource(f.size(), 0.0);
    size_t lo = f.size(), hi = 0;
    for (auto i = span.begin; i < span.end; ++i)
    {
        const auto j = static_cast<size_t>(grid.sourceFrame[i]);
        const auto d = grid.targetMidi[i] - f.midiF0[j];
        if (f.midiF0[j] > 0.0 && grid.targetMidi[i] > 0.0 && std::abs(d) > tolerance)
        {
            out[i] = d;
            bySource[j] = d;
        }
        lo = std::min(lo, j);
        hi = std::max(hi, j + 1);
    }
    const auto shielded = shieldedBySource(src, f, grid, bySource, lo, hi);
    for (auto i = span.begin; i < span.end; ++i)
    {
        const auto j = static_cast<size_t>(grid.sourceFrame[i]);
        if (bySource[j] != 0.0)
            out[i] *= shielded[j] / bySource[j];
    }
    return out;
}
