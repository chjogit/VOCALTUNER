#include "../Source/Render/EditRenderer.h"
#include "../Source/Render/EditTimeMap.h"
#include "../Source/Analysis/BlockParser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
constexpr int sr = 44100;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

// 모음처럼 들리는 합성음: 배음 24개, 기본음 hz, [0.3, 0.3+seconds) 구간
std::vector<float> makeVoice(double hz, double seconds, double total, double level = 0.2)
{
    std::vector<float> x(static_cast<size_t>(total * sr), 0.0f);
    const auto begin = static_cast<size_t>(0.3 * sr), end = begin + static_cast<size_t>(seconds * sr);
    for (size_t i = begin; i < end && i < x.size(); ++i)
    {
        const auto t = static_cast<double>(i) / sr;
        double v = 0.0;
        for (int h = 1; h <= 24; ++h)
            v += std::sin(2.0 * 3.14159265358979 * hz * h * t) / h * (h * hz < 3000.0 ? 1.0 : 0.2);
        x[i] = static_cast<float>(level * v);
    }
    return x;
}

double rmsBetween(const std::vector<float>& x, double a, double b)
{
    double sum = 0.0;
    const auto i0 = static_cast<size_t>(a * sr), i1 = static_cast<size_t>(b * sr);
    for (auto i = i0; i < i1 && i < x.size(); ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(i1 - i0));
}

float peakOf(const std::vector<float>& x)
{
    float peak = 0.0f;
    for (const auto v : x)
        peak = std::max(peak, std::abs(v));
    return peak;
}

