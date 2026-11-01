#include "GuidedTiming.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include "MixSpectrum.h"

namespace
{
constexpr double gridLowMidi = 30.0, gridStep = 0.1;
constexpr size_t gridSize = 701;      // 30 ~ 100 반음, 10센트 간격
constexpr size_t semitoneSteps = 10;

// 원곡 [startSec, startSec + count*hop) 의 가운데 성분에서, 음 높이 격자마다 배음 점수 (원곡 소스 시간)
// 격자는 [lowMidi, highMidi] 만 채운다 (나머지는 0: 그 구간 가이드가 쓰지 않는 음)
std::vector<std::vector<float>> salienceOver(double startSec, size_t count, int rate, const StereoReader& read, MixSpectrum& spectrum,
                                             double hopSec, double lowMidi, double highMidi)
{
    const auto gridOf = [](double m) { return static_cast<size_t>(std::clamp((m - gridLowMidi) / gridStep, 0.0, gridSize - 1.0)); };
    const auto lowGrid = gridOf(lowMidi), highGrid = gridOf(highMidi);
    const auto n = spectrum.size();
    const auto hop = hopSec * rate;
    const auto binHz = static_cast<double>(rate) / static_cast<double>(n);
    const auto first = static_cast<int64_t>(std::lround(startSec * rate)) - static_cast<int64_t>(n / 2);
    const auto length = static_cast<int>(std::lround(static_cast<double>(count) * hop)) + static_cast<int>(n) + 2;
    std::vector<float> left(static_cast<size_t>(length)), right(static_cast<size_t>(length));
    read(first, length, left.data(), right.data());
    std::vector<std::vector<float>> out(count, std::vector<float>(gridSize));
    for (size_t j = 0; j < count; ++j)
    {
        const auto offset = static_cast<size_t>(std::lround(static_cast<double>(j) * hop));
        const auto spec = spectrum.centre(left.data() + offset, right.data() + offset);
        for (auto g = lowGrid; g <= highGrid; ++g)
            out[j][g] = static_cast<float>(harmonicSalience(spec, hzOf(gridLowMidi + gridStep * static_cast<double>(g)), binHz, 10));
    }
    return out;
}

// 그 음이 반음 위아래보다 얼마나 더 뚜렷한지 (넓게 퍼진 소리·드럼은 셋 다 올라가서 상쇄된다)
double pitchContrast(const std::vector<float>& grid, double midi)
{
    const auto g = static_cast<long>(std::lround((midi - gridLowMidi) / gridStep));
    if (g < static_cast<long>(semitoneSteps) || g + static_cast<long>(semitoneSteps) >= static_cast<long>(gridSize))
        return 0.0;
    const auto i = static_cast<size_t>(g);
    return grid[i] - 0.5 * (grid[i - semitoneSteps] + grid[i + semitoneSteps]);
}

// 원곡의 잔향은 음을 뒤로 끌어 늦은 시차를 편들게 한다 (실측 +0.05~0.1초). 음이 막 커지는 순간(잔향 없음)에 더 무게를 둔다
constexpr size_t riseFrames = 5;
constexpr double riseWeight = 2.0;

// 한 구간: 타깃 프레임 k(시각 start + k*hop)의 음을 원곡 프레임 k + l 에서 찾은 점수 (평균 0, 표준편차 1로 맞춤)
std::vector<double> rawLagScores(const std::vector<double>& guide, const std::vector<std::vector<float>>& salience, size_t lagSteps,
                                 double transpose)
{
    std::vector<double> score(2 * lagSteps + 1, 0.0);
    for (size_t k = 0; k < guide.size(); ++k)
    {
        if (guide[k] <= 0.0)
            continue;
        const auto midi = guide[k] + transpose;
        for (size_t l = 0; l < score.size(); ++l)
        {
            const auto now = pitchContrast(salience[k + l], midi);
            const auto rise = k + l >= riseFrames ? std::max(0.0, now - pitchContrast(salience[k + l - riseFrames], midi)) : 0.0;
            score[l] += now + riseWeight * rise;
        }
    }
    return score;
}

// 음을 틀리게(반음 몇 개 낮거나 높게) 부른 프레이즈도 박자는 찾도록, 전체를 ±transpose 반음 옮긴 가이드로도 찾아 가장 뚜렷한 쪽을 쓴다
std::vector<double> lagScores(const std::vector<double>& guide, const std::vector<std::vector<float>>& salience, size_t lagSteps, int span)
{
    auto score = rawLagScores(guide, salience, lagSteps, 0.0);
    for (int t = -span; t <= span; ++t)
    {
        if (t == 0)
            continue;
        auto other = rawLagScores(guide, salience, lagSteps, t);
        if (*std::max_element(other.begin(), other.end()) > *std::max_element(score.begin(), score.end()))
            score.swap(other);
    }
    const auto mean = std::accumulate(score.begin(), score.end(), 0.0) / static_cast<double>(score.size());
    double var = 0.0;
    for (const auto v : score)
        var += (v - mean) * (v - mean);
    const auto sd = std::sqrt(var / static_cast<double>(score.size())) + 1e-12;
    for (auto& v : score)
        v = (v - mean) / sd;
    return score;
}

// 구간마다 따로 고르면 리듬이 되풀이되는 곳에서 엉뚱한 시차가 이긴다. 이웃 구간과 시차가 갑자기 바뀌는 데 벌점을 주고 함께 고른다 (비터비)
std::vector<size_t> smoothestLags(const std::vector<std::vector<double>>& scores, double jumpPerStep)
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
                if (v > best)
                {
                    best = v;
                    from[w][l] = p;
                }
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
// 전체 시차(중앙값)에서 조금 벗어난 구간은 측정 흔들림일 때가 많다: 벗어난 만큼에서 toleranceSec을 빼고 따른다
std::vector<double> shrunkTowardMedian(std::vector<double> lags, double toleranceSec)
{
    auto sorted = lags;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(sorted.size() / 2), sorted.end());
    const auto median = sorted[sorted.size() / 2];
    for (auto& lag : lags)
    {
        const auto away = lag - median;
        lag = median + std::copysign(std::max(0.0, std::abs(away) - toleranceSec), away);
        if (std::abs(lag) < 0.5 * toleranceSec)
            lag = 0.0;      // 곡 전체를 10ms쯤 옮기자고 모든 블록을 다시 합성하지 않는다
    }
    return lags;
}
} // namespace

