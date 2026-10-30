#include "../Source/Render/SpectralDiff.h"
#include <cmath>
#include <cstdio>
#include <random>

namespace
{
int failures = 0;
constexpr int sr = 44100;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

// 2초 모음 (220 Hz, 배음 12개, 대부분 4 kHz 미만)
std::vector<float> makeVowel()
{
    std::vector<float> x(static_cast<size_t>(2.0 * sr));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto t = static_cast<double>(i) / sr;
        double v = 0.0;
        for (int h = 1; h <= 12; ++h)
            v += std::sin(2.0 * 3.14159265358979 * 220.0 * h * t) / h;
        x[i] = static_cast<float>(0.2 * v);
    }
    return x;
}

// [a, b) 구간에 고역 잡음 추가 (치찰음 또는 렌더 아티팩트 흉내)
void addHiss(std::vector<float>& x, double a, double b, float level)
{
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, level);
    float previous = 0.0f;
    for (auto i = static_cast<size_t>(a * sr); i < static_cast<size_t>(b * sr) && i < x.size(); ++i)
    {
        const auto n = noise(rng);
        x[i] += n - previous;   // 1차 차분 = 고역 강조
        previous = n;
    }
}

void testIdentical()
{
    const auto x = makeVowel();
    const auto frames = spectralDiff(x, x, sr);
    check(! frames.empty(), "프레임 생성");
    check(flaggedRegions(frames, 2048.0 / sr).empty(), "같은 신호 -> 표시 없음");
}

void testArtifactFound()
{
    const auto x = makeVowel();
    auto y = x;
    addHiss(y, 1.0, 1.25, 0.05f);
    const auto regions = flaggedRegions(spectralDiff(x, y, sr), 2048.0 / sr);
    if (! regions.empty())
        std::printf("  region %.3f-%.3f peak %.1f dB\n", regions[0].startSec, regions[0].endSec, regions[0].peakDb);
    check(regions.size() == 1, "렌더에만 있는 고역 잡음 -> 구간 1개");
    check(! regions.empty() && regions[0].startSec > 0.9 && regions[0].endSec < 1.35, "구간 위치 ~1.0-1.25 s");
}

void testSibilanceIgnored()
{
    auto x = makeVowel();
    addHiss(x, 1.0, 1.25, 0.05f);    // 원본에 이미 있는 치찰음
    auto y = x;
    for (auto& v : y)
        v *= 0.9f;                   // 렌더가 살짝 작아도 고역 '추가'는 아님
    check(flaggedRegions(spectralDiff(x, y, sr), 2048.0 / sr).empty(), "원본 치찰음은 표시 안 함");
}

void testGapMerge()
{
    std::vector<SpectralDiffFrame> frames(5);
    for (size_t i = 0; i < frames.size(); ++i)
        frames[i].timeSec = 0.1 * static_cast<double>(i);
    frames[0].flagged = frames[1].flagged = frames[4].flagged = true;
    const auto regions = flaggedRegions(frames, 0.1, 0.05);
    check(regions.size() == 2, "끊긴 표시는 따로, 붙은 표시는 하나로");
}
} // namespace

int main()
{
    testIdentical();
    testArtifactFound();
    testSibilanceIgnored();
    testGapMerge();
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
