#include "EditRenderer.h"
#include <atomic>
#include "BlockTransforms.h"
#include "EditTimeMap.h"
#include <algorithm>
#include <cmath>
#include <world/cheaptrick.h>
#include <world/d4c.h>
#include <world/synthesis.h>

namespace
{
constexpr double padSeconds = 0.25;
constexpr size_t maxGapFrames = 8;

double midiToHz(double midi) noexcept { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

// 표시 시간 격자(프레임 i = i * 주기) 위에서 본 편집 결과
struct EditGrid
{
    std::vector<int> sourceFrame;       // 프레임 i가 가져오는 원본 프레임
    std::vector<double> targetMidi;     // 0 = 무성
    std::vector<double> originalMidi;   // 같은 프레임에서 원본이 가진 음정
    std::vector<double> gainDb;
    int lastSourceFrame = 0;
    size_t size() const noexcept { return targetMidi.size(); }
};

size_t frameCountFor(size_t samples, int sampleRate, double periodSec) noexcept
{
    return static_cast<size_t>(std::ceil(static_cast<double>(samples) / sampleRate / periodSec)) + 1;
}

void fillSourceFrames(EditGrid& grid, const VocalFeatures& f, const EditTimeMap& map, double period)
{
    const auto last = static_cast<int>(f.size()) - 1;
    for (size_t i = 0; i < grid.size(); ++i)
    {
        const auto j = static_cast<int>(std::lround(map.toSource(static_cast<double>(i) * period) / period));
        grid.sourceFrame[i] = std::clamp(j, 0, last);
        grid.targetMidi[i] = f.midiF0[static_cast<size_t>(grid.sourceFrame[i])];
        grid.originalMidi[i] = f.midiF0[std::min(i, f.size() - 1)];
    }
}

void scatterBlockPitch(EditGrid& grid, const VocalFeatures& f, const std::vector<VocalBlock>& blocks, size_t index, double period)
{
    const auto& b = blocks[index];
    long previousIndex = -1;
    double previousMidi = 0.0;
    for (auto frame = b.beginFrame; frame < std::min(b.endFrame, f.size()); ++frame)
    {
        const auto midi = editedPitchAt(f, blocks, index, frame);
        if (midi <= 0.0)
            continue;
        const auto at = std::lround(curveFrameDisplaySec(f, b, frame) / period);
        if (at < 0 || static_cast<size_t>(at) >= grid.size())
            continue;
        if (previousIndex >= 0 && at - previousIndex > 1 && static_cast<size_t>(at - previousIndex) <= maxGapFrames)
            for (auto k = previousIndex + 1; k < at; ++k)
                grid.targetMidi[static_cast<size_t>(k)] =
                    previousMidi + (midi - previousMidi) * static_cast<double>(k - previousIndex) / static_cast<double>(at - previousIndex);
        grid.targetMidi[static_cast<size_t>(at)] = midi;
        previousIndex = at;
        previousMidi = midi;
    }
}

void fillBlockGain(EditGrid& grid, const std::vector<VocalBlock>& blocks, size_t index, double period)
{
    const auto& b = blocks[index];
    const auto first = std::max(0L, std::lround(b.displayStartSec() / period));
    const auto last = std::min(static_cast<long>(grid.size()), std::lround(b.displayEndSec() / period));
    for (auto i = first; i < last; ++i)
        grid.gainDb[static_cast<size_t>(i)] = effectiveGainDb(blocks, index, static_cast<double>(i) * period);
}

EditGrid buildGrid(size_t samples, int sampleRate, const VocalFeatures& f, const std::vector<VocalBlock>& blocks)
{
    const auto period = f.framePeriodMs / 1000.0;
    EditGrid grid;
    const auto n = f.size() == 0 ? size_t{0} : frameCountFor(samples, sampleRate, period);
    grid.sourceFrame.assign(n, 0);
    grid.targetMidi.assign(n, 0.0);
    grid.originalMidi.assign(n, 0.0);
    grid.gainDb.assign(n, 0.0);
    if (n == 0)
        return grid;
    grid.lastSourceFrame = static_cast<int>(f.size()) - 1;
    fillSourceFrames(grid, f, EditTimeMap(blocks), period);
    for (size_t k = 0; k < blocks.size(); ++k)
    {
        scatterBlockPitch(grid, f, blocks, k, period);
        fillBlockGain(grid, blocks, k, period);
    }
    return grid;
}

bool frameChanges(const EditGrid& g, size_t i, double tolerance) noexcept
{
    return g.sourceFrame[i] != std::min(static_cast<int>(i), g.lastSourceFrame) || std::abs(g.targetMidi[i] - g.originalMidi[i]) > tolerance;
}

bool rangeChanges(const EditGrid& g, size_t begin, size_t end, double tolerance) noexcept
{
    for (auto i = begin; i < end; ++i)
        if (frameChanges(g, i, tolerance))
            return true;
    return false;
}

struct ChunkSpan { size_t coreBegin, coreEnd, begin, end; };   // 프레임 단위

std::vector<ChunkSpan> splitChunks(size_t frames, double period, double chunkSeconds)
{
    const auto perChunk = std::max<size_t>(1, static_cast<size_t>(chunkSeconds / period));
    const auto count = std::max<size_t>(1, frames / perChunk);
    const auto pad = static_cast<size_t>(padSeconds / period);
    std::vector<ChunkSpan> out;
    for (size_t k = 0; k < count; ++k)
    {
        const auto a = k * frames / count, b = (k + 1) * frames / count;
        out.push_back({a, b, a > pad ? a - pad : 0, std::min(frames, b + pad)});
    }
    return out;
}

struct WorldFrames
{
    std::vector<std::vector<double>> spectrogram, aperiodicity;
    size_t firstFrame = 0;
};

WorldFrames analyzeFrames(const std::vector<float>& channel, int sampleRate, const VocalFeatures& f, size_t jlo, size_t jhi, int fftSize)
{
    const auto period = f.framePeriodMs / 1000.0;
    const auto margin = static_cast<long>(0.1 * sampleRate);
    const auto sBegin = std::max(0L, std::lround(f.timeSec[jlo] * sampleRate) - margin);
    const auto sEnd = std::min(static_cast<long>(channel.size()), std::lround(f.timeSec[jhi] * sampleRate) + margin);
    std::vector<double> x(channel.begin() + sBegin, channel.begin() + std::max(sBegin, sEnd));
    const auto count = jhi - jlo + 1;
    std::vector<double> times(count), f0(count);
    for (size_t k = 0; k < count; ++k)
    {
        times[k] = f.timeSec[jlo + k] - static_cast<double>(sBegin) / sampleRate;
        f0[k] = f.midiF0[jlo + k] > 0.0 ? midiToHz(f.midiF0[jlo + k]) : 0.0;
    }
    (void) period;
    WorldFrames out;
    out.firstFrame = jlo;
    const auto bins = static_cast<size_t>(fftSize / 2 + 1);
    out.spectrogram.assign(count, std::vector<double>(bins));
    out.aperiodicity.assign(count, std::vector<double>(bins));
    std::vector<double*> sp(count), ap(count);
    for (size_t k = 0; k < count; ++k) { sp[k] = out.spectrogram[k].data(); ap[k] = out.aperiodicity[k].data(); }
    CheapTrickOption ct;
    InitializeCheapTrickOption(sampleRate, &ct);
    ct.fft_size = fftSize;
    D4COption d4;
    InitializeD4COption(&d4);
    CheapTrick(x.data(), static_cast<int>(x.size()), sampleRate, times.data(), f0.data(), static_cast<int>(count), &ct, sp.data());
    D4C(x.data(), static_cast<int>(x.size()), sampleRate, times.data(), f0.data(), static_cast<int>(count), fftSize, &d4, ap.data());
    return out;
}

// 풀스케일(1.0)에 가까워지면 부드럽게 눌러 디지털 클리핑(터짐)을 막는다. 0.9 아래는 그대로.
std::vector<float> softLimited(const std::vector<double>& y)
{
    constexpr double knee = 0.9;
    std::vector<float> out(y.size());
    for (size_t i = 0; i < y.size(); ++i)
    {
        const auto a = std::abs(y[i]);
        const auto limited = a <= knee ? a : knee + (1.0 - knee) * std::tanh((a - knee) / (1.0 - knee));
        out[i] = static_cast<float>(y[i] < 0.0 ? -limited : limited);
    }
    return out;
}

double windowRms(const double* x, size_t length, long centre, long radius) noexcept
{
    const auto lo = std::max(0L, centre - radius), hi = std::min(static_cast<long>(length), centre + radius + 1);
    double sum = 0.0;
    for (auto i = lo; i < hi; ++i)
        sum += x[i] * x[i];
    return hi > lo ? std::sqrt(sum / static_cast<double>(hi - lo)) : 0.0;
}

// WORLD 합성은 원본보다 크게 나오는 경우가 많다(큰 소리에서 특히). 프레임마다 원본의 짧은 구간 음량에 맞춘다.
// 이렇게 하지 않으면 큰 녹음에서 합성 구간이 풀스케일을 넘어 터진다.
void matchLoudnessToSource(std::vector<double>& y, const std::vector<float>& channel, int sampleRate, double period,
                           const EditGrid& grid, const ChunkSpan& span)
{
    const auto n = span.end - span.begin;
    const auto radius = static_cast<long>(0.012 * sampleRate);
    const auto hop = period * sampleRate;
    std::vector<double> sourceDouble;
    std::vector<double> gains(n, 1.0);
    for (size_t i = 0; i < n; ++i)
    {
        const auto sourceCentre = std::lround(static_cast<double>(grid.sourceFrame[span.begin + i]) * hop);
        const auto outCentre = std::lround(static_cast<double>(i) * hop);
        double source = 0.0;
        {
            const auto lo = std::max(0L, sourceCentre - radius), hi = std::min(static_cast<long>(channel.size()), sourceCentre + radius + 1);
            double sum = 0.0;
            for (auto k = lo; k < hi; ++k)
                sum += static_cast<double>(channel[static_cast<size_t>(k)]) * channel[static_cast<size_t>(k)];
            source = hi > lo ? std::sqrt(sum / static_cast<double>(hi - lo)) : 0.0;
        }
        const auto made = windowRms(y.data(), y.size(), outCentre, radius);
        gains[i] = made > 1e-7 && source > 1e-7 ? std::clamp(source / made, 0.1, 4.0) : 1.0;
    }
    // 급한 변화를 피하려고 ±3프레임으로 고르게
    std::vector<double> smooth(n, 1.0);
    for (size_t i = 0; i < n; ++i)
    {
        const auto lo = i > 3 ? i - 3 : 0, hi = std::min(n - 1, i + 3);
        double sum = 0.0;
        for (auto k = lo; k <= hi; ++k)
            sum += gains[k];
        smooth[i] = sum / static_cast<double>(hi - lo + 1);
    }
    for (size_t p = 0; p < y.size(); ++p)
    {
        const auto pos = static_cast<double>(p) / hop;
        const auto i = std::min(n - 1, static_cast<size_t>(pos));
        const auto j = std::min(n - 1, i + 1);
        y[p] *= smooth[i] + (smooth[j] - smooth[i]) * (pos - static_cast<double>(i));
    }
}

std::vector<float> synthesizeChunk(const std::vector<float>& channel, int sampleRate, const VocalFeatures& f, const EditGrid& grid,
                                   const ChunkSpan& span, int fftSize, const WorldFrames& frames)
{
    const auto n = span.end - span.begin;
    std::vector<double> f0(n);
    std::vector<const double*> sp(n), ap(n);
    for (size_t i = 0; i < n; ++i)
    {
        const auto m = grid.targetMidi[span.begin + i];
        f0[i] = m > 0.0 ? midiToHz(m) : 0.0;
        const auto row = static_cast<size_t>(grid.sourceFrame[span.begin + i]) - frames.firstFrame;
        sp[i] = frames.spectrogram[row].data();
        ap[i] = frames.aperiodicity[row].data();
    }
    const auto period = f.framePeriodMs / 1000.0;
    const auto samples = static_cast<size_t>(std::lround(static_cast<double>(n) * period * sampleRate));
    std::vector<double> y(samples, 0.0);
    Synthesis(f0.data(), static_cast<int>(n), sp.data(), ap.data(), fftSize, f.framePeriodMs, sampleRate, static_cast<int>(samples), y.data());
    matchLoudnessToSource(y, channel, sampleRate, period, grid, span);
    return softLimited(y);
}

struct ChunkAudio { size_t startSample = 0; std::vector<std::vector<float>> channels; };

ChunkAudio renderChunk(const AudioPlanes& src, const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span, double tolerance)
{
    const auto period = f.framePeriodMs / 1000.0;
    ChunkAudio out;
    out.startSample = static_cast<size_t>(std::lround(static_cast<double>(span.begin) * period * src.sampleRate));
    const auto samples = static_cast<size_t>(std::lround(static_cast<double>(span.end - span.begin) * period * src.sampleRate));
    if (! rangeChanges(grid, span.begin, span.end, tolerance))
    {
        for (const auto& ch : src.channels)
        {
            std::vector<float> seg(samples, 0.0f);
            for (size_t i = 0; i < samples && out.startSample + i < ch.size(); ++i)
                seg[i] = ch[out.startSample + i];
            out.channels.push_back(std::move(seg));
        }
        return out;
    }
    const auto [lo, hi] = std::minmax_element(grid.sourceFrame.begin() + static_cast<long>(span.begin), grid.sourceFrame.begin() + static_cast<long>(span.end));
    CheapTrickOption ct;
    InitializeCheapTrickOption(src.sampleRate, &ct);
    const auto fftSize = GetFFTSizeForCheapTrick(src.sampleRate, &ct);
    for (const auto& ch : src.channels)
    {
        const auto frames = analyzeFrames(ch, src.sampleRate, f, static_cast<size_t>(*lo), static_cast<size_t>(*hi), fftSize);
        out.channels.push_back(synthesizeChunk(ch, src.sampleRate, f, grid, span, fftSize, frames));
    }
    return out;
}

double chunkWeight(size_t sample, const ChunkSpan& span, bool first, bool last, double period, int sampleRate) noexcept
{
    const auto toSample = [&](size_t frame) { return static_cast<double>(frame) * period * sampleRate; };
    const auto pad = padSeconds * sampleRate;
    const auto p = static_cast<double>(sample);
    constexpr double halfPi = 1.5707963267948966;
    auto w = 1.0;
    const auto start = toSample(span.coreBegin), end = toSample(span.coreEnd);
    if (! first && p < start + pad)
        w *= std::pow(std::sin(halfPi * std::clamp((p - (start - pad)) / (2.0 * pad), 0.0, 1.0)), 2.0);
    if (! last && p > end - pad)
        w *= std::pow(std::cos(halfPi * std::clamp((p - (end - pad)) / (2.0 * pad), 0.0, 1.0)), 2.0);
    return w;
}

void accumulate(AudioPlanes& out, const ChunkAudio& chunk, const ChunkSpan& span, bool first, bool last, double period)
{
    for (size_t c = 0; c < chunk.channels.size(); ++c)
        for (size_t i = 0; i < chunk.channels[c].size(); ++i)
        {
            const auto p = chunk.startSample + i;
            if (p < out.channels[c].size())
                out.channels[c][p] += chunk.channels[c][i] * static_cast<float>(chunkWeight(p, span, first, last, period, out.sampleRate));
        }
}

// channels[c][0]이 출력의 startSample 번째 샘플
void applyGainAt(std::vector<std::vector<float>>& channels, size_t startSample, int sampleRate, const EditGrid& grid, double period)
{
    if (std::all_of(grid.gainDb.begin(), grid.gainDb.end(), [](double g) { return std::abs(g) < 1e-6; }))
        return;
    const auto last = grid.size() - 1;
    for (auto& ch : channels)
        for (size_t p = 0; p < ch.size(); ++p)
        {
            const auto pos = static_cast<double>(startSample + p) / sampleRate / period;
            const auto i = std::min(last, static_cast<size_t>(pos));
            const auto j = std::min(last, i + 1);
            const auto db = grid.gainDb[i] + (grid.gainDb[j] - grid.gainDb[i]) * (pos - static_cast<double>(i));
            ch[p] *= static_cast<float>(std::pow(10.0, db / 20.0));
        }
}
} // namespace

bool changesAudio(const std::vector<VocalBlock>& blocks, const VocalFeatures& features, double unchangedSemitones)
{
    if (features.size() == 0)
        return false;
    const auto samples = static_cast<size_t>(features.timeSec.back() * 44100.0) + 1;
    const auto grid = buildGrid(samples, 44100, features, blocks);
    const auto gainChanges = std::any_of(grid.gainDb.begin(), grid.gainDb.end(), [](double g) { return std::abs(g) > 1e-6; });
    return gainChanges || rangeChanges(grid, 0, grid.size(), unchangedSemitones);
}

std::optional<AudioPlanes> renderEdited(const AudioPlanes& source, const VocalFeatures& features, const std::vector<VocalBlock>& blocks,
                                        const RenderOptions& options)
{
    if (source.channels.empty() || features.size() == 0)
        return source;
    const auto period = features.framePeriodMs / 1000.0;
    const auto grid = buildGrid(source.length(), source.sampleRate, features, blocks);
    const auto spans = splitChunks(grid.size(), period, options.chunkSeconds);
    std::vector<ChunkAudio> chunks(spans.size());
    std::atomic<bool> cancelled{false};
    const auto job = [&](int k)
    {
        if (cancelled || (options.shouldCancel && options.shouldCancel()))
        {
            cancelled = true;
            return;
        }
        chunks[static_cast<size_t>(k)] = renderChunk(source, features, grid, spans[static_cast<size_t>(k)], options.unchangedSemitones);
    };
    if (options.parallelFor)
        options.parallelFor(static_cast<int>(spans.size()), job);
    else
        for (int k = 0; k < static_cast<int>(spans.size()); ++k)
            job(k);
    if (cancelled)
        return std::nullopt;

    AudioPlanes out;
    out.sampleRate = source.sampleRate;
    out.channels.assign(source.channels.size(), std::vector<float>(source.length(), 0.0f));
    for (size_t k = 0; k < spans.size(); ++k)
        accumulate(out, chunks[k], spans[k], k == 0, k + 1 == spans.size(), period);
    applyGainAt(out.channels, 0, out.sampleRate, grid, period);
    return out;
}

std::optional<RenderedSegment> renderEditedRange(const AudioPlanes& source, const VocalFeatures& features,
                                                 const std::vector<VocalBlock>& blocks, size_t beginSample, size_t endSample,
                                                 const RenderOptions& options)
{
    if (source.channels.empty() || features.size() == 0 || beginSample >= std::min(endSample, source.length()))
        return RenderedSegment{};
    if (options.shouldCancel && options.shouldCancel())
        return std::nullopt;
    const auto period = features.framePeriodMs / 1000.0;
    const auto grid = buildGrid(source.length(), source.sampleRate, features, blocks);
    const auto toFrame = [&](size_t sample) { return std::min(grid.size(), static_cast<size_t>(static_cast<double>(sample) / source.sampleRate / period)); };
    const auto pad = static_cast<size_t>(padSeconds / period);
    const auto a = toFrame(beginSample), b = std::max(a + 1, toFrame(endSample) + 1);
    const ChunkSpan span{a, b, a > pad ? a - pad : 0, std::min(grid.size(), b + pad)};
    auto chunk = renderChunk(source, features, grid, span, options.unchangedSemitones);
    RenderedSegment out;
    out.startSample = chunk.startSample;
    out.audio.sampleRate = source.sampleRate;
    for (auto& ch : chunk.channels)
        ch.resize(std::min(ch.size(), source.length() - std::min(source.length(), chunk.startSample)));
    out.audio.channels = std::move(chunk.channels);
    applyGainAt(out.audio.channels, out.startSample, source.sampleRate, grid, period);
    return out;
}
