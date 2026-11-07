#include "EditRenderer.h"
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

// 박자 변경 구간 안에서 '그대로 옮기기만 하는' 곳(대응 기울기가 1이고 음정 이동이 없는 곳: EditTimeMap이 숨·자음·잡음·어택을
// 고정으로 둔 조각)은 PSOLA 결과 대신 원본을 그 시간만큼 옮겨 그대로 쓴다. PSOLA는 유성 구간의 클릭·잡음을 조각마다 되풀이해
// 거슬리지 않던 녹음 잡음을 '틱'으로 만들었다. 경계는 5 ms로 섞는다.
void copyRigidMoves(ChunkAudio& out, const AudioPlanes& src, const EditGrid& grid, const std::vector<double>& shift, double period,
                    double tolerance)
{
    if (grid.timeMap == nullptr || out.channels.empty())
        return;
    const auto rate = static_cast<double>(src.sampleRate);
    const auto n = out.channels.front().size();
    std::vector<double> mask(n, 0.0);
    const auto probe = 0.004;
    for (size_t i = 0; i < n; ++i)
    {
        const auto t = static_cast<double>(out.startSample + i) / rate;
        const auto s = grid.timeMap->toSource(t);
        // 기울기가 1에서 0.3% 안이면(음정으로 5센트 안) 원본을 그 기울기로 읽어 그대로 쓴다. PSOLA로 만들면 원본 마크에 붙어 있다가
        // 쌓인 어긋남만큼 한 주기를 한꺼번에 건너뛰거나 되풀이해 그 자리가 '틱'이 된다
        constexpr double nearlyRigid = 0.003;
        const bool rigid = std::abs((grid.timeMap->toSource(t + probe) - s) / probe - 1.0) < nearlyRigid
                           && std::abs((s - grid.timeMap->toSource(t - probe)) / probe - 1.0) < nearlyRigid;
        const auto frame = std::min(shift.size() - 1, static_cast<size_t>(std::max(0.0, t / period)));
        mask[i] = rigid && (shift.empty() || std::abs(shift[frame]) <= tolerance) ? 1.0 : 0.0;
    }
    // 5 ms 이동 평균으로 경계를 부드럽게 (가장자리는 안쪽으로 줄어들도록 최소값 뒤 평균)
    const auto half = static_cast<size_t>(0.0025 * rate);
    std::vector<double> eroded(n, 0.0), smooth(n, 0.0), prefix(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i)
    {
        double m = 1.0;
        for (auto k = i >= half ? i - half : 0; k <= std::min(n - 1, i + half) && m > 0.0; ++k) m = std::min(m, mask[k]);
        eroded[i] = m;
    }
    for (size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + eroded[i];
    for (size_t i = 0; i < n; ++i)
    {
        const auto lo = i >= half ? i - half : 0, hi = std::min(n, i + half + 1);
        smooth[i] = (prefix[hi] - prefix[lo]) / static_cast<double>(hi - lo);
    }
    // 섞는 곳의 위상 맞춤: 복사로 들어가는(나오는) 경계마다, 그 앞(뒤) 10 ms에서 PSOLA 결과를 원본 복사에 가장 잘 맞추는
    // 어긋남 d(±1.5 ms)를 찾고, PSOLA 쪽을 경계 앞(뒤) 20 ms에 걸쳐 0 -> d로 서서히 옮겨 둔다. 그대로 섞으면 반 주기쯤 어긋난
    // 두 소리가 서로 지워 소리가 꺼지거나(끊김) 겹쳐 들린다(더블링)
    {
        const auto sourceAt = [&](const std::vector<float>& x, size_t i) {
            const auto pos = grid.timeMap->toSource(static_cast<double>(out.startSample + i) / rate) * rate;
            const auto k = static_cast<long>(std::floor(pos));
            if (k < 0 || k + 1 >= static_cast<long>(x.size())) return 0.0;
            const auto f = pos - static_cast<double>(k);
            return (1.0 - f) * x[static_cast<size_t>(k)] + f * x[static_cast<size_t>(k + 1)];
        };
        const auto window = static_cast<long>(0.010 * rate), ramp = static_cast<long>(0.020 * rate), reach = static_cast<long>(0.0015 * rate);
        const auto& y0 = out.channels.front();
        std::vector<std::pair<long, int>> edges;   // (경계 표본, +1 = 복사로 들어감, -1 = 복사에서 나옴)
        for (size_t i = 1; i < n; ++i)
        {
            if (smooth[i - 1] <= 0.0 && smooth[i] > 0.0) edges.push_back({ static_cast<long>(i), +1 });
            if (smooth[i - 1] > 0.0 && smooth[i] <= 0.0) edges.push_back({ static_cast<long>(i), -1 });
        }
        auto original = out.channels;   // 옮기기 전 PSOLA 결과
        for (const auto& [edge, direction] : edges)
        {
            // 비교 구간: 들어가는 경계면 [edge - window, edge), 나오는 경계면 [edge, edge + window)
            const auto a = direction > 0 ? edge - window : edge, b = direction > 0 ? edge : edge + window;
            if (a - reach < 0 || b + reach >= static_cast<long>(n)) continue;
            double best = -1e300;
            long shiftBy = 0;
            double energy = 0.0;
            for (auto j = a; j < b; ++j) energy += sourceAt(src.channels.front(), static_cast<size_t>(j)) * sourceAt(src.channels.front(), static_cast<size_t>(j));
            if (energy < 1e-6 * static_cast<double>(window)) continue;   // 조용한 곳은 위상이 상관없다
            for (auto d = -reach; d <= reach; ++d)
            {
                double c = 0.0, e = 0.0;
                for (auto j = a; j < b; ++j)
                {
                    const auto yv = static_cast<double>(y0[static_cast<size_t>(j + d)]);
                    c += yv * sourceAt(src.channels.front(), static_cast<size_t>(j));
                    e += yv * yv;
                }
                const auto score = c / std::sqrt(e + 1e-12);
                if (score > best) { best = score; shiftBy = d; }
            }
            if (shiftBy == 0) continue;
            // PSOLA 쪽을 r(i) * shiftBy 만큼 당겨 읽는다. r은 경계에서 1, ramp만큼 떨어지면 0
            for (size_t c = 0; c < out.channels.size(); ++c)
                for (long k = 0; k < ramp; ++k)
                {
                    const auto i = direction > 0 ? edge - 1 - k : edge + k;
                    if (i < 0 || i >= static_cast<long>(n)) break;
                    const auto r = 1.0 - static_cast<double>(k) / static_cast<double>(ramp);
                    const auto pos = static_cast<double>(i) + r * static_cast<double>(shiftBy);
                    const auto p = static_cast<long>(std::floor(pos));
                    if (p < 0 || p + 1 >= static_cast<long>(n)) continue;
                    const auto f = pos - static_cast<double>(p);
                    out.channels[c][static_cast<size_t>(i)] = static_cast<float>((1.0 - f) * original[c][static_cast<size_t>(p)] + f * original[c][static_cast<size_t>(p + 1)]);
                }
        }
    }
    for (size_t c = 0; c < out.channels.size(); ++c)
    {
        const auto& x = src.channels[c];
        for (size_t i = 0; i < n; ++i)
        {
            if (smooth[i] <= 0.0) continue;
            const auto pos = grid.timeMap->toSource(static_cast<double>(out.startSample + i) / rate) * rate;
            const auto k = static_cast<long>(std::floor(pos));
            if (k < 0 || k + 1 >= static_cast<long>(x.size())) continue;
            const auto f = pos - static_cast<double>(k);
            const auto original = (1.0 - f) * x[static_cast<size_t>(k)] + f * x[static_cast<size_t>(k + 1)];
            out.channels[c][i] = static_cast<float>((1.0 - smooth[i]) * out.channels[c][i] + smooth[i] * original);
        }
    }
}

// 채널마다 PSOLA로 [start, start + samples)를 합성
void psolaChannels(const AudioPlanes& src, const VocalFeatures& f, double period, const std::vector<double>& shift,
                   const std::vector<double>& formant, bool levelMatch, ChunkAudio& out, size_t samples)
{
    for (const auto& ch : src.channels)
    {
        const PsolaInput in { ch.data(), ch.size(), src.sampleRate, period, f.midiF0.data(), shift.data(), f.size(),
                              formant.empty() ? nullptr : formant.data(), levelMatch };
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
    // 자음 보호 (+ vowelFocus): 마찰음 같은 프레임의 이동량을 0으로, 모음이 아닌 프레임은 줄인다
    const auto wide = spanToQuiet(f, grid, span);
    const auto shift = protectedShift(src, f, grid, psolaShiftCurve(f, grid, wide, tolerance), wide.begin, wide.end);
    psolaChannels(src, f, period, shift, psolaFormantCurve(f, grid, wide), grid.levelMatch, out, range.count);
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
    // 복사 경계의 위상 맞춤은 경계 둘레 30 ms를 보므로, 앞뒤 50 ms를 더 만들어 처리한 뒤 잘라 낸다.
    // 그래야 어느 구간을 렌더하든(미리 듣기 구간 렌더 = 전체 렌더) 같은 표본이 같은 값이 된다
    const auto margin = static_cast<size_t>(0.05 * src.sampleRate);
    ChunkAudio wide;
    wide.startSample = out.startSample > margin ? out.startSample - margin : 0;
    const auto lead = out.startSample - wide.startSample;
    for (const auto& ch : src.channels)
    {
        const WarpInput in { ch.data(), ch.size(), src.sampleRate, period, f.midiF0.data(), f.size(), shift.data(), shift.size(),
                             [map](double t) { return map->toSource(t); }, [map](double s) { return map->toDisplay(s); },
                             grid.formantEdited && grid.formantAmount.size() == shift.size() ? grid.formantAmount.data() : nullptr,
                             grid.levelMatch };
        wide.channels.push_back(psolaWarp(in, wide.startSample, out.startSample + samples + margin));
    }
    copyRigidMoves(wide, src, grid, shift, period, tolerance);
    for (auto& ch : wide.channels)
    {
        ch.resize(std::max(ch.size(), lead + samples), 0.0f);
        out.channels.emplace_back(ch.begin() + static_cast<long>(lead), ch.begin() + static_cast<long>(lead + samples));
    }
    keepSourceWhereUnchanged(out, src, renderedShare(grid, span, tolerance, period), period);
    return out;
}

// 바뀌는 곳이 없는 구간: 원본 샘플을 그대로 복사
ChunkAudio sourceChunk(const AudioPlanes& src, size_t startSample, size_t samples)
{
    ChunkAudio out;
    out.startSample = startSample;
    for (const auto& ch : src.channels)
    {
        std::vector<float> seg(samples, 0.0f);
        for (size_t i = 0; i < samples && startSample + i < ch.size(); ++i)
            seg[i] = ch[startSample + i];
        out.channels.push_back(std::move(seg));
    }
    return out;
}

ChunkAudio renderChunk(const AudioPlanes& src, const VocalFeatures& f, const EditGrid& grid, const ChunkSpan& span, double tolerance)
{
    if (! rangeChanges(grid, span.begin, span.end, tolerance))
    {
        const auto range = sampleRangeOf(src, f, span);
        return sourceChunk(src, range.start, range.count);
    }
    return grid.timingChanged ? warpChunk(src, f, grid, span, tolerance) : psolaChunk(src, f, grid, span, tolerance);
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

static EditGrid gridFor(const AudioPlanes& source, const VocalFeatures& features, const std::vector<VocalBlock>& blocks, const RenderOptions& options)
{
    auto grid = buildGrid(source.length(), source.sampleRate, features, blocks);
    grid.levelMatch = options.levelMatch;
    grid.vowelFocus = options.vowelFocus;
    return grid;
}

std::optional<AudioPlanes> renderEdited(const AudioPlanes& source, const VocalFeatures& features, const std::vector<VocalBlock>& blocks,
                                        const RenderOptions& options)
{
    if (source.channels.empty() || features.size() == 0)
        return source;
    const auto period = features.framePeriodMs / 1000.0;
    const auto grid = gridFor(source, features, blocks, options);
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
    const auto grid = gridFor(source, features, blocks, options);
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
