#include "TimeWarp.h"
#include <algorithm>
#include <cmath>
#include <limits>

double TimeMap::map(double t) const noexcept
{
    if (fromTimes.empty())
        return t;
    if (t <= fromTimes.front())
        return t + (toTimes.front() - fromTimes.front());
    if (t >= fromTimes.back())
        return t + (toTimes.back() - fromTimes.back());

    const auto it = std::upper_bound(fromTimes.begin(), fromTimes.end(), t);
    const auto hi = static_cast<size_t>(it - fromTimes.begin());
    const auto lo = hi - 1;
    const auto span = fromTimes[hi] - fromTimes[lo];
    const auto x = span > 0.0 ? (t - fromTimes[lo]) / span : 0.0;
    return toTimes[lo] + x * (toTimes[hi] - toTimes[lo]);
}

namespace
{
std::vector<float> normalisedEnergy(const SpectralFrames& frames)
{
    std::vector<float> sorted = frames.energyDb;
    if (sorted.empty())
        return {};
    std::sort(sorted.begin(), sorted.end());
    const auto floorDb = sorted[sorted.size() / 10];
    const auto peakDb = sorted[sorted.size() * 95 / 100];
    const auto range = std::max(1.0f, peakDb - floorDb);
    std::vector<float> out(frames.size());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = std::clamp((frames.energyDb[i] - floorDb) / range, 0.0f, 1.2f);
    return out;
}

double kindCost(SectionKind a, SectionKind b, double mismatch) noexcept
{
    if (a == b)
        return 0.0;
    const auto softPair = (a == SectionKind::Silence && b == SectionKind::Breath) || (a == SectionKind::Breath && b == SectionKind::Silence)
                       || (a == SectionKind::Voiced && b == SectionKind::Unvoiced) || (a == SectionKind::Unvoiced && b == SectionKind::Voiced);
    return softPair ? 0.5 * mismatch : mismatch;
}

struct Context
{
    const TrackFrames& ref;
    const TrackFrames& tgt;
    const AlignOptions& options;
    std::vector<float> refEnergy, tgtEnergy;

    double distance(size_t i, size_t j) const noexcept
    {
        const auto* a = ref.frames->coefficients(i);
        const auto* b = tgt.frames->coefficients(j);
        double sum = 0.0;
        for (int c = 0; c < mfccCount; ++c)
        {
            const double d = a[c] - b[c];
            sum += d * d;
        }
        auto cost = std::sqrt(sum / mfccCount);
        cost += options.energyWeight * std::abs(static_cast<double>(refEnergy[i]) - tgtEnergy[j]);
        cost += kindCost((*ref.kinds)[i], (*tgt.kinds)[j], options.kindMismatchCost);
        return cost;
    }
};

TimeMap pathToMap(const std::vector<std::pair<size_t, size_t>>& path, const TrackFrames& ref, const TrackFrames& tgt, double smoothSec)
{
    // 타깃 프레임마다 대응된 레퍼런스 프레임들의 평균 시각
    std::vector<double> from, delta;
    size_t k = 0;
    while (k < path.size())
    {
        const auto j = path[k].second;
        double sum = 0.0;
        size_t count = 0;
        for (; k < path.size() && path[k].second == j; ++k, ++count)
            sum += ref.songTimeAt(path[k].first);
        const auto t = tgt.songTimeAt(j);
        from.push_back(t);
        delta.push_back(sum / static_cast<double>(count) - t);
    }

    const auto radius = static_cast<long>(std::llround(smoothSec / tgt.frames->hopSec));
    std::vector<double> to(from.size());
    for (size_t i = 0; i < from.size(); ++i)
    {
        const auto lo = std::max<long>(0, static_cast<long>(i) - radius);
        const auto hi = std::min<long>(static_cast<long>(from.size()) - 1, static_cast<long>(i) + radius);
        double sum = 0.0;
        for (auto m = lo; m <= hi; ++m)
            sum += delta[static_cast<size_t>(m)];
        to[i] = from[i] + sum / static_cast<double>(hi - lo + 1);
        if (i > 0)
            to[i] = std::max(to[i], to[i - 1]);
    }
    return { std::move(from), std::move(to) };
}
} // namespace

