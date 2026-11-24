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

namespace
{
constexpr size_t passingFrames = 30;  // 앞뒤 150 ms: 모음 안에서 이보다 짧게 0 근처에 머무는 곳은 원본으로 돌아가지 않는다
constexpr double keptMinimum = 0.006; // 계속 합성하는 프레임의 최소 이동량 (0.6센트): PSOLA가 '이동 없음'으로 보고 원본으로 섞지 않게

// 프레임 i의 편집량을 '바뀌지 않음'(0)으로 볼지: |d| <= tolerance 이고, 앞 150 ms나 뒤 150 ms 중 한쪽이 내내 tolerance 이하일 때만.
// 펴기·음정 흐름처럼 모음 안에서 편집량이 비브라토마다 +에서 -로 0을 지나가면, 그 몇 프레임을 0으로 잘라 합성 <-> 원본이
// 초당 10번 넘게 바뀌었다 (선풍기 앞에서 내는 소리처럼 끊기고, 원본으로 돌아갈 때마다 밀린 위상을 따라잡느라 음정이 30~40센트 튐.
// 11/24 사용자 캡처). 짧게 0 근처에 머무는 곳도 계속 합성하면 위상을 따라잡을 일이 없다
bool unchangedAt(const std::vector<double>& d, size_t i, double tolerance) noexcept
{
    if (std::isnan(d[i]) || std::abs(d[i]) > tolerance)
        return std::isnan(d[i]);
    double before = 0.0, after = 0.0;
    for (auto k = i >= passingFrames ? i - passingFrames : 0; k <= i; ++k)
        before = std::max(before, std::isnan(d[k]) ? 0.0 : std::abs(d[k]));
    for (auto k = i; k < std::min(d.size(), i + passingFrames + 1); ++k)
        after = std::max(after, std::isnan(d[k]) ? 0.0 : std::abs(d[k]));
    return std::min(before, after) <= tolerance;
}

// 편집량 d(유성이 아니면 NaN)를 이동 곡선으로: 바뀌지 않는 곳만 0
std::vector<double> keptShifts(const std::vector<double>& d, double tolerance)
{
    std::vector<double> out(d.size(), 0.0);
    for (size_t i = 0; i < d.size(); ++i)
        if (! unchangedAt(d, i, tolerance))
            out[i] = std::abs(d[i]) >= keptMinimum ? d[i] : (d[i] < 0.0 ? -keptMinimum : keptMinimum);
    return out;
}
} // namespace

// 프레임마다 옮길 반음 (원본 프레임 기준). 원래 무성이거나 바뀌지 않는 곳(unchangedAt)이면 0
std::vector<double> psolaShiftCurve(const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span, double tolerance)
{
    std::vector<double> d(f.size(), std::nan(""));
    for (auto i = span.begin; i < std::min(span.end, f.size()); ++i)
        if (grid.originalMidi[i] > 0.0 && grid.targetMidi[i] > 0.0)
            d[i] = grid.targetMidi[i] - grid.originalMidi[i];
    return keptShifts(d, tolerance);
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

// 프레임마다 조각 간격을 그린 선에서 정하는 정도 (박자 그대로일 때 표시 프레임 = 원본 프레임). 없으면 비어 있다
std::vector<double> psolaLineFollowCurve(const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span)
{
    if (! grid.lineFollowEdited)
        return {};
    std::vector<double> amount(f.size(), 0.0);
    for (auto i = span.begin; i < std::min({ span.end, f.size(), grid.size() }); ++i)
        amount[i] = grid.lineFollow[i];
    return amount;
}

// 원본 프레임 기준 이동량 shift의 [first, last)에 자음 보호(와 vowelFocus면 모음 가중치)를 적용. 첫 채널로 판단한다 (채널마다 다르면 소리가 갈라진다)
std::vector<double> protectedShift(const AudioPlanes& src, const VocalFeatures& f, const EditGrid& grid, const std::vector<double>& shift,
                                   size_t first, size_t last)
{
    const auto period = f.framePeriodMs / 1000.0;
    const auto* x = src.channels[0].data();
    auto out = shieldedShift(x, src.channels[0].size(), src.sampleRate, period, shift, first, last);
    if (grid.vowelFocus)
        applyVowelFocus(out, x, src.channels[0].size(), src.sampleRate, period, f.midiF0.data(), f.size(), first, last);
    return out;
}

namespace
{
// 출력 프레임 이동량 out과 같은 값을 원본 프레임 쪽(bySource)에도 적는다. 원본 프레임 범위 [lo, hi)를 돌려준다
std::pair<size_t, size_t> shiftsBySource(const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span, double tolerance,
                                         std::vector<double>& out, std::vector<double>& bySource)
{
    size_t lo = f.size(), hi = 0;
    std::vector<double> d(out.size(), std::nan(""));
    for (auto i = span.begin; i < span.end; ++i)
    {
        const auto j = static_cast<size_t>(grid.sourceFrame[i]);
        if (f.midiF0[j] > 0.0 && grid.targetMidi[i] > 0.0)
            d[i] = grid.targetMidi[i] - f.midiF0[j];
        lo = std::min(lo, j);
        hi = std::max(hi, j + 1);
    }
    const auto kept = keptShifts(d, tolerance);
    for (auto i = span.begin; i < span.end; ++i)
        if (kept[i] != 0.0)
        {
            out[i] = kept[i];
            bySource[static_cast<size_t>(grid.sourceFrame[i])] = kept[i];
        }
    return { lo, hi };
}
} // namespace

// 박자가 바뀐 구간의 이동량 (출력 프레임 기준). 원본 음정은 가져오는 원본 프레임의 것. 자음 보호는 원본 시간 쪽에서 판단한다
std::vector<double> warpShiftCurve(const AudioPlanes& src, const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span, double tolerance)
{
    std::vector<double> out(grid.size(), 0.0), bySource(f.size(), 0.0);
    const auto [lo, hi] = shiftsBySource(f, grid, span, tolerance, out, bySource);
    const auto shielded = protectedShift(src, f, grid, bySource, lo, hi);
    for (auto i = span.begin; i < span.end; ++i)
    {
        const auto j = static_cast<size_t>(grid.sourceFrame[i]);
        if (bySource[j] != 0.0)
            out[i] *= shielded[j] / bySource[j];
    }
    return out;
}
