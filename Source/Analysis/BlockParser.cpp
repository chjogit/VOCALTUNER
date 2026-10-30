#include "BlockParser.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
struct FrameRange
{
    size_t begin = 0;
    size_t end = 0;     // 배타적
    size_t size() const noexcept { return end - begin; }
};

bool isVoiced(const VocalFeatures& f, size_t i) noexcept { return f.midiF0[i] > 0.0; }

// ---------------------------------------------------------------- scipy 호환 헬퍼

// scipy.signal.medfilt 와 같은 윈도우 중앙값. 가장자리만 0 패딩 대신 끝 값 복제.
// Tests/crosscheck 의 무작위 300개 시퀀스에서 두 방식의 분할 결과는 동일했음 (영향 미미).
std::vector<double> medianFilter(const std::vector<double>& x, size_t kernel)
{
    const auto half = static_cast<long long>(kernel / 2);
    const auto n = static_cast<long long>(x.size());
    std::vector<double> out(x.size()), window(kernel);
    for (long long i = 0; i < n; ++i)
    {
        for (long long j = -half; j <= half; ++j)
            window[static_cast<size_t>(j + half)] = x[static_cast<size_t>(std::clamp(i + j, 0LL, n - 1))];
        std::nth_element(window.begin(), window.begin() + half, window.end());
        out[static_cast<size_t>(i)] = window[static_cast<size_t>(half)];
    }
    return out;
}

// np.abs(np.gradient(x, 3)): 내부는 중앙차분 / (2*3), 양끝은 단측차분 / 3
std::vector<double> absGradient(const std::vector<double>& x, double spacing)
{
    const auto n = x.size();
    std::vector<double> g(n, 0.0);
    if (n < 2)
        return g;
    g[0] = std::abs(x[1] - x[0]) / spacing;
    g[n - 1] = std::abs(x[n - 1] - x[n - 2]) / spacing;
    for (size_t i = 1; i + 1 < n; ++i)
        g[i] = std::abs(x[i + 1] - x[i - 1]) / (2.0 * spacing);
    return g;
}

// scipy.signal.find_peaks(x, height=, distance=) 와 같은 결과 (평탄한 봉우리는 가운데 인덱스)
std::vector<size_t> findPeaks(const std::vector<double>& x, double minHeight, size_t distance)
{
    std::vector<size_t> peaks;
    const auto n = x.size();
    for (size_t i = 1; i + 1 < n;)
    {
        if (x[i - 1] < x[i])
        {
            auto ahead = i + 1;
            while (ahead < n - 1 && x[ahead] == x[i])
                ++ahead;
            if (x[ahead] < x[i])
            {
                peaks.push_back((i + (ahead - 1)) / 2);
                i = ahead;
                continue;
            }
        }
        ++i;
    }

    peaks.erase(std::remove_if(peaks.begin(), peaks.end(), [&](size_t p) { return x[p] < minHeight; }), peaks.end());

    // 높은 봉우리부터 확정하고 distance 안의 낮은 봉우리를 제거 (동률이면 뒤쪽이 우선)
    std::vector<size_t> byHeight(peaks.size());
    std::iota(byHeight.begin(), byHeight.end(), size_t { 0 });
    std::stable_sort(byHeight.begin(), byHeight.end(), [&](size_t a, size_t b) { return x[peaks[a]] < x[peaks[b]]; });

    std::vector<char> keep(peaks.size(), 1);
    for (auto it = byHeight.rbegin(); it != byHeight.rend(); ++it)
    {
        const auto j = *it;
        if (! keep[j])
            continue;
        for (auto k = j; k-- > 0 && peaks[j] - peaks[k] < distance;)
            keep[k] = 0;
        for (auto k = j + 1; k < peaks.size() && peaks[k] - peaks[j] < distance; ++k)
            keep[k] = 0;
    }

    std::vector<size_t> result;
    for (size_t i = 0; i < peaks.size(); ++i)
        if (keep[i])
            result.push_back(peaks[i]);
    return result;
}

// ---------------------------------------------------------------- 구간 나누기

