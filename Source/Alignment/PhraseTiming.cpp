#include "PhraseTiming.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
// 곡 시간 격자 위에 다시 놓은 한 트랙의 특징
struct Track
{
    std::vector<double> level;   // 0..1.2 정규화 음량
    std::vector<double> onset;   // 음량이 막 커지는 정도
    std::vector<double> voiced;  // 0 / 1
    std::vector<double> midi;    // 0 = 무성
};

Track resample(const VocalFeatures& f, double offset, double gridStart, size_t count, double hop)
{
    Track t;
    t.level.assign(count, 0.0);
    t.voiced.assign(count, 0.0);
    t.midi.assign(count, 0.0);
    const auto period = f.framePeriodMs / 1000.0;
    std::vector<double> db(count, -80.0);
    for (size_t k = 0; k < count; ++k)
    {
        const auto source = (gridStart + static_cast<double>(k) * hop - offset) / period;
        if (source < 0.0)
            continue;
        const auto i = static_cast<size_t>(std::lround(source));
        if (i >= f.size())
            continue;
        db[k] = 20.0 * std::log10(std::max(f.rms[i], 1.0e-4));
        t.midi[k] = f.midiF0[i] > 0.0 ? f.midiF0[i] : 0.0;
        t.voiced[k] = t.midi[k] > 0.0 ? 1.0 : 0.0;
    }
    auto sorted = db;
    std::sort(sorted.begin(), sorted.end());
    const auto floorDb = std::max(sorted[sorted.size() / 10], sorted.back() - 60.0);
    const auto peakDb = sorted[sorted.size() * 95 / 100];
    const auto range = std::max(1.0, peakDb - floorDb);
    for (size_t k = 0; k < count; ++k)
        t.level[k] = std::clamp((db[k] - floorDb) / range, 0.0, 1.2);
    constexpr size_t rise = 3;   // 30 ms
    t.onset.assign(count, 0.0);
    for (size_t k = rise; k < count; ++k)
        t.onset[k] = std::max(0.0, t.level[k] - t.level[k - rise]);
    return t;
}

struct Piece { size_t begin, end; };

// 유성 덩어리를 minGap 넘는 틈에서 끊고, 긴 것은 같은 길이 조각으로 나눈다
std::vector<Piece> piecesOf(const Track& t, size_t minGap, size_t maxPiece)
{
    std::vector<Piece> runs;
    for (size_t k = 0; k < t.voiced.size(); ++k)
    {
        if (t.voiced[k] <= 0.0)
            continue;
        if (! runs.empty() && k - runs.back().end <= minGap)
            runs.back().end = k + 1;
        else
            runs.push_back({ k, k + 1 });
    }
    std::vector<Piece> out;
    for (const auto& r : runs)
    {
        const auto length = r.end - r.begin;
        const auto parts = std::max<size_t>(1, (length + maxPiece - 1) / maxPiece);
        for (size_t p = 0; p < parts; ++p)
            out.push_back({ r.begin + length * p / parts, r.begin + length * (p + 1) / parts });
    }
    return out;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b, size_t begin, size_t end, long lag)
{
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    const auto n = static_cast<double>(end - begin);
    for (auto k = begin; k < end; ++k)
    {
        const auto j = static_cast<long>(k) + lag;
        const auto x = a[k];
        const auto y = j >= 0 && j < static_cast<long>(b.size()) ? b[static_cast<size_t>(j)] : 0.0;
        sa += x; sb += y; saa += x * x; sbb += y * y; sab += x * y;
    }
    const auto va = saa - sa * sa / n, vb = sbb - sb * sb / n;
    return va > 1e-9 && vb > 1e-9 ? (sab - sa * sb / n) / std::sqrt(va * vb) : 0.0;
}

