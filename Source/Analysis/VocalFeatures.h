#pragma once
#include <functional>
#include <optional>
#include <vector>

// Python 프로토타입 extract_vocal_features()의 C++ 포팅. JUCE 비의존(단위 테스트용).

// count개의 작업을 body(0..count-1)로 실행하고 모두 끝나면 반환. 미지정이면 순차 실행.
using ParallelFor = std::function<void(int count, const std::function<void(int)>& body)>;

struct FeatureOptions
{
    double framePeriodMs = 5.0;
    double f0FloorHz = 71.0;
    double f0CeilHz = 800.0;
    int rmsFrameLength = 2048;

    // Harvest는 느리므로 정수 초 단위 구간으로 나눠 병렬 처리. 경계마다 overlap 만큼 더 분석해 버림.
    int chunkSeconds = 10;
    int overlapSeconds = 1;
    ParallelFor parallelFor;

    std::function<bool()> shouldCancel;        // 어느 스레드에서든 호출됨
    std::function<void(float)> onProgress;     // 0..1, 호출 간 단조 증가 보장, 어느 스레드에서든 호출됨
};

struct VocalFeatures
{
    double framePeriodMs = 5.0;
    std::vector<double> timeSec;  // 프레임 시각
    std::vector<double> midiF0;   // 0 = 무성(unvoiced)
    std::vector<double> rms;      // 0..1 정규화

    size_t size() const noexcept { return timeSec.size(); }
};

double hzToMidi(double hz) noexcept;

// 취소되면 nullopt
std::optional<VocalFeatures> extractVocalFeatures(const std::vector<double>& mono, int sampleRate,
                                                  const FeatureOptions& options = {});
