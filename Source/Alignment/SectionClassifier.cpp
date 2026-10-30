#include "SectionClassifier.h"
#include <algorithm>
#include <cmath>

namespace
{
double percentile(std::vector<float> values, double fraction)
{
    if (values.empty())
        return 0.0;
    const auto index = static_cast<size_t>(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<long>(index), values.end());
    return values[index];
}

bool voicedAt(const VocalFeatures& f, double timeSec) noexcept
{
    if (f.size() == 0)
        return false;
    const auto index = std::llround(timeSec / (f.framePeriodMs / 1000.0));
    const auto i = static_cast<size_t>(std::clamp<long long>(index, 0, static_cast<long long>(f.size()) - 1));
    return f.midiF0[i] > 0.0;
}

// 같은 종류가 이어지는 구간 [begin, end)
struct Run
{
    size_t begin = 0, end = 0;
    SectionKind kind = SectionKind::Silence;
};

std::vector<Run> runsOf(const std::vector<SectionKind>& kinds)
{
    std::vector<Run> runs;
    for (size_t i = 0; i < kinds.size(); ++i)
    {
        if (! runs.empty() && runs.back().kind == kinds[i])
            runs.back().end = i + 1;
        else
            runs.push_back({ i, i + 1, kinds[i] });
    }
    return runs;
}

// minFrames보다 짧은 구간을 더 긴 쪽 이웃의 종류로 바꾼다
void absorbShortRuns(std::vector<SectionKind>& kinds, size_t minFrames)
{
    for (int pass = 0; pass < 3; ++pass)
    {
        const auto runs = runsOf(kinds);
        for (size_t r = 0; r < runs.size(); ++r)
        {
            if (runs[r].end - runs[r].begin >= minFrames || runs.size() == 1)
                continue;
            const auto prevLen = r > 0 ? runs[r - 1].end - runs[r - 1].begin : 0;
            const auto nextLen = r + 1 < runs.size() ? runs[r + 1].end - runs[r + 1].begin : 0;
            const auto kind = prevLen >= nextLen ? runs[r - 1].kind : runs[r + 1].kind;
            std::fill(kinds.begin() + static_cast<long>(runs[r].begin), kinds.begin() + static_cast<long>(runs[r].end), kind);
        }
    }
}
} // namespace

std::vector<SectionKind> classifyFrames(const SpectralFrames& frames, const VocalFeatures& features, const ClassifierOptions& o)
{
    const auto n = frames.size();
    std::vector<SectionKind> kinds(n, SectionKind::Silence);
    if (n == 0)
        return kinds;

    const auto floorDb = percentile(frames.energyDb, 0.10);
    const auto peakDb = percentile(frames.energyDb, 0.95);

    for (size_t i = 0; i < n; ++i)
    {
        const auto energy = static_cast<double>(frames.energyDb[i]);
        if (energy < floorDb + o.silenceAboveFloorDb)
            kinds[i] = SectionKind::Silence;
        else if (voicedAt(features, frames.timeAt(i)))
            kinds[i] = SectionKind::Voiced;
        else if (frames.flatness[i] >= o.breathMinFlatness && frames.highRatio[i] >= o.breathMinHighRatio
                 && energy <= peakDb - o.breathMaxBelowPeakDb)
            kinds[i] = SectionKind::Breath;
        else
            kinds[i] = SectionKind::Unvoiced;
    }

    const auto minFrames = std::max<size_t>(1, static_cast<size_t>(std::llround(o.minSectionSec / frames.hopSec)));
    absorbShortRuns(kinds, minFrames);

    // 호흡 후보가 너무 짧으면 무성음(자음)으로
    const auto breathFrames = static_cast<size_t>(std::llround(o.breathMinSec / frames.hopSec));
    for (const auto& run : runsOf(kinds))
        if (run.kind == SectionKind::Breath && run.end - run.begin < breathFrames)
            std::fill(kinds.begin() + static_cast<long>(run.begin), kinds.begin() + static_cast<long>(run.end), SectionKind::Unvoiced);
    return kinds;
}

std::vector<Section> toSections(const std::vector<SectionKind>& kinds, const SpectralFrames& frames)
{
    std::vector<Section> sections;
    for (const auto& run : runsOf(kinds))
        sections.push_back({ run.kind, frames.timeAt(run.begin) - 0.5 * frames.hopSec, frames.timeAt(run.end - 1) + 0.5 * frames.hopSec });
    return sections;
}