// 유성 프레임들을 짧은 무성 틈은 이어 붙여 구간으로 묶고, 너무 짧은 구간은 버림
std::vector<FrameRange> findVoicedRegions(const VocalFeatures& f, const BlockOptions& o)
{
    std::vector<FrameRange> regions;
    const auto n = f.size();
    size_t voicedCount = 0, lastVoiced = 0;
    bool open = false;
    FrameRange current;

    const auto close = [&]
    {
        if (open && voicedCount >= static_cast<size_t>(o.minVoicedFrames))
            regions.push_back({ current.begin, lastVoiced + 1 });
        open = false;
        voicedCount = 0;
    };

    for (size_t i = 0; i < n; ++i)
    {
        if (! isVoiced(f, i))
        {
            if (open && i - lastVoiced > static_cast<size_t>(o.maxBridgeFrames))
                close();
            continue;
        }
        if (! open)
        {
            open = true;
            current.begin = i;
        }
        lastVoiced = i;
        ++voicedCount;
    }
    close();
    return regions;
}

// split_by_pitch_steps(): 구간 안에서 음정이 계단식으로 꺾이는 지점으로 하위 구간 분할
std::vector<FrameRange> splitByPitchSteps(const VocalFeatures& f, FrameRange region, const BlockOptions& o)
{
    std::vector<size_t> voicedIndex;   // region.begin 기준 상대 인덱스
    std::vector<double> voicedMidi;
    for (size_t i = region.begin; i < region.end; ++i)
        if (isVoiced(f, i))
        {
            voicedIndex.push_back(i - region.begin);
            voicedMidi.push_back(f.midiF0[i]);
        }

    if (voicedMidi.size() < static_cast<size_t>(o.minVoicedFrames))
        return {};

    const auto kernel = std::min<size_t>(11, voicedMidi.size() / 2 * 2 + 1);
    const auto slope = absGradient(medianFilter(voicedMidi, kernel), 3.0);
    const auto peaks = findPeaks(slope, o.stepPeakHeight, static_cast<size_t>(o.stepPeakDistance));

    std::vector<size_t> splitPoints { 0 };
    for (const auto p : peaks)
        splitPoints.push_back(voicedIndex[p]);
    splitPoints.push_back(region.size());

    std::vector<FrameRange> parts;
    for (size_t i = 0; i + 1 < splitPoints.size(); ++i)
    {
        const FrameRange part { region.begin + splitPoints[i], region.begin + splitPoints[i + 1] };
        if (part.size() < static_cast<size_t>(o.minSubBlockFrames))
            continue;
        const auto anyVoiced = std::any_of(f.midiF0.begin() + static_cast<long>(part.begin),
                                           f.midiF0.begin() + static_cast<long>(part.end),
                                           [](double m) { return m > 0.0; });
        if (anyVoiced)
            parts.push_back(part);
    }
    return parts;
}

// 고른 곡선의 앞/뒤 평균 차이가 가장 큰 곳을 찾아, 충분히 다르면 둘로 나누고 각각 다시 살핀다.
// 비브라토는 고르기(중앙값 21프레임)로 대부분 사라지고 앞/뒤 평균도 비슷해 나뉘지 않는다.
void splitByPlateaus(const VocalFeatures& f, FrameRange part, const BlockOptions& o, std::vector<FrameRange>& out)
{
    std::vector<size_t> index;
    std::vector<double> midi;
    for (size_t i = part.begin; i < part.end; ++i)
        if (isVoiced(f, i))
        {
            index.push_back(i);
            midi.push_back(f.midiF0[i]);
        }
    const auto minSide = static_cast<size_t>(o.minPlateauFrames);
    if (midi.size() < 2 * minSide)
        return out.push_back(part);

    const auto smooth = medianFilter(midi, std::min<size_t>(21, midi.size() / 2 * 2 + 1));
    std::vector<double> prefix(smooth.size() + 1, 0.0);
    for (size_t i = 0; i < smooth.size(); ++i)
        prefix[i + 1] = prefix[i] + smooth[i];
    const auto mean = [&](size_t a, size_t b) { return (prefix[b] - prefix[a]) / static_cast<double>(b - a); };

    size_t best = 0;
    double bestGap = 0.0;
    for (auto k = minSide; k + minSide <= smooth.size(); ++k)
        if (const auto gap = std::abs(mean(0, k) - mean(k, smooth.size())); gap > bestGap)
        {
            bestGap = gap;
            best = k;
        }
    if (bestGap < o.plateauSplitSemitones)
        return out.push_back(part);
    splitByPlateaus(f, { part.begin, index[best] }, o, out);
    splitByPlateaus(f, { index[best], part.end }, o, out);
}

// ---------------------------------------------------------------- 자음

