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
    }
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

double distance(const Grid& tgt, const Grid& ref, size_t g, long r) noexcept
{
    if (r < 0 || r >= static_cast<long>(ref.level.size()))
        return 4.0;
    const auto& a = tgt.level[g];
    const auto& b = ref.level[static_cast<size_t>(r)];
    double d = 0.0;
    for (size_t i = 0; i < 4; ++i) d += std::abs(static_cast<double>(a[i]) - b[i]);
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
            const auto local = distance(tgt, ref, g, r) + o.shiftPenaltyPerSec * std::abs(static_cast<double>(r - static_cast<long>(g))) * hop;
            if (row == 0) { cur[k] = local; continue; }   // 시작은 열려 있다
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

TimeMap bandAlignMap(const BandFrames& reference, double referenceOffset, const BandFrames& target, double targetOffset,
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