double medianMidi(const std::vector<float>& x, double a, double b)
{
    std::vector<double> mono(x.begin(), x.end());
    const auto f = *extractVocalFeatures(mono, sr);
    std::vector<double> v;
    for (size_t i = 0; i < f.size(); ++i)
        if (f.timeSec[i] > a && f.timeSec[i] < b && f.midiF0[i] > 0.0)
            v.push_back(f.midiF0[i]);
    if (v.empty())
        return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

struct Fixture
{
    AudioPlanes audio;
    VocalFeatures features;
    std::vector<VocalBlock> blocks;
    explicit Fixture(double level = 0.2)
    {
        audio.sampleRate = sr;
        audio.channels = {makeVoice(220.0, 1.5, 2.4, level)};
        features = *extractVocalFeatures(std::vector<double>(audio.channels[0].begin(), audio.channels[0].end()), sr);
        blocks = parseBlocks(features);
    }
    size_t firstVoiced() const
    {
        for (size_t i = 0; i < blocks.size(); ++i)
            if (! blocks[i].isConsonant)
                return i;
        return 0;
    }
};

void testIdentity()
{
    Fixture fx;
    check(! fx.blocks.empty(), "블록이 파싱됨");
    for (auto& b : fx.blocks)
        b.tidyAmount = 0.0;   // 기본 자동 보정(정돈)도 편집이므로 끈다
    check(! changesAudio(fx.blocks, fx.features), "편집 없음 = 소리 변화 없음");
    const auto out = renderEdited(fx.audio, fx.features, fx.blocks);
    check(out && out->channels[0] == fx.audio.channels[0], "편집 없음 -> 원본 그대로");
}

void testPitchShift()
{
    Fixture fx;
    const auto before = medianMidi(fx.audio.channels[0], 0.6, 1.6);
    for (auto& b : fx.blocks)
        b.pitchShift = 2.0;
    check(changesAudio(fx.blocks, fx.features), "음정 이동 = 소리 변화");
    const auto out = renderEdited(fx.audio, fx.features, fx.blocks);
    check(out.has_value() && out->length() == fx.audio.length(), "길이 유지");
    const auto after = medianMidi(out->channels[0], 0.6, 1.6);
    std::printf("  pitch %.3f -> %.3f\n", before, after);
    check(std::abs(after - before - 2.0) < 0.15, "+2 반음 렌더");
}

void testGain()
{
    Fixture fx;
    for (auto& b : fx.blocks)
        b.gainDb = -6.0;
    const auto out = renderEdited(fx.audio, fx.features, fx.blocks);
    const auto ratio = rmsBetween(out->channels[0], 0.8, 1.4) / rmsBetween(fx.audio.channels[0], 0.8, 1.4);
    std::printf("  gain ratio %.3f\n", ratio);
    check(std::abs(ratio - 0.501) < 0.06, "-6 dB 볼륨");
}

void testTimeShift()
{
    Fixture fx;
    const auto i = fx.firstVoiced();
    for (auto& b : fx.blocks)
    {
        b.startOffsetSec = 0.2;
        b.endOffsetSec = 0.2;
    }
    const auto out = renderEdited(fx.audio, fx.features, fx.blocks);
    const auto silentBefore = rmsBetween(out->channels[0], 0.32, 0.45);
    const auto loudAfter = rmsBetween(out->channels[0], 0.7, 1.2);
    std::printf("  rms %.4f / %.4f (first voiced %zu)\n", silentBefore, loudAfter, i);
    check(silentBefore < 0.2 * loudAfter, "0.2초 늦춤: 원래 시작 지점은 조용");
}

// 음량 회귀(#2 큰 음량 깨짐): 음정만 바꾸면 음량은 그대로, 피크는 1을 넘지 않는다
void checkLoudnessAfterShift(double level, const char* rmsMessage, const char* peakMessage)
{
    Fixture fx(level);
    for (auto& b : fx.blocks)
        b.pitchShift = 2.0;
    const auto out = renderEdited(fx.audio, fx.features, fx.blocks);
    const auto ratio = rmsBetween(out->channels[0], 0.6, 1.6) / rmsBetween(fx.audio.channels[0], 0.6, 1.6);
    const auto peak = peakOf(out->channels[0]);
    std::printf("  level %.2f: rms ratio %.3f, peak %.3f (src %.3f)\n", level, ratio, peak, peakOf(fx.audio.channels[0]));
    check(std::abs(ratio - 1.0) < 0.15, rmsMessage);
    check(peak <= 1.0f, peakMessage);
}

void testLoudness()
{
    checkLoudnessAfterShift(0.2, "+2 반음: RMS 비 ~1", "+2 반음: 피크 <= 1");
    checkLoudnessAfterShift(0.55, "큰 음량 +2 반음: RMS 비 ~1", "큰 음량 +2 반음: 피크 <= 1");
}

void testCancelAndStereo()
{
    Fixture fx;
    fx.audio.channels.push_back(fx.audio.channels[0]);
    for (auto& b : fx.blocks)
        b.pitchShift = 1.0;
    RenderOptions cancel;
    cancel.shouldCancel = [] { return true; };
    check(! renderEdited(fx.audio, fx.features, fx.blocks, cancel).has_value(), "취소되면 nullopt");
    const auto out = renderEdited(fx.audio, fx.features, fx.blocks);
    check(out && out->channels.size() == 2, "스테레오 채널 수 유지");
}

void testRange()
{
    Fixture fx;
    for (auto& b : fx.blocks)
        b.pitchShift = 2.0;
    const auto begin = static_cast<size_t>(0.6 * sr), end = static_cast<size_t>(1.4 * sr);
    const auto seg = renderEditedRange(fx.audio, fx.features, fx.blocks, begin, end);
    check(seg.has_value() && seg->startSample <= begin && seg->endSample() >= end, "구간 렌더: 요청 구간을 덮음");
    check(seg->endSample() <= fx.audio.length(), "구간 렌더: 원본 길이 안");
    std::vector<float> placed(fx.audio.length(), 0.0f);
    std::copy(seg->audio.channels[0].begin(), seg->audio.channels[0].end(), placed.begin() + static_cast<long>(seg->startSample));
    const auto after = medianMidi(placed, 0.7, 1.3), before = medianMidi(fx.audio.channels[0], 0.7, 1.3);
    std::printf("  range pitch %.3f -> %.3f\n", before, after);
    check(std::abs(after - before - 2.0) < 0.2, "구간 렌더: +2 반음");
    RenderOptions cancel;
    cancel.shouldCancel = [] { return true; };
    check(! renderEditedRange(fx.audio, fx.features, fx.blocks, begin, end, cancel).has_value(), "구간 렌더: 취소");
}

void testTimeMap()
{
    Fixture fx;
    fx.blocks[fx.firstVoiced()].startOffsetSec = 0.1;
    fx.blocks[fx.firstVoiced()].endOffsetSec = 0.1;
    const EditTimeMap map(fx.blocks);
    const auto& b = fx.blocks[fx.firstVoiced()];
    check(std::abs(map.toSource(b.displayStartSec()) - b.startSec) < 1e-6, "시간 대응: 시작");
    check(std::abs(map.toDisplay(b.endSec) - b.displayEndSec()) < 1e-6, "시간 대응: 끝");
}
} // namespace

int main()
{
    testTimeMap();
    testIdentity();
    testPitchShift();
    testGain();
    testTimeShift();
    testLoudness();
    testCancelAndStereo();
    testRange();
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