double median(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// 음 높이 모양: 둘 다 유성인 프레임에서 조각 중앙값을 뺀 곡선끼리 비교 (화음의 평행 이동은 같은 모양). 겹친 비율을 곱한다
double pitchShape(const Track& tgt, const Track& ref, size_t begin, size_t end, long lag)
{
    std::vector<double> a, b;
    size_t voiced = 0;
    for (auto k = begin; k < end; ++k)
    {
        if (tgt.midi[k] <= 0.0)
            continue;
        ++voiced;
        const auto j = static_cast<long>(k) + lag;
        if (j < 0 || j >= static_cast<long>(ref.midi.size()) || ref.midi[static_cast<size_t>(j)] <= 0.0)
            continue;
        a.push_back(tgt.midi[k]);
        b.push_back(ref.midi[static_cast<size_t>(j)]);
    }
    if (a.size() < 5 || voiced == 0)
        return 0.0;
    const auto ma = median(a), mb = median(b);
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        sum += std::exp(-std::abs((a[i] - ma) - (b[i] - mb)));
    return sum / static_cast<double>(voiced);
}

std::vector<double> zScores(std::vector<double> s)
{
    const auto mean = std::accumulate(s.begin(), s.end(), 0.0) / static_cast<double>(s.size());
    double var = 0.0;
    for (const auto v : s) var += (v - mean) * (v - mean);
    const auto sd = std::sqrt(var / static_cast<double>(s.size())) + 1e-12;
    for (auto& v : s) v = (v - mean) / sd;
    return s;
}

// 이웃 조각과 시차가 갑자기 바뀌는 데 벌점을 주고 함께 고른다 (비터비)
std::vector<size_t> smoothest(const std::vector<std::vector<double>>& scores, double jumpPerStep)
{
    const auto n = scores.size(), m = scores.front().size();
    std::vector<double> total = scores.front(), next(m);
    std::vector<std::vector<size_t>> from(n, std::vector<size_t>(m, 0));
    for (size_t w = 1; w < n; ++w)
    {
        for (size_t l = 0; l < m; ++l)
        {
            auto best = -1e300;
            for (size_t p = 0; p < m; ++p)
            {
                const auto v = total[p] - jumpPerStep * std::abs(static_cast<double>(l) - static_cast<double>(p));
                if (v > best) { best = v; from[w][l] = p; }
            }
            next[l] = best + scores[w][l];
        }
        total.swap(next);
    }
    std::vector<size_t> path(n);
    path[n - 1] = static_cast<size_t>(std::max_element(total.begin(), total.end()) - total.begin());
    for (auto w = n - 1; w > 0; --w)
        path[w - 1] = from[w][path[w]];
    return path;
}
} // namespace

TimeMap phraseTimeMap(const VocalFeatures& reference, double referenceOffset, const VocalFeatures& target, double targetOffset,
                      const PhraseTimingOptions& o)
{
    if (reference.size() == 0 || target.size() == 0 || o.hopSec <= 0.0)
        return {};
    const auto refEnd = referenceOffset + static_cast<double>(reference.size()) * reference.framePeriodMs / 1000.0;
    const auto tgtEnd = targetOffset + static_cast<double>(target.size()) * target.framePeriodMs / 1000.0;
    const auto gridStart = std::min(referenceOffset, targetOffset);
    const auto count = static_cast<size_t>(std::ceil((std::max(refEnd, tgtEnd) - gridStart) / o.hopSec)) + 1;
    const auto ref = resample(reference, referenceOffset, gridStart, count, o.hopSec);
    const auto tgt = resample(target, targetOffset, gridStart, count, o.hopSec);
    const auto steps = static_cast<long>(std::lround(o.maxShiftSec / o.hopSec));
    const auto timeAt = [&](size_t k) { return gridStart + static_cast<double>(k) * o.hopSec; };

    std::vector<Piece> used;
    std::vector<std::vector<double>> scores;
    for (const auto& piece : piecesOf(tgt, static_cast<size_t>(o.minGapSec / o.hopSec), static_cast<size_t>(o.maxPieceSec / o.hopSec)))
    {
        if (o.shouldCancel && o.shouldCancel())
            return {};
        // 앞뒤로 50 ms 넓혀 음절 시작을 조각 안에 둔다
        const Piece wide { piece.begin > 5 ? piece.begin - 5 : 0, std::min(count, piece.end + 5) };
        if (static_cast<double>(piece.end - piece.begin) * o.hopSec < o.minPieceSec)
        {
            if (o.onPiece) o.onPiece(timeAt(piece.begin), timeAt(piece.end), 0.0, 0.0, false);
            continue;
        }
        std::vector<double> score;
        for (long lag = -steps; lag <= steps; ++lag)
            score.push_back(o.levelWeight * correlation(tgt.level, ref.level, wide.begin, wide.end, lag)
                            + o.transientWeight * correlation(tgt.onset, ref.onset, wide.begin, wide.end, lag)
                            + o.voicingWeight * correlation(tgt.voiced, ref.voiced, wide.begin, wide.end, lag)
                            + o.pitchWeight * pitchShape(tgt, ref, piece.begin, piece.end, lag));
        score = zScores(std::move(score));
        const auto best = std::max_element(score.begin(), score.end());
        const bool sure = *best >= o.minConfidence;
        if (o.onPiece)
            o.onPiece(timeAt(piece.begin), timeAt(piece.end), *best, static_cast<double>(best - score.begin() - steps) * o.hopSec, sure);
        if (! sure)
            continue;
        used.push_back(piece);
        scores.push_back(std::move(score));
    }
    if (used.empty())
        return {};
    const auto path = smoothest(scores, o.jumpCostPerSec * o.hopSec);
    std::vector<double> from, to;
    for (size_t i = 0; i < used.size(); ++i)
    {
        auto lag = static_cast<double>(static_cast<long>(path[i]) - steps) * o.hopSec;
        if (std::abs(lag) < 0.015)
            lag = 0.0;      // 10ms쯤 옮기자고 블록을 다시 합성하지 않는다
        for (const auto t : { timeAt(used[i].begin), timeAt(used[i].end) - 0.5 * o.hopSec })
        {
            if (! from.empty() && t <= from.back())
                continue;
            from.push_back(t);
            to.push_back(to.empty() ? t + lag : std::max(to.back(), t + lag));
        }
    }
    return TimeMap(std::move(from), std::move(to));
}
