#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include "BlockParser.h"
#include "VocalFeatures.h"
#include "AlignmentData.h"

struct AnalysisResult
{
    VocalFeatures features;
    std::vector<VocalBlock> blocks;
    double elapsedSeconds = 0.0;
    double audioSeconds = 0.0;
};

// 한 오디오 소스의 분석 1회. 메시지 스레드에서 생성/취소, 워커 스레드에서 run().
// sourceMutex: 취소 후에는 워커가 소스/리더에 접근하지 않도록 보장
class AnalysisTask
{
public:
    // hostProgress=false: 호스트 분석 진행 알림 없이 조용히 실행 (정렬용 보조 분석)
    explicit AnalysisTask(juce::ARAAudioSource&, bool hostProgress = true);

    // 코디네이터 스레드. 구간 병렬 처리는 parallelFor로 위임. 취소되면 nullopt
    std::optional<AnalysisResult> run(ParallelFor parallelFor);

    // 정렬용 스펙트럼 분석: 오디오를 다시 읽어 MFCC와 구간 분류를 만든다. 취소되면 nullopt
    std::optional<AlignmentData> runAlignment(const VocalFeatures&, ParallelFor);

    // 메시지 스레드. 읽기 중이면 현재 청크가 끝날 때까지 대기
    void cancel();
    bool isCancelled() const noexcept { return cancelled.load(); }

private:
    bool readMono(std::vector<double>& mono);
    bool readChunk(juce::AudioBuffer<float>&, juce::int64 start, int numSamples);
    void notifyProgress(float progress);
    void notifyStarted();
    void notifyCompleted();

    juce::ARAAudioSource& source;
    std::unique_ptr<juce::ARAAudioSourceReader> reader;
    const juce::int64 numSamples;
    const int sampleRate;
    std::mutex sourceMutex;
    const bool reportsToHost;
    std::atomic<bool> cancelled { false };
    bool started = false;
    bool completed = false;
};