TimeMap guidedTimeMap(int64_t referenceLength, int referenceRate, const StereoReader& read, const VocalFeatures& target,
                      double targetOffset, double referenceOffset, const GuidedTimingOptions& o)
{
    if (target.size() == 0 || referenceLength <= 0)
        return {};
    MixSpectrum spectrum(static_cast<size_t>(o.frameSize));
    const auto period = target.framePeriodMs / 1000.0;
    const auto frames = static_cast<size_t>(o.windowSec / o.hopSec);
    const auto lagSteps = static_cast<size_t>(o.maxLagSec / o.hopSec);
    const auto targetEnd = targetOffset + static_cast<double>(target.size()) * period;
    std::vector<double> centres;
    std::vector<std::vector<double>> scores;
    for (auto start = targetOffset; start + o.windowSec <= targetEnd + 1e-9; start += o.stepSec)
    {
        if (o.shouldCancel && o.shouldCancel())
            return {};
        std::vector<double> guide(frames, 0.0);
        size_t voiced = 0;
        double lowMidi = 1e9, highMidi = -1e9;
        for (size_t k = 0; k < frames; ++k)
        {
            const auto index = static_cast<size_t>(std::lround((start + static_cast<double>(k) * o.hopSec - targetOffset) / period));
            guide[k] = index < target.size() ? target.midiF0[index] : 0.0;
            if (guide[k] <= 0.0)
                continue;
            ++voiced;
            lowMidi = std::min(lowMidi, guide[k]);
            highMidi = std::max(highMidi, guide[k]);
        }
        if (static_cast<double>(voiced) < 0.3 * static_cast<double>(frames))
            continue;
        const auto referenceStart = start - o.maxLagSec - referenceOffset;
        const auto salience = salienceOver(referenceStart, frames + 2 * lagSteps + 1, referenceRate, read, spectrum, o.hopSec,
                                             lowMidi - o.transposeSemitones - 1.2, highMidi + o.transposeSemitones + 1.2);
        auto score = lagScores(guide, salience, lagSteps, o.transposeSemitones);
        if (*std::max_element(score.begin(), score.end()) < o.minConfidence)
            continue;
        centres.push_back(start + 0.5 * o.windowSec);
        scores.push_back(std::move(score));
    }
    if (centres.empty())
        return {};
    const auto path = smoothestLags(scores, o.jumpCostPerSec * o.hopSec);
    std::vector<double> lags(path.size());
    for (size_t i = 0; i < path.size(); ++i)
        lags[i] = (static_cast<double>(path[i]) - static_cast<double>(lagSteps)) * o.hopSec;
    lags = shrunkTowardMedian(std::move(lags), o.toleranceSec);
    std::vector<double> to(centres.size());
    for (size_t i = 0; i < centres.size(); ++i)
        to[i] = centres[i] + lags[i];
    return TimeMap(std::move(centres), std::move(to));
}
