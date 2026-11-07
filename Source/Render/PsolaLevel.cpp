#include "PsolaLevel.h"
#include <algorithm>
#include <cmath>
#include <deque>

namespace psola
{
namespace
{
struct Sums { std::vector<double> ref, out, w; };

Sums prefixSums(const std::vector<float>& y, const std::vector<float>& ref, const std::vector<double>& weight)
{
    Sums s{ std::vector<double>(y.size() + 1, 0.0), std::vector<double>(y.size() + 1, 0.0), std::vector<double>(y.size() + 1, 0.0) };
    for (size_t i = 0; i < y.size(); ++i)
    {
        const auto r = weight[i] * ref[i] * ref[i], o = weight[i] * y[i] * y[i];
        const auto ok = std::isfinite(r) && std::isfinite(o);   // NaN/Inf 표본은 gain 계산에서 뺀다 (0 dB)
        s.ref[i + 1] = s.ref[i] + (ok ? r : 0.0);
        s.out[i + 1] = s.out[i] + (ok ? o : 0.0);
        s.w[i + 1] = s.w[i] + (ok ? weight[i] : 0.0);
    }
    return s;
}

// 창 [lo, hi)의 gain (dB). 합성 비중이 낮거나 무음이면 0
double windowGainDb(const Sums& s, size_t lo, size_t hi)
{
    if (hi <= lo) return 0.0;
    const auto w = s.w[hi] - s.w[lo];
    if (w < 0.25 * static_cast<double>(hi - lo)) return 0.0;
    const auto src = std::sqrt((s.ref[hi] - s.ref[lo]) / w), out = std::sqrt((s.out[hi] - s.out[lo]) / w);
    if (src < levelGateRms || out < 1e-9) return 0.0;
    const auto db = 20.0 * std::log10(src / out);
    return std::isfinite(db) ? std::clamp(db, -levelMaxDb, levelMaxDb) : 0.0;
}

std::vector<double> boxSmooth(const std::vector<double>& x, size_t radius)
{
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto lo = i > radius ? i - radius : 0, hi = std::min(x.size(), i + radius + 1);
        double sum = 0.0;
        for (auto j = lo; j < hi; ++j) sum += x[j];
        y[i] = sum / static_cast<double>(hi - lo);
    }
    return y;
}

// 인접 창 gain 차를 levelSlewDb 안으로 (앞으로, 뒤로 한 번씩)
void limitSlew(std::vector<double>& g)
{
    for (size_t i = 1; i < g.size(); ++i) g[i] = std::clamp(g[i], g[i - 1] - levelSlewDb, g[i - 1] + levelSlewDb);
    for (size_t i = g.size(); i-- > 1;) g[i - 1] = std::clamp(g[i - 1], g[i] - levelSlewDb, g[i] + levelSlewDb);
}

// 표본 i의 gain. 창 중심은 off + k * hop (off보다 앞은 첫 gain)
double gainAt(const std::vector<double>& db, size_t i, size_t hop, size_t off)
{
    if (i < off) return std::pow(10.0, db[0] / 20.0);
    const auto k = std::min((i - off) / hop, db.size() - 1), k2 = std::min(k + 1, db.size() - 1);
    const auto f = static_cast<double>((i - off) % hop) / static_cast<double>(hop);
    return std::pow(10.0, ((1.0 - f) * db[k] + f * db[k2]) / 20.0);
}

// 표본 i 둘레 win 표본 안 |ref|의 최대 (슬라이딩 최대)
std::vector<float> localPeaks(const std::vector<float>& ref, size_t win)
{
    std::vector<float> peak(ref.size());
    std::deque<size_t> q;
    size_t next = 0;
    for (size_t i = 0; i < ref.size(); ++i)
    {
        for (; next < ref.size() && next <= i + win / 2; ++next)
        {
            while (! q.empty() && std::abs(ref[q.back()]) <= std::abs(ref[next])) q.pop_back();
            q.push_back(next);
        }
        while (q.front() + win / 2 < i) q.pop_front();
        peak[i] = std::abs(ref[q.front()]);
    }
    return peak;
}

size_t hopOf(int sampleRate) { return std::max<size_t>(1, static_cast<size_t>(std::lround(levelHopSec * sampleRate))); }
} // namespace

size_t levelGridOffset(size_t origin, int sampleRate)
{
    const auto hop = hopOf(sampleRate);
    return (hop - origin % hop) % hop;
}

std::vector<double> levelGainsDb(const std::vector<float>& y, const std::vector<float>& ref, const std::vector<double>& weight, int sampleRate,
                                 size_t origin)
{
    if (y.empty() || ref.size() != y.size() || weight.size() != y.size()) return {};
    const auto hop = hopOf(sampleRate), win = std::max<size_t>(2, static_cast<size_t>(std::lround(levelWindowSec * sampleRate)));
    const auto off = levelGridOffset(origin, sampleRate);
    const auto sums = prefixSums(y, ref, weight);
    std::vector<double> db((y.size() + hop - 1 - std::min(off, y.size() - 1)) / hop + 1);
    for (size_t k = 0; k < db.size(); ++k)
    {
        const auto c = off + k * hop;
        db[k] = windowGainDb(sums, c > win / 2 ? std::min(y.size(), c - win / 2) : 0, std::min(y.size(), c + win / 2));
    }
    db = boxSmooth(db, 2);
    limitSlew(db);
    return db;
}

void applyLevelMatch(std::vector<float>& y, const std::vector<float>& ref, const std::vector<double>& weight, int sampleRate, size_t origin)
{
    const auto db = levelGainsDb(y, ref, weight, sampleRate, origin);
    if (db.empty()) return;
    const auto hop = hopOf(sampleRate), off = levelGridOffset(origin, sampleRate);
    const auto peak = localPeaks(ref, std::max<size_t>(2, static_cast<size_t>(std::lround(levelWindowSec * sampleRate))));
    for (size_t i = 0; i < y.size(); ++i)
    {
        if (weight[i] <= 0.0 || ! std::isfinite(y[i])) continue;
        auto g = 1.0 + weight[i] * (gainAt(db, i, hop, off) - 1.0);
        if (const auto mag = std::abs(static_cast<double>(y[i])); g > 1.0 && mag * g > peak[i]) g = std::max(1.0, peak[i] / mag);
        y[i] = static_cast<float>(y[i] * g);
    }
}

std::vector<double> coveredMask(const std::vector<double>& mask, const std::vector<double>& synthWeight)
{
    std::vector<double> out(mask);
    for (size_t i = 0; i < out.size() && i < synthWeight.size(); ++i)
        out[i] = synthWeight[i] > synthCoverageMin ? out[i] : 0.0;
    return out;
}

std::vector<double> maskWeight(const std::vector<double>& mask, int sampleRate)
{
    const auto fade = std::max<size_t>(1, static_cast<size_t>(0.01 * sampleRate));
    std::vector<double> w(mask.size(), 0.0);
    double sum = 0.0;
    for (size_t i = 0; i < mask.size() + fade / 2; ++i)
    {
        if (i < mask.size()) sum += mask[i];
        if (i >= fade) sum -= mask[i - fade];
        if (i >= fade / 2 && i - fade / 2 < mask.size()) w[i - fade / 2] = std::min(mask[i - fade / 2], sum / static_cast<double>(fade));
    }
    return w;
}
} // namespace psola
