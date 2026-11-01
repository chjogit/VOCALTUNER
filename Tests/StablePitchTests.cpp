#include "StablePitch.h"
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
void check(bool ok, const char* what) { if (! ok) { std::printf("FAIL: %s\n", what); ++failures; } }

VocalFeatures featuresOf(const std::vector<double>& midi)
{
    VocalFeatures f;
    f.midiF0 = midi;
    f.rms.assign(midi.size(), 0.5);
    for (size_t i = 0; i < midi.size(); ++i) f.timeSec.push_back(0.005 * static_cast<double>(i));
    return f;
}

void testGlitchesAreDropped()
{
    // 안정한 60.0 바탕에 한쪽으로 치우친 튀는 프레임(옥타브 위, 반음 위)이 여러 개
    std::vector<double> m(40, 60.0);
    for (size_t i : {5u, 9u, 21u, 22u, 30u}) m[i] = 72.0;
    for (size_t i : {13u, 14u, 15u}) m[i] = 61.2;
    const auto frames = centreMidiFrames(featuresOf(m), 0, m.size());
    check(! frames.empty(), "has frames");
    for (auto v : frames) check(std::abs(v - 60.0) < 1e-9, "only stable frames remain");
}

void testSlidingNoteKeepsAll()
{
    // 계속 미끄러지는 음: 이웃과 한 프레임 차이가 작아 대부분 안정 -> 중심이 거의 그대로
    std::vector<double> m;
    for (int i = 0; i < 40; ++i) m.push_back(60.0 + 0.05 * i);
    const auto frames = centreMidiFrames(featuresOf(m), 0, m.size());
    check(frames.size() >= 30, "sliding note mostly kept");
}

void testFallbackWhenAllUnstable()
{
    std::vector<double> m { 60.0, 64.0, 58.0, 66.0, 61.0, 70.0 };
    const auto frames = centreMidiFrames(featuresOf(m), 0, m.size());
    check(frames.size() == m.size(), "falls back to all voiced frames");
}

void testUnvoicedSkipped()
{
    std::vector<double> m { 0.0, 60.0, 60.0, 60.0, 60.0, 0.0 };
    const auto frames = centreMidiFrames(featuresOf(m), 0, m.size());
    check(frames.size() == 4, "unvoiced frames are not used");
}

void testMedian()
{
    check(medianOf({}) == 0.0, "empty median");
    check(std::abs(medianOf({ 3.0, 1.0, 2.0 }) - 2.0) < 1e-12, "odd median");
    check(std::abs(medianOf({ 4.0, 1.0, 2.0, 3.0 }) - 2.5) < 1e-12, "even median");
}
} // namespace

int main()
{
    testGlitchesAreDropped();
    testSlidingNoteKeepsAll();
    testFallbackWhenAllUnstable();
    testUnvoicedSkipped();
    testMedian();
    std::printf(failures == 0 ? "StablePitchTests passed\n" : "StablePitchTests FAILED\n");
    return failures == 0 ? 0 : 1;
}
