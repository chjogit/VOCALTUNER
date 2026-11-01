#include "GuidedMelody.h"
#include "MixSpectrum.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr size_t chunkFrames = 200;

struct Frame { double guide = 0.0; std::vector<double> salience; };

Frame scoreFrame(const std::vector<double>& spec, double guide, double binHz, const GuidedMelodyOptions& o)
{
    Frame f;
    f.guide = guide;
    if (guide <= 0.0)
        return f;
    const auto count = static_cast<int>(std::lround(2.0 * o.searchSemitones * 100.0 / o.stepCents)) + 1;
    f.salience.resize(static_cast<size_t>(count));
    for (int c = 0; c < count; ++c)
    {
        const auto hz = hzOf(guide - o.searchSemitones + c * o.stepCents / 100.0);
        f.salience[static_cast<size_t>(c)] = harmonicSalience(spec, hz, binHz, o.harmonics);
    }
    return f;
}
} // namespace

namespace
{
// 유성 구간 하나를 따라가는 비터비 (이전 비용과 되짚기 표만 들고 간다)
class Tracker
{
public:
    Tracker(MelodyTrack& outTrack, const GuidedMelodyOptions& o) : out(outTrack), options(o) {}

    void push(size_t index, const Frame& f)
    {
        if (f.salience.empty())
            return finish();
        if (cost.empty())
            begin = index;
        step(f);
        guides.push_back(f.guide);
        saliences.emplace_back(f.salience.begin(), f.salience.end());
    }

    void finish()
    {
        if (cost.empty())
            return;
        auto c = static_cast<size_t>(std::min_element(cost.begin(), cost.end()) - cost.begin());
        for (size_t t = guides.size(); t-- > 0;)
        {
            out.midi[begin + t] = static_cast<float>(midiOf(guides[t], c));
            out.evidence[begin + t] = evidenceOf(saliences[t], c);
            if (t > 0)
                c = back[t][c];
        }
        cost.clear();
        back.clear();
        guides.clear();
        saliences.clear();
    }

private:
    double midiOf(double guide, size_t c) const noexcept { return guide - options.searchSemitones + static_cast<double>(c) * options.stepCents / 100.0; }

    void step(const Frame& f)
    {
        const auto best = *std::max_element(f.salience.begin(), f.salience.end()) + 1e-12;
        const auto n = f.salience.size();
        std::vector<double> next(n);
        std::vector<uint8_t> from(n, 0);
        for (size_t c = 0; c < n; ++c)
        {
            const auto emission = 1.0 - f.salience[c] / best;
            if (cost.empty())
            {
                next[c] = emission;
                continue;
            }
            auto bestCost = std::numeric_limits<double>::max();
            for (size_t p = 0; p < cost.size(); ++p)
            {
                const auto jump = std::abs(midiOf(f.guide, c) - midiOf(guides.back(), p));
                const auto total = cost[p] + options.jumpCostPerSemitone * jump;
                if (total < bestCost) { bestCost = total; from[c] = static_cast<uint8_t>(p); }
            }
            next[c] = bestCost + emission;
        }
        const auto low = *std::min_element(next.begin(), next.end());
        for (auto& v : next)
            v -= low;           // 값이 커지지 않게
        cost = std::move(next);
        back.push_back(std::move(from));
    }

    MelodyTrack& out;
    const GuidedMelodyOptions& options;
    size_t begin = 0;
    // 고른 음이 부른 음(가이드 ±30센트)보다 원곡에서 얼마나 더 두드러지는지 (0 = 부른 음도 그만큼 있다)
    float evidenceOf(const std::vector<float>& salience, size_t chosen) const noexcept
    {
        const auto middle = salience.size() / 2;
        const auto near = static_cast<size_t>(std::lround(30.0 / options.stepCents));
        float atGuide = 0.0f;
        for (auto c = middle - std::min(middle, near); c <= std::min(salience.size() - 1, middle + near); ++c)
            atGuide = std::max(atGuide, salience[c]);
        return salience[chosen] > 1e-9f ? std::clamp(1.0f - atGuide / salience[chosen], 0.0f, 1.0f) : 0.0f;
    }

