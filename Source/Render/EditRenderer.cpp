#include "EditRenderer.h"
#include "ConsonantShield.h"
#include "Psola.h"
#include "PsolaWarp.h"
#include "PsolaGrain.h"
#include <atomic>
#include <memory>
#include "EditGrid.h"
#include "EditShiftCurves.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr double padSeconds = 0.25;

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

struct ChunkAudio { size_t startSample = 0; std::vector<std::vector<float>> channels; };

// 구간의 시작 샘플과 길이
struct SampleRange { size_t start = 0, count = 0; };

SampleRange sampleRangeOf(const AudioPlanes& src, const VocalFeatures& f, const ChunkSpan& span)
{
    const auto period = f.framePeriodMs / 1000.0;
    const auto at = [&](size_t frame) { return static_cast<size_t>(std::lround(static_cast<double>(frame) * period * src.sampleRate)); };
    return { at(span.begin), static_cast<size_t>(std::lround(static_cast<double>(span.end - span.begin) * period * src.sampleRate)) };
}

// 다시 합성한 소리는 바뀐 프레임 둘레에서만 쓰고, 나머지는 원본 그대로 (편집하지 않은 음까지 합성 음색이 되지 않게)
constexpr double renderedMarginSec = 0.03, renderedFadeSec = 0.02;

// 구간의 프레임마다 합성 소리를 쓰는 비율 (1 = 합성, 0 = 원본)
std::vector<double> renderedShare(const EditGrid& grid, const ChunkSpan& span, double tolerance, double period)
{
    const auto n = span.end - span.begin;
    std::vector<double> distance(n, 1e9);
    double since = 1e9;
    for (size_t i = 0; i < n; ++i)
    {
        since = frameChanges(grid, span.begin + i, tolerance) ? 0.0 : since + 1.0;
        distance[i] = since;
    }
    since = 1e9;
    for (size_t i = n; i-- > 0;)
    {
        since = distance[i] == 0.0 ? 0.0 : since + 1.0;
        distance[i] = std::min(distance[i], since);
    }
    std::vector<double> share(n);
    for (size_t i = 0; i < n; ++i)
        share[i] = std::clamp(1.0 - (distance[i] * period - renderedMarginSec) / renderedFadeSec, 0.0, 1.0);
    return share;
}

void keepSourceWhereUnchanged(ChunkAudio& out, const AudioPlanes& src, const std::vector<double>& share, double period)
{
    if (share.empty())
        return;
    const auto framesPerSample = 1.0 / (period * src.sampleRate);
    for (size_t c = 0; c < out.channels.size(); ++c)
        for (size_t i = 0; i < out.channels[c].size(); ++i)
        {
            const auto pos = static_cast<double>(i) * framesPerSample;
            const auto k = std::min(share.size() - 1, static_cast<size_t>(pos));
            const auto next = std::min(share.size() - 1, k + 1);
            const auto w = share[k] + (share[next] - share[k]) * (pos - static_cast<double>(k));
            const auto at = out.startSample + i;
            const auto original = at < src.channels[c].size() ? src.channels[c][at] : 0.0f;
            out.channels[c][i] = static_cast<float>(w * out.channels[c][i] + (1.0 - w) * original);
        }
}

// 채널마다 PSOLA로 [start, start + samples)를 합성
void psolaChannels(const AudioPlanes& src, const VocalFeatures& f, double period, const std::vector<double>& shift,
                   const std::vector<double>& formant, ChunkAudio& out, size_t samples)
{
    for (const auto& ch : src.channels)
    {
        const PsolaInput in { ch.data(), ch.size(), src.sampleRate, period, f.midiF0.data(), shift.data(), f.size(),
                              formant.empty() ? nullptr : formant.data() };
        auto y = psolaShift(in, out.startSample, out.startSample + samples);
        y.resize(samples, 0.0f);
        out.channels.push_back(std::move(y));
    }
}

ChunkAudio psolaChunk(const AudioPlanes& src, const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span, double tolerance)
{
    const auto period = f.framePeriodMs / 1000.0;
    const auto range = sampleRangeOf(src, f, span);
    ChunkAudio out;
    out.startSample = range.start;
    // 자음 보호: 첫 채널로 마찰음 같은 프레임을 찾아 그 프레임의 이동량을 0으로 (채널마다 다르면 소리가 갈라진다)
    const auto wide = spanToQuiet(f, grid, span);
    const auto shift = shieldedShift(src.channels[0].data(), src.channels[0].size(), src.sampleRate, period,
                                     psolaShiftCurve(f, grid, wide, tolerance), wide.begin, wide.end);
    psolaChannels(src, f, period, shift, psolaFormantCurve(f, grid, wide), out, range.count);
    return out;
}

// 박자가 바뀐 구간: 박자와 음정을 한 번에 PSOLA로 (원본 파형 조각을 옮겨 놓는다)
ChunkAudio warpChunk(const AudioPlanes& src, const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span, double tolerance)
{
    const auto period = f.framePeriodMs / 1000.0;
    const auto range = sampleRangeOf(src, f, span);
    ChunkAudio out;
    out.startSample = range.start;
    const auto samples = range.count;
    const auto shift = warpShiftCurve(src, f, grid, spanToQuiet(f, grid, span), tolerance);
    const auto* map = grid.timeMap.get();
    for (const auto& ch : src.channels)
    {
        const WarpInput in { ch.data(), ch.size(), src.sampleRate, period, f.midiF0.data(), f.size(), shift.data(), shift.size(),
                             [map](double t) { return map->toSource(t); }, [map](double s) { return map->toDisplay(s); },
                             grid.formantEdited && grid.formantAmount.size() == shift.size() ? grid.formantAmount.data() : nullptr };
        out.channels.push_back(psolaWarp(in, out.startSample, out.startSample + samples));
    }
    keepSourceWhereUnchanged(out, src, renderedShare(grid, span, tolerance, period), period);
    return out;
}

ChunkAudio renderChunk(const AudioPlanes& src, const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span, double tolerance)
{
    const auto period = f.framePeriodMs / 1000.0;
    const auto range = sampleRangeOf(src, f, span);
    ChunkAudio out;
    out.startSample = range.start;
    const auto samples = range.count;
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
    if (! grid.timingChanged)
        return psolaChunk(src, f, grid, span, tolerance);
    return warpChunk(src, f, grid, span, tolerance);
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