// from에서 step(+1/-1) 방향으로, 무성이면서 에너지가 있는 프레임을 최대 maxFrames개 센다
size_t countConsonantFrames(const VocalFeatures& f, const BlockOptions& o, size_t from, long step, size_t limit)
{
    size_t count = 0;
    auto i = static_cast<long long>(from);
    while (count < static_cast<size_t>(o.maxConsonantFrames) && i >= 0 && i < static_cast<long long>(f.size())
           && static_cast<size_t>(i) != limit && ! isVoiced(f, static_cast<size_t>(i))
           && f.rms[static_cast<size_t>(i)] >= o.consonantMinRms)
    {
        ++count;
        i += step;
    }
    return count >= static_cast<size_t>(o.minConsonantFrames) ? count : 0;
}

// ---------------------------------------------------------------- 블록 조립

struct RegionBlocks
{
    FrameRange region;
    std::vector<FrameRange> voiced;
    FrameRange leading;     // 비어 있으면 size()==0
    FrameRange trailing;
};

void fillPitchStats(const VocalFeatures& f, FrameRange range, VocalBlock& block)
{
    std::vector<double> voiced;
    for (auto i = range.begin; i < range.end; ++i)
        if (isVoiced(f, i))
            voiced.push_back(f.midiF0[i]);

    std::nth_element(voiced.begin(), voiced.begin() + static_cast<long>(voiced.size() / 2), voiced.end());
    auto median = voiced[voiced.size() / 2];
    if (voiced.size() % 2 == 0)   // numpy median: 짝수 개면 가운데 두 값의 평균
        median = 0.5 * (median + *std::max_element(voiced.begin(), voiced.begin() + static_cast<long>(voiced.size() / 2)));

    block.baseMidi = static_cast<int>(std::lround(median));
    block.pitchOffset = median - block.baseMidi;
}

VocalBlock makeBlock(const VocalFeatures& f, FrameRange range)
{
    VocalBlock block;
    block.beginFrame = range.begin;
    block.endFrame = range.end;
    block.startSec = f.timeSec[range.begin];
    block.endSec = f.timeSec[range.end - 1] + f.framePeriodMs / 1000.0;
    return block;
}
} // namespace

std::vector<VocalBlock> parseBlocks(const VocalFeatures& features, const BlockOptions& options)
{
    if (features.size() == 0 || features.midiF0.size() != features.size() || features.rms.size() != features.size())
        return {};

    std::vector<RegionBlocks> regions;
    for (const auto& region : findVoicedRegions(features, options))
    {
        std::vector<FrameRange> voiced;
        for (const auto& part : splitByPitchSteps(features, region, options))
            splitByPlateaus(features, part, options, voiced);
        if (! voiced.empty())
            regions.push_back({ region, std::move(voiced), {}, {} });
    }

    // 앞 자음을 먼저 확정하고, 뒤 자음은 다음 구간의 앞 자음을 침범하지 않게 제한
    for (size_t r = 0; r < regions.size(); ++r)
    {
        const auto begin = regions[r].voiced.front().begin;
        const auto floor = r > 0 ? regions[r - 1].region.end : 0;
        const auto count = begin > floor ? countConsonantFrames(features, options, begin - 1, -1, floor - 1) : 0;
        regions[r].leading = { begin - count, begin };
    }
    if (options.includeTrailingConsonants)
        for (size_t r = 0; r < regions.size(); ++r)
        {
            const auto end = regions[r].voiced.back().end;
            const auto ceiling = r + 1 < regions.size() ? regions[r + 1].leading.begin : features.size();
            const auto count = end < ceiling ? countConsonantFrames(features, options, end, +1, ceiling) : 0;
            regions[r].trailing = { end, end + count };
        }

    std::vector<VocalBlock> blocks;
    const auto push = [&](VocalBlock block) { block.id = static_cast<int>(blocks.size()); blocks.push_back(std::move(block)); };

    for (const auto& r : regions)
    {
        VocalBlock first = makeBlock(features, r.voiced.front());
        fillPitchStats(features, r.voiced.front(), first);

        if (r.leading.size() > 0)
        {
            auto consonant = makeBlock(features, r.leading);
            consonant.isConsonant = true;
            consonant.baseMidi = first.baseMidi;
            push(std::move(consonant));
        }

        for (size_t i = 0; i < r.voiced.size(); ++i)
        {
            auto block = makeBlock(features, r.voiced[i]);
            fillPitchStats(features, r.voiced[i], block);
            block.isContinuation = i > 0;
            push(std::move(block));
        }

        if (r.trailing.size() > 0)
        {
            auto consonant = makeBlock(features, r.trailing);
            consonant.isConsonant = true;
            consonant.baseMidi = blocks.back().baseMidi;
            push(std::move(consonant));
        }
    }
    return blocks;
}