    std::vector<double> cost, guides;
    std::vector<std::vector<float>> saliences;
    std::vector<std::vector<uint8_t>> back;
};

// 점수 대비: 가장 높은 후보가 평균보다 얼마나 두드러지는지 (0..1)
float contrastOf(const Frame& f) noexcept
{
    if (f.salience.empty())
        return 0.0f;
    double sum = 0.0, best = 0.0;
    for (const auto s : f.salience) { sum += s; best = std::max(best, s); }
    const auto mean = sum / static_cast<double>(f.salience.size());
    return best > 1e-9 ? static_cast<float>((best - mean) / best) : 0.0f;
}
} // namespace

MelodyTrack extractGuidedMelody(int64_t lengthSamples, int sampleRate, const StereoReader& read,
                                const std::function<double(double)>& guideMidiAt, const GuidedMelodyOptions& o)
{
    MelodyTrack out;
    out.hopSec = o.hopSec;
    const auto hop = o.hopSec * sampleRate;
    const auto frames = static_cast<size_t>(static_cast<double>(lengthSamples) / hop) + 1;
    out.midi.assign(frames, 0.0f);
    out.confidence.assign(frames, 0.0f);
    out.evidence.assign(frames, 0.0f);
    size_t n = 256;
    while (n < static_cast<size_t>(o.frameSize))
        n <<= 1;
    MixSpectrum spectrum(n);
    Tracker tracker(out, o);
    const auto binHz = static_cast<double>(sampleRate) / static_cast<double>(n);

    for (size_t first = 0; first < frames; first += chunkFrames)
    {
        if (o.shouldCancel && o.shouldCancel())
            break;
        const auto last = std::min(frames, first + chunkFrames);
        const auto start = static_cast<int64_t>(std::lround(static_cast<double>(first) * hop)) - static_cast<int64_t>(n / 2);
        const auto count = static_cast<int>(std::lround(static_cast<double>(last - first) * hop)) + static_cast<int>(n) + 2;
        std::vector<float> left(static_cast<size_t>(count)), right(static_cast<size_t>(count));
        read(start, count, left.data(), right.data());
        for (auto t = first; t < last; ++t)
        {
            const auto offset = static_cast<size_t>(std::lround(static_cast<double>(t) * hop) - static_cast<double>(n / 2) - static_cast<double>(start));
            const auto spec = spectrum.centre(left.data() + offset, right.data() + offset);
            const auto frame = scoreFrame(spec, guideMidiAt(static_cast<double>(t) * o.hopSec), binHz, o);
            out.confidence[t] = contrastOf(frame);
            tracker.push(t, frame);
        }
    }
    tracker.finish();
    return out;
}

bool looksLikeMix(const std::vector<double>& rms)
{
    if (rms.size() < 200)
        return false;
    auto sorted = rms;
    const auto p95 = sorted.begin() + static_cast<long>(sorted.size() * 95 / 100);
    std::nth_element(sorted.begin(), p95, sorted.end());
    const auto quietBelow = *p95 * 0.0316;     // -30 dB
    const auto quiet = std::count_if(rms.begin(), rms.end(), [quietBelow](double v) { return v < quietBelow; });
    return static_cast<double>(quiet) < 0.08 * static_cast<double>(rms.size());   // 보컬 트랙은 쉬는 구간이 15~30%
}

double melodyPitchOver(const MelodyTrack& melody, double startSec, double endSec, const MelodyGate& gate)
{
    if (endSec - startSec < gate.minBlockSec || melody.size() == 0)
        return -1.0;
    const auto first = static_cast<size_t>(std::max(0.0, std::floor(startSec / melody.hopSec)));
    const auto last = std::min(melody.size(), static_cast<size_t>(std::max(0.0, std::floor(endSec / melody.hopSec))) + 1);
    if (last <= first)
        return -1.0;
    std::vector<float> good;
    for (auto t = first; t < last; ++t)
        if (melody.midi[t] > 0.0f && melody.evidence[t] >= gate.minEvidence)
            good.push_back(melody.midi[t]);
    if (good.size() < 3 || static_cast<double>(good.size()) < gate.minEvidenceShare * static_cast<double>(last - first))
        return -1.0;
    const auto mid = good.begin() + static_cast<long>(good.size() / 2);
    std::nth_element(good.begin(), mid, good.end());
    return *mid;
}