AlignResult alignToReference(const TrackFrames& reference, const TrackFrames& target, const AlignOptions& options)
{
    AlignResult result;
    if (reference.frames == nullptr || target.frames == nullptr || reference.kinds == nullptr || target.kinds == nullptr)
        return result;
    const auto n = reference.frames->size();
    const auto m = target.frames->size();
    if (n < 4 || m < 4)
        return result;

    const Context ctx { reference, target, options, normalisedEnergy(*reference.frames), normalisedEnergy(*target.frames) };
    const auto hopT = target.frames->hopSec;
    const auto band = static_cast<long>(std::ceil(options.maxShiftSec / hopT));
    const auto width = static_cast<size_t>(2 * band + 1);

    // 레퍼런스 프레임 i의 곡 시간에 해당하는 타깃 프레임 (대각선의 중심)
    const auto centre = [&](size_t i)
    {
        const auto songTime = reference.songTimeAt(i);
        const auto guided = options.guide != nullptr && ! options.guide->empty() ? options.guide->map(songTime) : songTime;
        return static_cast<long>(std::llround((guided - target.songTimeAt(0)) / hopT));
    };

    size_t first = n, last = 0;
    for (size_t i = 0; i < n; ++i)
        if (centre(i) >= 0 && centre(i) < static_cast<long>(m))
        {
            first = std::min(first, i);
            last = i;
        }
    if (first > last || first == n)
        return result;

    const auto rows = last - first + 1;
    constexpr double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> previous(width, infinity), current(width, infinity);
    std::vector<unsigned char> direction(rows * width, 0);   // 0 대각선, 1 위(레퍼런스만 진행), 2 왼쪽(타깃만 진행)

    for (size_t r = 0; r < rows; ++r)
    {
        if (options.progress && r % 256 == 0 && ! options.progress(static_cast<double>(r) / static_cast<double>(rows)))
        {
            result.cancelled = true;
            return result;
        }
        const auto i = first + r;
        const auto c = centre(i);
        const auto prevCentre = r > 0 ? centre(i - 1) : 0;
        std::fill(current.begin(), current.end(), infinity);
        for (size_t k = 0; k < width; ++k)
        {
            const auto j = c - band + static_cast<long>(k);
            if (j < 0 || j >= static_cast<long>(m))
                continue;
            const auto d = ctx.distance(i, static_cast<size_t>(j));

            double best = infinity;
            unsigned char dir = 0;
            if (r == 0)
                best = 0.0;
            else
            {
                const auto diagK = j - 1 - prevCentre + band;
                const auto upK = j - prevCentre + band;
                if (diagK >= 0 && diagK < static_cast<long>(width) && previous[static_cast<size_t>(diagK)] < best)
                    best = previous[static_cast<size_t>(diagK)];
                if (upK >= 0 && upK < static_cast<long>(width) && previous[static_cast<size_t>(upK)] + options.stepPenalty < best)
                {
                    best = previous[static_cast<size_t>(upK)] + options.stepPenalty;
                    dir = 1;
                }
            }
            if (k > 0 && current[k - 1] + options.stepPenalty < best)
            {
                best = current[k - 1] + options.stepPenalty;
                dir = 2;
            }
            if (best == infinity)
                continue;
            current[k] = best + d;
            direction[r * width + k] = dir;
        }
        std::swap(previous, current);
    }

    // 끝: 마지막 행에서 가장 싼 칸
    size_t bestK = 0;
    for (size_t k = 1; k < width; ++k)
        if (previous[k] < previous[bestK])
            bestK = k;
    if (previous[bestK] == infinity)
        return result;

    std::vector<std::pair<size_t, size_t>> path;
    auto r = rows - 1;
    auto k = bestK;
    while (true)
    {
        const auto i = first + r;
        const auto j = static_cast<size_t>(centre(i) - band + static_cast<long>(k));
        path.push_back({ i, j });
        if (r == 0 && direction[r * width + k] != 2)
            break;
        const auto dir = direction[r * width + k];
        if (dir == 2)
        {
            --k;
            continue;
        }
        const auto prevCentre = centre(i - 1);
        const auto jPrev = dir == 0 ? static_cast<long>(j) - 1 : static_cast<long>(j);
        if (r == 0)
            break;
        k = static_cast<size_t>(jPrev - prevCentre + band);
        --r;
    }
    std::reverse(path.begin(), path.end());

    result.ok = true;
    result.meanCost = previous[bestK] / static_cast<double>(path.size());
    result.targetToReference = pathToMap(path, reference, target, options.smoothSec);
    return result;
}
