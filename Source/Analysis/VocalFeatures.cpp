#include "VocalFeatures.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include "world/harvest.h"

namespace
{
struct ChunkPlan
{
    long long ownBegin = 0;     // 전체 프레임 인덱스 [ownBegin, ownEnd)
    long long ownEnd = 0;
    long long windowStartSample = 0;
    long long windowLength = 0;
    long long ownOffsetInWindow = 0;  // 윈도우 안에서 own 구간이 시작하는 프레임
};

bool isWholeNumber(double value) noexcept
{
    return std::abs(value - std::round(value)) < 1e-9;
}

// 프레임 격자(framePeriod)와 샘플 격자가 모두 정수 초에서 일치할 때만 구간 분할 가능
bool canSplit(const FeatureOptions& options) noexcept
{
    return options.chunkSeconds > 0 && options.overlapSeconds >= 0 && options.framePeriodMs > 0.0
        && isWholeNumber(1000.0 / options.framePeriodMs);
}

std::vector<ChunkPlan> planChunks(long long numSamples, int sampleRate, long long totalFrames,
                                  const FeatureOptions& options)
{
    if (! canSplit(options) || numSamples <= static_cast<long long>(options.chunkSeconds) * sampleRate)
        return { { 0, totalFrames, 0, numSamples, 0 } };

    const auto framesPerSecond = std::llround(1000.0 / options.framePeriodMs);
    const long long chunkFrames = options.chunkSeconds * framesPerSecond;
    std::vector<ChunkPlan> plans;

    for (long long begin = 0; begin < totalFrames; begin += chunkFrames)
    {
        const auto ownSec = begin / framesPerSecond;
        const auto windowStartSec = std::max<long long>(0, ownSec - options.overlapSeconds);
        const auto ownEnd = std::min(totalFrames, begin + chunkFrames);
        const auto windowEndSample = std::min(numSamples, ((ownEnd / framesPerSecond) + options.overlapSeconds + 1) * sampleRate);

        ChunkPlan plan;
        plan.ownBegin = begin;
        plan.ownEnd = ownEnd;
        plan.windowStartSample = windowStartSec * sampleRate;
        plan.windowLength = windowEndSample - plan.windowStartSample;
        plan.ownOffsetInWindow = begin - windowStartSec * framesPerSecond;
        plans.push_back(plan);
    }
    return plans;
}

// 윈도우를 Harvest로 분석해 own 구간의 F0(Hz)를 out에 기록
void harvestChunk(const std::vector<double>& mono, int sampleRate, const FeatureOptions& options,
                  const ChunkPlan& plan, std::vector<double>& out)
{
    HarvestOption harvestOption;
    InitializeHarvestOption(&harvestOption);
    harvestOption.frame_period = options.framePeriodMs;
    harvestOption.f0_floor = options.f0FloorHz;
    harvestOption.f0_ceil = options.f0CeilHz;

    const auto length = static_cast<int>(plan.windowLength);
    const auto windowFrames = static_cast<size_t>(GetSamplesForHarvest(sampleRate, length, options.framePeriodMs));
    std::vector<double> temporal(windowFrames, 0.0), f0(windowFrames, 0.0);
    Harvest(mono.data() + plan.windowStartSample, length, sampleRate, &harvestOption, temporal.data(), f0.data());

    for (long long frame = plan.ownBegin; frame < plan.ownEnd; ++frame)
    {
        const auto local = static_cast<size_t>(plan.ownOffsetInWindow + (frame - plan.ownBegin));
        out[static_cast<size_t>(frame)] = local < windowFrames ? f0[local] : 0.0;
    }
}

void runSequentially(int count, const std::function<void(int)>& body)
{
    for (int i = 0; i < count; ++i)
        body(i);
}

// 단조 증가를 보장하며 진행률 보고
class ProgressReporter
{
public:
    ProgressReporter(const std::function<void(float)>& callbackIn, int total) : callback(callbackIn), numTotal(total) {}

    void chunkDone()
    {
        const std::lock_guard<std::mutex> lock(mutex);
        ++numDone;
        if (callback)
            callback(static_cast<float>(numDone) / static_cast<float>(numTotal));
    }

private:
    const std::function<void(float)>& callback;
    int numTotal;
    int numDone = 0;
    std::mutex mutex;
};

std::vector<double> toMidi(const std::vector<double>& f0Hz)
{
    std::vector<double> midi(f0Hz.size(), 0.0);
    std::transform(f0Hz.begin(), f0Hz.end(), midi.begin(),
                   [](double hz) { return hz > 0.0 ? hzToMidi(hz) : 0.0; });
    return midi;
}

double frameRms(const std::vector<double>& mono, long long centre, int frameLength)
{
    // librosa center=True 와 동일하게 프레임 중심 기준, 범위 밖은 0 패딩으로 취급
    const auto begin = std::max(0LL, centre - frameLength / 2);
    const auto end = std::min(static_cast<long long>(mono.size()), centre + frameLength / 2);
    double sum = 0.0;
    for (auto i = begin; i < end; ++i)
        sum += mono[static_cast<size_t>(i)] * mono[static_cast<size_t>(i)];
    return std::sqrt(sum / frameLength);
}

std::vector<double> computeNormalisedRms(const std::vector<double>& mono, int sampleRate,
                                         const std::vector<double>& timeSec, int frameLength)
{
    std::vector<double> rms(timeSec.size(), 0.0);
    for (size_t i = 0; i < timeSec.size(); ++i)
        rms[i] = frameRms(mono, std::llround(timeSec[i] * sampleRate), frameLength);

    const auto peak = rms.empty() ? 0.0 : *std::max_element(rms.begin(), rms.end());
    for (auto& value : rms)
        value /= peak + 1e-6;
    return rms;
}
} // namespace

double hzToMidi(double hz) noexcept
{
    return 69.0 + 12.0 * std::log2(hz / 440.0);
}

std::optional<VocalFeatures> extractVocalFeatures(const std::vector<double>& mono, int sampleRate,
                                                  const FeatureOptions& options)
{
    VocalFeatures features;
    features.framePeriodMs = options.framePeriodMs;
    if (mono.empty() || sampleRate <= 0)
        return features;

    const auto totalFrames = static_cast<long long>(
        GetSamplesForHarvest(sampleRate, static_cast<int>(mono.size()), options.framePeriodMs));
    const auto plans = planChunks(static_cast<long long>(mono.size()), sampleRate, totalFrames, options);

    std::vector<double> f0Hz(static_cast<size_t>(totalFrames), 0.0);
    std::atomic<bool> cancelled { false };
    ProgressReporter progress(options.onProgress, static_cast<int>(plans.size()));

    const auto runChunk = [&](int index)
    {
        if (cancelled.load() || (options.shouldCancel && options.shouldCancel()))
        {
            cancelled.store(true);
            return;
        }
        harvestChunk(mono, sampleRate, options, plans[static_cast<size_t>(index)], f0Hz);
        progress.chunkDone();
    };

    const auto& run = options.parallelFor ? options.parallelFor : ParallelFor(runSequentially);
    run(static_cast<int>(plans.size()), runChunk);
    if (cancelled.load())
        return std::nullopt;

    features.timeSec.resize(f0Hz.size());
    for (size_t i = 0; i < f0Hz.size(); ++i)
        features.timeSec[i] = static_cast<double>(i) * options.framePeriodMs / 1000.0;
    features.midiF0 = toMidi(f0Hz);
    features.rms = computeNormalisedRms(mono, sampleRate, features.timeSec, options.rmsFrameLength);
    return features;
}
