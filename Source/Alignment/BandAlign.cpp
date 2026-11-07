#include "BandAlign.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double bandEdges[5] = { 250.0, 500.0, 1000.0, 2000.0, 4000.0 };

struct Biquad
{
    double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    // 대역통과 (봉우리 0 dB)
    Biquad(double centreHz, double q, int rate)
    {
        const auto w = 2.0 * pi * centreHz / rate;
        const auto alpha = std::sin(w) / (2.0 * q);
        const auto a0 = 1.0 + alpha;
        b0 = alpha / a0;
        b2 = -alpha / a0;
        a1 = -2.0 * std::cos(w) / a0;
        a2 = (1.0 - alpha) / a0;
    }
    double operator()(double x) noexcept
    {
        const auto y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// 한 대역: 2차 대역통과 둘(약 4차) -> 정류 -> 10 Hz 1차 저역 둘
struct Band
{
    Biquad f1, f2;
    double smooth = 0.0, e1 = 0.0, e2 = 0.0;
    Band(double lo, double hi, int rate)
        : f1(std::sqrt(lo * hi), std::sqrt(lo * hi) / (hi - lo), rate), f2(std::sqrt(lo * hi), std::sqrt(lo * hi) / (hi - lo), rate),
          smooth(std::exp(-2.0 * pi * 10.0 / rate)) {}
    double operator()(double x) noexcept
    {
        const auto r = std::abs(f2(f1(x)));
        e1 = (1.0 - smooth) * r + smooth * e1;
        e2 = (1.0 - smooth) * e1 + smooth * e2;
        return e2;
    }
};

// 트랙 하나를 곡 시간 격자에 놓고 0..1로 맞춘 대역 값과 말소리 여부
struct Grid
{
    std::vector<std::array<float, 4>> level;
    std::vector<unsigned char> speech;
    std::vector<float> onset;   // 네 대역의 음량이 20 ms 사이에 오른 양의 합 (음절·자음 시작)
    std::vector<float> fromPeakDb;   // 네 대역 합 전력의 dB - 트랙의 98% 지점 (소리 시작 판정용, 무음 = -120)
};

Grid placed(const BandFrames& f, double offset, double gridStart, size_t count, double thresholdDb)
{
    std::array<double, 4> floorDb {}, peakDb {};
    for (size_t b = 0; b < 4; ++b)
    {
        std::vector<float> v;
        v.reserve(f.size());
        for (const auto& frame : f.db) v.push_back(frame[b]);
        if (v.empty()) { floorDb[b] = 0.0; peakDb[b] = 1.0; continue; }
        std::sort(v.begin(), v.end());
        floorDb[b] = std::max<double>(v[v.size() / 10], v.back() - 70.0);
        peakDb[b] = std::max(floorDb[b] + 1.0, static_cast<double>(v[v.size() * 98 / 100]));
    }
    Grid g;
    g.level.assign(count, { 0.0f, 0.0f, 0.0f, 0.0f });
    g.speech.assign(count, 0);
    g.fromPeakDb.assign(count, -120.0f);
    for (size_t k = 0; k < count; ++k)
    {
        const auto source = (gridStart + static_cast<double>(k) * f.hopSec - offset) / f.hopSec;
        if (source < 0.0) continue;
        const auto i = static_cast<size_t>(std::lround(source));
        if (i >= f.size()) continue;
        int loud = 0;
        for (size_t b = 0; b < 4; ++b)
        {
            const double db = f.db[i][b];
            g.level[k][b] = static_cast<float>(std::clamp((db - floorDb[b]) / (peakDb[b] - floorDb[b]), 0.0, 1.2));
            loud += db > floorDb[b] + thresholdDb ? 1 : 0;
        }
        g.speech[k] = loud >= 2 ? 1 : 0;
        double power = 0.0;
        for (size_t b = 0; b < 4; ++b) power += std::pow(10.0, f.db[i][b] / 10.0);
        g.fromPeakDb[k] = static_cast<float>(10.0 * std::log10(power + 1e-14));
    }
    {
        std::vector<float> sorted;
        for (const auto v : g.fromPeakDb) if (v > -119.0f) sorted.push_back(v);
        const auto peak = sorted.empty() ? 0.0f : (std::sort(sorted.begin(), sorted.end()), sorted[sorted.size() * 98 / 100]);
        for (auto& v : g.fromPeakDb) v = v > -119.0f ? v - peak : -120.0f;
    }
    g.onset.assign(count, 0.0f);
    for (size_t k = 2; k < count; ++k)
        for (size_t b = 0; b < 4; ++b)
            g.onset[k] += std::max(0.0f, g.level[k][b] - g.level[k - 2][b]);
    return g;
}

struct Run { size_t begin, end; };

std::vector<Run> speechRuns(const Grid& g, size_t minGap, size_t minRun)
{
    std::vector<Run> runs;
    for (size_t k = 0; k < g.speech.size(); ++k)
    {
        if (! g.speech[k]) continue;
        if (! runs.empty() && k - runs.back().end <= minGap) runs.back().end = k + 1;
        else runs.push_back({ k, k + 1 });
    }
    runs.erase(std::remove_if(runs.begin(), runs.end(), [minRun](const Run& r) { return r.end - r.begin < minRun; }), runs.end());
    return runs;
}

double distance(const Grid& tgt, const Grid& ref, size_t g, long r, double transientWeight, double levelWeight) noexcept
{
    if (r < 0 || r >= static_cast<long>(ref.level.size()))
        return 4.0;
    const auto& a = tgt.level[g];
    const auto& b = ref.level[static_cast<size_t>(r)];
    double d = 0.0;
    for (size_t i = 0; i < 4; ++i) d += levelWeight * std::abs(static_cast<double>(a[i]) - b[i]);
    d += transientWeight * std::abs(static_cast<double>(tgt.onset[g]) - ref.onset[static_cast<size_t>(r)]);
    return d + (tgt.speech[g] != ref.speech[static_cast<size_t>(r)] ? 0.5 : 0.0);
}

// 덩어리 하나의 DTW. 행 = 타깃 프레임 g (한 칸씩), 열 = 기준 프레임 r (g ± S). 결과: 행마다 고른 r
std::vector<long> alignRun(const Grid& tgt, const Grid& ref, size_t begin, size_t end, long steps, double hop, const BandAlignOptions& o)
{
    const auto rows = end - begin;
    const auto width = static_cast<size_t>(2 * steps + 1);
    constexpr double inf = std::numeric_limits<double>::infinity();
    std::vector<double> prev(width, inf), cur(width, inf);
    std::vector<unsigned char> from(rows * width, 1);
    for (size_t row = 0; row < rows; ++row)
    {
        const auto g = begin + row;
        const bool speech = tgt.speech[g] != 0;
        const double pen[3] = { speech ? o.repeatPenalty : o.silencePenalty, 0.0, speech ? o.skipPenalty : o.silencePenalty };
        std::fill(cur.begin(), cur.end(), inf);
        for (size_t k = 0; k < width; ++k)
        {
            const auto r = static_cast<long>(g) - steps + static_cast<long>(k);
            const auto local = distance(tgt, ref, g, r, o.transientWeight, o.levelWeight) + o.shiftPenaltyPerSec * std::abs(static_cast<double>(r - static_cast<long>(g))) * hop;
            // 시작은 열려 있되, 원래 자리에서 먼 칸으로 시작하는 데 벌점 (숨·잡음으로 시작하는 덩어리가 엉뚱한 칸에서 출발하지 않게)
            if (row == 0) { cur[k] = local + o.startPenaltyPerSec * std::abs(static_cast<double>(r - static_cast<long>(g))) * hop; continue; }
            double best = inf;
            unsigned char step = 1;
            for (unsigned char s = 0; s < 3; ++s)
            {
                // 이전 행에서 기준이 r - s 였던 칸: 이전 행의 열 번호 = (r - s) - (g - 1 - steps) = k - s + 1
                const auto pk = static_cast<long>(k) - s + 1;
                if (pk < 0 || pk >= static_cast<long>(width)) continue;
                const auto v = prev[static_cast<size_t>(pk)] + pen[s];
                if (v < best) { best = v; step = s; }
            }
            if (best == inf) continue;
            cur[k] = best + local;
            from[row * width + k] = step;
        }
        std::swap(prev, cur);
    }
    size_t k = static_cast<size_t>(std::min_element(prev.begin(), prev.end()) - prev.begin());   // 끝도 열려 있다
    std::vector<long> path(rows);
    for (size_t row = rows; row-- > 0;)
    {
        const auto g = begin + row;
        path[row] = static_cast<long>(g) - steps + static_cast<long>(k);
        if (row == 0) break;
        k = static_cast<size_t>(static_cast<long>(k) - from[row * width + k] + 1);
    }
    return path;
}
} // namespace

BandFrames computeBandFrames(const StereoReader& read, int64_t length, int rate, double hopSec, const std::function<bool()>& shouldCancel)
{
    BandFrames out;
    out.hopSec = hopSec;
    if (length <= 0 || rate <= 0 || bandEdges[4] >= 0.5 * rate)
        return out;
    std::vector<Band> bands;
    for (size_t b = 0; b < 4; ++b) bands.emplace_back(bandEdges[b], bandEdges[b + 1], rate);
    const auto hop = hopSec * rate;
    const int chunk = rate;
    std::vector<float> left(static_cast<size_t>(chunk)), right(static_cast<size_t>(chunk));
    double nextFrame = 0.0;
    for (int64_t start = 0; start < length; start += chunk)
    {
        if (shouldCancel && shouldCancel()) return {};
        const auto count = static_cast<int>(std::min<int64_t>(chunk, length - start));
        read(start, count, left.data(), right.data());
        for (int i = 0; i < count; ++i)
        {
            const auto x = 0.5 * (static_cast<double>(left[static_cast<size_t>(i)]) + right[static_cast<size_t>(i)]);
            std::array<double, 4> e {};
            for (size_t b = 0; b < 4; ++b) e[b] = bands[b](x);
            if (static_cast<double>(start + i) >= nextFrame)
            {
                std::array<float, 4> db {};
                for (size_t b = 0; b < 4; ++b) db[b] = static_cast<float>(20.0 * std::log10(e[b] + 1.0e-7));
                out.db.push_back(db);
                nextFrame += hop;
            }
        }
    }
    return out;
}

namespace
{
TimeMap alignOnce(const BandFrames& reference, double referenceOffset, const BandFrames& target, double targetOffset,
                  const BandAlignOptions& o);

// 타깃을 map대로 옮겨 놓은 대역 프레임 (옮긴 뒤의 곡 시간 = 원래 곡 시간 targetOffset부터)
BandFrames warpedBy(const BandFrames& target, double targetOffset, const TimeMap& map)
{
    const TimeMap inverse(map.to(), map.from());   // 단조 증가라 뒤집으면 역함수
    BandFrames out;
    out.hopSec = target.hopSec;
    out.db.resize(target.size());
    for (size_t k = 0; k < target.size(); ++k)
    {
        const auto song = targetOffset + static_cast<double>(k) * target.hopSec;
        const auto source = (inverse.map(song) - targetOffset) / target.hopSec;
        const auto i = static_cast<size_t>(std::clamp<long>(std::lround(source), 0, static_cast<long>(target.size()) - 1));
        out.db[k] = target.db[i];
    }
    return out;
}
} // namespace

TimeMap bandAlignMap(const BandFrames& reference, double referenceOffset, const BandFrames& target, double targetOffset,
                     const BandAlignOptions& o)
{
    // passes > 1: 맞춘 결과를 다시 맞춘다 (한 번에는 벌점·평활 때문에 차이의 일부만 줄인다). 두 대응을 이어 붙인다
    auto map = alignOnce(reference, referenceOffset, target, targetOffset, o);
    for (int pass = 1; pass < o.passes && ! map.empty(); ++pass)
    {
        const auto next = alignOnce(reference, referenceOffset, warpedBy(target, targetOffset, map), targetOffset, o);
        if (next.empty())
            break;
        std::vector<double> from, to;
        for (size_t i = 0; i < map.from().size(); ++i)
        {
            const auto t = map.from()[i];
            from.push_back(t);
            to.push_back(std::max(to.empty() ? -1e300 : to.back(), next.map(map.to()[i])));
        }
        map = TimeMap(std::move(from), std::move(to));
    }
    return map;
}

namespace
{
TimeMap alignOnce(const BandFrames& reference, double referenceOffset, const BandFrames& target, double targetOffset,
                  const BandAlignOptions& o)
{
    if (reference.size() == 0 || target.size() == 0 || reference.hopSec != target.hopSec || target.hopSec <= 0.0)
        return {};
    const auto hop = target.hopSec;
    const auto refEnd = referenceOffset + static_cast<double>(reference.size()) * hop;
    const auto tgtEnd = targetOffset + static_cast<double>(target.size()) * hop;
    const auto gridStart = std::min(referenceOffset, targetOffset);
    const auto count = static_cast<size_t>(std::ceil((std::max(refEnd, tgtEnd) - gridStart) / hop)) + 1;
    const auto ref = placed(reference, referenceOffset, gridStart, count, o.speechThresholdDb);
    const auto tgt = placed(target, targetOffset, gridStart, count, o.speechThresholdDb);
    const auto steps = static_cast<long>(std::lround(o.maxShiftSec / hop));
    const auto margin = static_cast<size_t>(o.marginSec / hop);
    const auto smooth = static_cast<long>(std::lround(o.smoothSec / hop));

    std::vector<double> from, to;
    for (const auto& run : speechRuns(tgt, static_cast<size_t>(o.minGapSec / hop), static_cast<size_t>(o.minRunSec / hop)))
    {
        if (o.shouldCancel && o.shouldCancel()) return {};
        const auto begin = run.begin > margin ? run.begin - margin : 0;
        const auto end = std::min(count, run.end + margin);
        const auto path = alignRun(tgt, ref, begin, end, steps, hop, o);
        std::vector<double> shift(path.size());
        for (size_t i = 0; i < path.size(); ++i) shift[i] = static_cast<double>(path[i] - static_cast<long>(begin + i)) * hop;
        // 약한 프레임(숨·자음 앞머리·끝 여운: 덩어리 음량 상위 10%보다 strongBelowDb 넘게 작은 곳)의 이동량은 믿지 않고
        // 가장 가까운 센 프레임의 값을 쓴다. 덩어리 머리의 숨을 기준의 엉뚱한 소리에 맞춘 값이 늘임 한도를 타고 프레이즈 전체로 퍼지던 문제.
        {
            std::vector<double> loud(shift.size());
            for (size_t i = 0; i < shift.size(); ++i)
            {
                const auto& l = tgt.level[begin + i];
                loud[i] = static_cast<double>(l[0]) + l[1] + l[2] + l[3];
            }
            auto sorted = loud;
            std::sort(sorted.begin(), sorted.end());
            const auto strong = sorted[sorted.size() * 9 / 10] * std::pow(10.0, -o.strongBelowDb / 20.0);
            std::vector<long> nearest(shift.size(), -1);
            long last = -1;
            for (size_t i = 0; i < shift.size(); ++i) { if (loud[i] >= strong) last = static_cast<long>(i); nearest[i] = last; }
            last = -1;
            for (size_t i = shift.size(); i-- > 0;)
            {
                if (loud[i] >= strong) last = static_cast<long>(i);
                if (last >= 0 && (nearest[i] < 0 || last - static_cast<long>(i) < static_cast<long>(i) - nearest[i])) nearest[i] = last;
            }
            auto filled = shift;
            for (size_t i = 0; i < shift.size(); ++i) if (nearest[i] >= 0) filled[i] = shift[static_cast<size_t>(nearest[i])];
            shift.swap(filled);
        }
        // 짧은 튐은 중앙값으로 지운다 (튄 값이 늘임 한도를 타고 넓게 퍼지지 않게)
        {
            const auto radius = static_cast<long>(std::lround(o.medianSec / hop));
            std::vector<double> med(shift.size()), window;
            for (size_t i = 0; i < shift.size(); ++i)
            {
                window.clear();
                for (auto m = std::max<long>(0, static_cast<long>(i) - radius); m <= std::min<long>(static_cast<long>(shift.size()) - 1, static_cast<long>(i) + radius); ++m)
                    window.push_back(shift[static_cast<size_t>(m)]);
                std::nth_element(window.begin(), window.begin() + static_cast<long>(window.size() / 2), window.end());
                med[i] = window[window.size() / 2];
            }
            shift.swap(med);
        }
        // 평활 -> 작은 차이는 0 (Max Difference) -> 이동량이 10 ms마다 바뀌는 폭을 maxStretch*10 ms로 제한
        // (한 블록 안에서 늘이고 줄이는 비율의 한도. 따로 두면 DTW의 잔걸음이 그대로 늘임·줄임이 되어 소리가 뒤틀린다)
        std::vector<double> smoothed(shift.size());
        for (size_t i = 0; i < shift.size(); ++i)
        {
            const auto lo = static_cast<size_t>(std::max<long>(0, static_cast<long>(i) - smooth));
            const auto hi = static_cast<size_t>(std::min<long>(static_cast<long>(shift.size()) - 1, static_cast<long>(i) + smooth));
            double sum = 0.0;
            for (auto m = lo; m <= hi; ++m) sum += shift[m];
            smoothed[i] = sum / static_cast<double>(hi - lo + 1);
            if (std::abs(smoothed[i]) < o.maxDifferenceSec) smoothed[i] = 0.0;
        }
        // 소리 시작 맞춤 (SmartAlign: 맞출 소리가 시작하는 곳끼리): 덩어리 첫 센 프레임(쉼 뒤 소리가 서는 곳)을, 지금 이동량 근처
        // (±startSnapSec)에서 쉼 뒤 소리가 서는 기준 프레임에 맞추고, 그 차이를 덩어리 머리 startBlendSec 동안 줄여 가며 더한다
        if (o.startSnapSec > 0.0)
        {
            const auto quiet = static_cast<long>(std::lround(0.08 / hop));
            const auto reach = static_cast<long>(std::lround(o.startSnapSec / hop));
            // 시작: 트랙 최고(98%)에서 riseBelowDb 안으로 막 올라섰고, 앞 80 ms는 모두 그 아래
            const auto level = [](const Grid& grid, long k) { return grid.fromPeakDb[static_cast<size_t>(k)]; };
            const auto rises = [&](const Grid& grid, long k) {
                const auto line = -static_cast<float>(o.riseBelowDb);
                if (k < quiet || k >= static_cast<long>(grid.level.size()) || level(grid, k) < line || level(grid, k - 1) >= line) return false;
                for (auto q = k - quiet; q < k; ++q) if (level(grid, q) >= line) return false;
                return true;
            };
            // 덩어리 안의 모든 '쉼(80 ms) 뒤 시작'에 대해: 고칠 양을 구해 그 시작 앞 80 ms ~ 뒤 startBlendSec에 삼각형으로 나눠 준다
            std::vector<double> correction(smoothed.size(), 0.0), weight(smoothed.size(), 0.0);
            const auto blend = std::max<long>(1, std::lround(o.startBlendSec / hop));
            for (long i = 0; i < static_cast<long>(smoothed.size()); ++i)
            {
                const auto g = static_cast<long>(begin) + i;
                if (! rises(tgt, g)) continue;
                const auto centre = g + std::lround(smoothed[static_cast<size_t>(i)] / hop);
                long best = -1;
                for (long d = 0; d <= reach && best < 0; ++d)
                    for (const auto r : { centre - d, centre + d })
                        if (best < 0 && rises(ref, r)) best = r;
                if (best < 0) continue;
                const auto fix = static_cast<double>(best - g) * hop - smoothed[static_cast<size_t>(i)];
                for (auto k = std::max<long>(0, i - quiet); k < std::min<long>(static_cast<long>(smoothed.size()), i + blend); ++k)
                {
                    const auto w = k <= i ? 1.0 : 1.0 - static_cast<double>(k - i) / static_cast<double>(blend);
                    correction[static_cast<size_t>(k)] += w * fix;
                    weight[static_cast<size_t>(k)] += w;
                }
            }
            for (size_t i = 0; i < smoothed.size(); ++i)
                if (weight[i] > 0.0) smoothed[i] += correction[i] / std::max(1.0, weight[i]);
        }
        // 음절 시작 맞춤 (Sync Vx의 트랜지언트 검출에 해당): 타깃의 뚜렷한 시작마다, 지금 이동량 근처(±snapOnsetSec)의
        // 기준 시작 중 가장 뚜렷한 것에 정확히 맞도록 그 둘레(±snapWidthSec, 삼각형)의 이동량을 고친다
        if (o.snapOnsetSec > 0.0)
        {
            const auto reach = static_cast<long>(std::lround(o.snapOnsetSec / hop));
            const auto width = std::max<long>(1, std::lround(o.snapWidthSec / hop));
            std::vector<double> correction(smoothed.size(), 0.0), weight(smoothed.size(), 0.0);
            for (size_t i = 1; i + 1 < smoothed.size(); ++i)
            {
                const auto g = begin + i;
                const auto on = tgt.onset[g];
                if (on < o.onsetThreshold || on < tgt.onset[g - 1] || on < tgt.onset[g + 1]) continue;
                const auto centre = static_cast<long>(g) + std::lround(smoothed[i] / hop);
                long best = -1;
                float bestOn = o.onsetThreshold;
                for (auto r = centre - reach; r <= centre + reach; ++r)
                    if (r > 0 && r + 1 < static_cast<long>(ref.onset.size()) && ref.onset[static_cast<size_t>(r)] > bestOn
                        && ref.onset[static_cast<size_t>(r)] >= ref.onset[static_cast<size_t>(r - 1)] && ref.onset[static_cast<size_t>(r)] >= ref.onset[static_cast<size_t>(r + 1)])
                    { best = r; bestOn = ref.onset[static_cast<size_t>(r)]; }
                if (best < 0) continue;
                const auto fix = static_cast<double>(best - static_cast<long>(g)) * hop - smoothed[i];
                for (auto k = -width; k <= width; ++k)
                {
                    const auto j = static_cast<long>(i) + k;
                    if (j < 0 || j >= static_cast<long>(smoothed.size())) continue;
                    const auto w = 1.0 - std::abs(static_cast<double>(k)) / static_cast<double>(width + 1);
                    correction[static_cast<size_t>(j)] += w * fix;
                    weight[static_cast<size_t>(j)] += w;
                }
            }
            for (size_t i = 0; i < smoothed.size(); ++i)
                if (weight[i] > 0.0) smoothed[i] += correction[i] / std::max(1.0, weight[i]);
        }
        const auto rate = o.maxStretch * hop;
        for (size_t i = 1; i < smoothed.size(); ++i) smoothed[i] = std::clamp(smoothed[i], smoothed[i - 1] - rate, smoothed[i - 1] + rate);
        for (size_t i = smoothed.size() - 1; i-- > 0;) smoothed[i] = std::clamp(smoothed[i], smoothed[i + 1] - rate, smoothed[i + 1] + rate);
        double total = 0.0;
        for (size_t i = 0; i < smoothed.size(); ++i)
        {
            const auto s = smoothed[i];
            total += s;
            const auto t = gridStart + static_cast<double>(begin + i) * hop;
            if (! from.empty() && t <= from.back()) continue;
            from.push_back(t);
            to.push_back(to.empty() ? t + s : std::max(to.back(), t + s));
        }
        if (o.onRun) o.onRun(gridStart + static_cast<double>(begin) * hop, gridStart + static_cast<double>(end) * hop, total / static_cast<double>(shift.size()));
    }
    return from.empty() ? TimeMap{} : TimeMap(std::move(from), std::move(to));
}
} // namespace
