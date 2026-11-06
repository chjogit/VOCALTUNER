#include "VowelTestScene.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

using namespace vwtest;

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

void testVowel()
{
    Scene s;
    s.append(vowel(1.5));
    const auto w = s.weights();
    std::printf("  sustained vowel: min w in middle %.3f\n", lowestIn(w, 0.3, 1.2));
    check(lowestIn(w, 0.3, 1.2) == 1.0, "sustained vowel: w == 1 exactly in the middle");
}

void testNoise()
{
    Scene white, pink;
    white.append(whiteNoise(1.5, 0.1));
    pink.append(pinkNoise(1.5, 0.1));
    std::printf("  white noise max w %.3f, pink noise max w %.3f\n", highestIn(white.weights(), 0.3, 1.2), highestIn(pink.weights(), 0.3, 1.2));
    check(highestIn(white.weights(), 0.3, 1.2) <= 0.1, "white-noise breath labelled voiced: w <= 0.1");
    check(highestIn(pink.weights(), 0.3, 1.2) <= 0.1, "pink-noise breath labelled voiced: w <= 0.1");
}

void testFricative()
{
    Scene s;
    s.append(hiss(1.5, 0.2));
    check(highestIn(s.weights(), 0.3, 1.2) == 0.0, "fricative-like HF noise labelled voiced: w == 0");
}

void testNasal()
{
    Scene s;
    s.append(vowel(0.6));
    s.append(nasal(0.25));
    s.append(vowel(0.6));
    const auto w = s.weights();
    std::printf("  nasal-like: max w in middle %.3f, vowel min %.3f\n", highestIn(w, 0.67, 0.78), lowestIn(w, 0.2, 0.45));
    check(highestIn(w, 0.67, 0.78) <= 0.3, "quiet low nasal-like segment (-25 dB): w <= 0.3");
    check(lowestIn(w, 0.2, 0.45) >= 0.95 && lowestIn(w, 1.0, 1.3) >= 0.95, "vowels around the nasal keep w >= 0.95");
}

void testSequence()
{
    Scene s;
    s.append(vowel(0.6));
    s.append(whiteNoise(0.4, 0.1));
    s.append(vowel(0.6));
    const auto w = s.weights();
    double step = 0.0;
    for (size_t i = 1; i < w.size(); ++i) step = std::max(step, std::abs(w[i] - w[i - 1]));
    std::printf("  vowel-noise-vowel: max step %.3f, noise middle max %.3f\n", step, highestIn(w, 0.75, 0.95));
    check(step <= 0.15, "vowel -> noise -> vowel: no step above 0.15 per frame");
    check(highestIn(w, 0.75, 0.95) <= 0.1 && lowestIn(w, 0.2, 0.45) >= 0.95, "noise low, vowels high in the sequence");
}

// 무성(sourceMidi == 0) 구간이 낀 열: 유성 구간 안에서는 프레임당 변화 <= 0.15, 무성은 w = 0
void testUnvoicedGap()
{
    Scene s;
    s.append(vowel(0.5));
    s.append(whiteNoise(0.2, 0.1), false);
    s.append(vowel(0.5));
    const auto w = s.weights();
    double step = 0.0;
    bool silent = true;
    for (size_t i = 1; i < w.size(); ++i)
    {
        silent = silent && (s.midi[i] > 0.0 || w[i] == 0.0);
        if (s.midi[i] > 0.0 && s.midi[i - 1] > 0.0) step = std::max(step, std::abs(w[i] - w[i - 1]));
    }
    std::printf("  with unvoiced gap: max step inside voiced runs %.3f\n", step);
    check(step <= 0.15 && silent, "voiced runs around an unvoiced gap: no step above 0.15 per frame, unvoiced frames are 0");
    check(lowestIn(w, 0.15, 0.4) == 1.0 && lowestIn(w, 0.95, 1.2) == 1.0, "vowels next to the unvoiced gap keep w == 1 in their cores");
}

// NaN 표본, null 입력: w는 항상 유한한 0..1, null이면 0
void testNonFinite()
{
    Scene s;
    s.append(vowel(1.0));
    s.x[samplesOf(0.5)] = std::nanf("");
    const auto w = s.weights();
    bool ok = true;
    for (const auto v : w) ok = ok && std::isfinite(v) && v >= 0.0 && v <= 1.0;
    check(ok, "a NaN sample: every w stays finite within 0..1");
    const auto none = vowelWeights(nullptr, 0, sr, period, nullptr, s.midi.size(), 0, 50);
    check(none.size() == 50 && *std::max_element(none.begin(), none.end()) == 0.0, "null input: w == 0");
    std::vector<double> shift(s.midi.size(), 2.0);
    applyVowelFocus(shift, s.x.data(), s.x.size(), sr, period, nullptr, s.midi.size(), 0, 50);
    check(shift[10] == 0.0 && shift[60] == 2.0, "applyVowelFocus with null midi zeroes the range only");
}

void testRange()
{
    Scene s;
    s.append(vowel(0.6));
    s.append(whiteNoise(0.3, 0.1));
    s.append(vowel(0.4));
    s.append(hiss(0.3, 0.2));
    s.append(vowel(0.5));
    const auto whole = s.weights();
    double worst = 0.0;
    const size_t n = whole.size();
    for (const auto& [a, b] : { std::pair<size_t, size_t>{ 0, n }, { 100, 160 }, { 117, 133 }, { 0, 30 }, { n - 40, n }, { 130, 131 }, { 150, n + 50 } })
    {
        const auto part = s.weights(a, b);
        for (size_t i = 0; i < part.size(); ++i) worst = std::max(worst, std::abs(part[i] - whole[a + i]));
    }
    std::printf("  range vs whole: worst difference %.3g\n", worst);
    check(worst < 1e-9, "weights for a frame range equal those from the whole-file call");
}

void testApply()
{
    Scene s;
    s.append(vowel(0.6));
    s.append(whiteNoise(0.4, 0.1));
    std::vector<double> shift(s.midi.size(), 2.0);
    applyVowelFocus(shift, s.x.data(), s.x.size(), sr, period, s.midi.data(), s.midi.size(), 20, 190);
    check(shift[10] == 2.0 && shift[195] == 2.0, "applyVowelFocus leaves frames outside the range untouched");
    check(shift[60] > 1.9 && shift[170] < 0.2, "applyVowelFocus scales vowel ~1 and noise ~0");
}

} // namespace

int main()
{
    testVowel();
    testNoise();
    testFricative();
    testNasal();
    testSequence();
    testUnvoicedGap();
    testNonFinite();
    testRange();
    testApply();
    std::printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
