#include "../Source/Analysis/VocalFeatures.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <thread>
#include <vector>

namespace
{
constexpr int sampleRate = 44100;
constexpr double pi = 3.14159265358979323846;

int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

// Harvest는 배음 없는 순수 사인파를 무성으로 판정하므로 배음 5개짜리 톤 사용
std::vector<double> makeHarmonicTone(double hz, double seconds)
{
    std::vector<double> samples(static_cast<size_t>(seconds * sampleRate), 0.0);
    for (size_t i = 0; i < samples.size(); ++i)
        for (int k = 1; k <= 5; ++k)
            samples[i] += 0.3 / k * std::sin(2.0 * pi * hz * k * static_cast<double>(i) / sampleRate);
    return samples;
}

// 음정이 바뀌고(220/262/330 Hz), 비브라토가 있고, 무음 구간이 있는 보컬 비슷한 신호
std::vector<double> makeMelody(double seconds)
{
    std::vector<double> samples(static_cast<size_t>(seconds * sampleRate), 0.0);
    double phase = 0.0;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const auto t = static_cast<double>(i) / sampleRate;
        const auto base = t < seconds / 3 ? 220.0 : (t < 2 * seconds / 3 ? 261.63 : 329.63);
        phase += 2.0 * pi * base * std::pow(2.0, 0.3 * std::sin(2.0 * pi * 5.5 * t) / 12.0) / sampleRate;
        const auto silent = std::fmod(t, 4.0) > 3.4;
        for (int k = 1; k <= 6; ++k)
            samples[i] += silent ? 0.0 : 0.3 / k * std::sin(k * phase);
    }
    return samples;
}

double medianVoicedMidi(const VocalFeatures& features)
{
    std::vector<double> voiced;
    std::copy_if(features.midiF0.begin(), features.midiF0.end(), std::back_inserter(voiced),
                 [](double m) { return m > 0.0; });
    if (voiced.empty())
        return 0.0;
    std::nth_element(voiced.begin(), voiced.begin() + static_cast<long>(voiced.size() / 2), voiced.end());
    return voiced[voiced.size() / 2];
}

ParallelFor threadedFor(int numThreads)
{
    return [numThreads](int count, const std::function<void(int)>& body)
    {
        std::atomic<int> next { 0 };
        std::vector<std::thread> threads;
        for (int t = 0; t < numThreads; ++t)
            threads.emplace_back([&] { for (int i; (i = next++) < count;) body(i); });
        for (auto& thread : threads)
            thread.join();
    };
}

void testDetectsA4()
{
    const auto features = *extractVocalFeatures(makeHarmonicTone(440.0, 1.0), sampleRate);
    check(std::abs(medianVoicedMidi(features) - 69.0) < 0.1, "440 Hz tone -> MIDI 69");
    check(features.size() == features.midiF0.size() && features.size() == features.rms.size(),
          "time/f0/rms lengths match");
}

void testDetectsA3()
{
    const auto features = *extractVocalFeatures(makeHarmonicTone(220.0, 1.0), sampleRate);
    check(std::abs(medianVoicedMidi(features) - 57.0) < 0.1, "220 Hz tone -> MIDI 57");
    check(std::abs(features.timeSec[1] - features.timeSec[0] - 0.005) < 1e-9, "5 ms frame period");
}

void testSilenceIsUnvoiced()
{
    const auto features = *extractVocalFeatures(std::vector<double>(sampleRate / 2, 0.0), sampleRate);
    const auto allUnvoiced = std::all_of(features.midiF0.begin(), features.midiF0.end(),
                                         [](double m) { return m == 0.0; });
    check(features.size() > 0 && allUnvoiced, "silence -> all frames unvoiced");
    check(extractVocalFeatures({}, sampleRate)->size() == 0, "empty input -> empty result");
}

// 유성/무성 전환점으로부터 maxDistance 프레임 이내인지
bool nearVoicingChange(const VocalFeatures& reference, size_t index, size_t maxDistance)
{
    const auto lo = index > maxDistance ? index - maxDistance : 0;
    const auto hi = std::min(reference.size() - 1, index + maxDistance);
    for (auto i = lo + 1; i <= hi; ++i)
        if ((reference.midiF0[i] > 0.0) != (reference.midiF0[i - 1] > 0.0))
            return true;
    return false;
}

void testChunkedMatchesWhole()
{
    // 30 s: 음정이 바뀌는 순간(10 s, 20 s)이 구간 경계와 정확히 일치하는 최악의 경우
    const auto signal = makeMelody(30.0);

    FeatureOptions whole;
    whole.chunkSeconds = 1000;
    const auto reference = *extractVocalFeatures(signal, sampleRate, whole);

    FeatureOptions chunked;
    chunked.parallelFor = threadedFor(3);
    const auto result = *extractVocalFeatures(signal, sampleRate, chunked);

    check(result.size() == reference.size(), "chunked: same frame count as whole");

    size_t atTransitions = 0, elsewhere = 0;
    for (size_t i = 0; i < std::min(result.size(), reference.size()); ++i)
    {
        const auto voicedDiffers = (result.midiF0[i] > 0.0) != (reference.midiF0[i] > 0.0);
        const auto pitchDiffers = std::abs(result.midiF0[i] - reference.midiF0[i]) > 0.05;
        if (! (voicedDiffers || pitchDiffers))
            continue;
        (nearVoicingChange(reference, i, 8) ? atTransitions : elsewhere)++;   // 8 프레임 = 40 ms
    }
    std::printf("       differing frames: %zu near voicing changes, %zu elsewhere (of %zu)\n",
                atTransitions, elsewhere, reference.size());
    check(elsewhere == 0, "chunked: identical to whole except within 40 ms of voicing changes");
}

void testProgressIsMonotonicAndComplete()
{
    std::vector<float> reported;
    FeatureOptions options;
    options.chunkSeconds = 2;
    options.parallelFor = threadedFor(2);
    options.onProgress = [&](float p) { reported.push_back(p); };   // 호출은 직렬화됨
    extractVocalFeatures(makeHarmonicTone(220.0, 7.0), sampleRate, options);

    check(reported.size() == 4, "progress: one report per chunk");
    check(std::is_sorted(reported.begin(), reported.end()) && std::abs(reported.back() - 1.0f) < 1e-6f,
          "progress: monotonic and ends at 1");
}

void testCancellation()
{
    std::atomic<int> started { 0 };
    FeatureOptions options;
    options.chunkSeconds = 1;
    options.shouldCancel = [&] { return started++ >= 2; };
    const auto result = extractVocalFeatures(makeHarmonicTone(220.0, 8.0), sampleRate, options);
    check(! result.has_value(), "cancel: returns nullopt");
    check(started.load() < 8, "cancel: stops before processing all chunks");
}
} // namespace

int main()
{
    testDetectsA4();
    testDetectsA3();
    testSilenceIsUnvoiced();
    testChunkedMatchesWhole();
    testProgressIsMonotonicAndComplete();
    testCancellation();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
