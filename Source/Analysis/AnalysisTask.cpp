#include "AnalysisTask.h"
#include "WhisperRunner.h"

namespace
{
constexpr int chunkSize = 1 << 16;
constexpr float readProgressShare = 0.3f;   // 나머지 70%는 Harvest 구간 처리
}

AnalysisTask::AnalysisTask(juce::ARAAudioSource& sourceIn, bool hostProgress)
    : source(sourceIn),
      reader(std::make_unique<juce::ARAAudioSourceReader>(&sourceIn)),
      numSamples(sourceIn.getSampleCount()),
      sampleRate(juce::roundToInt(sourceIn.getSampleRate())),
      reportsToHost(hostProgress)
{
}

std::optional<AlignmentData> AnalysisTask::runAlignment(const VocalFeatures& features, ParallelFor parallelFor)
{
    std::vector<double> mono;
    if (! readMono(mono) || isCancelled())
        return std::nullopt;

    AlignmentData data;
    data.frames = computeSpectralFrames(mono, sampleRate, 0.01, parallelFor);
    if (isCancelled() || data.frames.size() == 0)
        return std::nullopt;
    data.kinds = classifyFrames(data.frames, features);
    if (const auto whisper = locateWhisper())
    {
        data.words = transcribeWords(mono, sampleRate, *whisper, [this] { return isCancelled(); });
        data.whisperUsed = ! data.words.empty();
    }
    return isCancelled() ? std::nullopt : std::optional<AlignmentData>(std::move(data));
}

std::optional<AnalysisResult> AnalysisTask::run(ParallelFor parallelFor)
{
    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    notifyStarted();
    std::vector<double> mono;
    if (! readMono(mono))
        return std::nullopt;

    FeatureOptions options;
    options.parallelFor = std::move(parallelFor);
    options.shouldCancel = [this] { return isCancelled(); };
    options.onProgress = [this](float p) { notifyProgress(readProgressShare + (1.0f - readProgressShare) * p); };

    auto features = extractVocalFeatures(mono, sampleRate, options);
    notifyCompleted();
    if (! features || isCancelled())
        return std::nullopt;

    auto blocks = parseBlocks(*features);
    return AnalysisResult { std::move(*features),
                            std::move(blocks),
                            (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0,
                            static_cast<double>(numSamples) / sampleRate };
}

bool AnalysisTask::readMono(std::vector<double>& mono)
{
    mono.assign(static_cast<size_t>(numSamples), 0.0);
    juce::AudioBuffer<float> chunk(juce::jmax(1, source.getChannelCount()), chunkSize);

    for (juce::int64 start = 0; start < numSamples; start += chunkSize)
    {
        const auto count = static_cast<int>(juce::jmin<juce::int64>(chunkSize, numSamples - start));
        if (! readChunk(chunk, start, count))
            return false;

        const auto gain = 1.0 / chunk.getNumChannels();
        for (int ch = 0; ch < chunk.getNumChannels(); ++ch)
            for (int i = 0; i < count; ++i)
                mono[static_cast<size_t>(start + i)] += gain * chunk.getSample(ch, i);

        notifyProgress(readProgressShare * static_cast<float>(start + count) / static_cast<float>(numSamples));
    }
    return true;
}

bool AnalysisTask::readChunk(juce::AudioBuffer<float>& chunk, juce::int64 start, int count)
{
    const std::lock_guard<std::mutex> lock(sourceMutex);
    if (isCancelled() || reader == nullptr)
        return false;
    return reader->read(chunk.getArrayOfWritePointers(), chunk.getNumChannels(), start, count);
}

void AnalysisTask::cancel()
{
    cancelled.store(true);
    const std::lock_guard<std::mutex> lock(sourceMutex);
    reader.reset();   // ARAAudioSourceReader는 소스 리스너 등록/해제를 하므로 메시지 스레드에서 파괴
    if (started && ! completed)
        source.notifyAnalysisProgressCompleted();   // 호스트의 시작/완료 짝 맞추기
    completed = true;
}

void AnalysisTask::notifyStarted()
{
    const std::lock_guard<std::mutex> lock(sourceMutex);
    if (isCancelled() || ! reportsToHost)
        return;
    source.notifyAnalysisProgressStarted();
    started = true;
}

void AnalysisTask::notifyProgress(float progress)
{
    const std::lock_guard<std::mutex> lock(sourceMutex);
    if (! isCancelled() && reportsToHost)
        source.notifyAnalysisProgressUpdated(progress);
}

void AnalysisTask::notifyCompleted()
{
    const std::lock_guard<std::mutex> lock(sourceMutex);
    if (isCancelled() || completed || ! reportsToHost)
        return;
    source.notifyAnalysisProgressCompleted();
    completed = true;
}
