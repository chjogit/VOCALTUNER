#include "VowelSynth.h"
#include "../Source/Analysis/VocalFeatures.h"
#include "../Source/Render/PhaseVocoder.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// 위상 보코더 엔진: 이동 0은 원본 그대로, 음정은 목표에, 포먼트 양은 PSOLA와 같은 의미로
namespace
{
int failures = 0;
constexpr int sr = 44100;
constexpr double framePeriod = 0.005, f0 = 120.0, ratio7 = 1.4983070768766815;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

double midiOf(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

// 0.2 ~ 1.4초가 /a/ (120 Hz), 앞뒤는 무음. 총 1.6초
struct Fixture
{
    std::vector<float> x;
    std::vector<double> midi, shift;
    explicit Fixture(double st)
    {
        const auto n = static_cast<size_t>(1.6 * sr);
        x = vowelsynth::synth(vowelsynth::vowelA(), f0, n, sr);
        for (size_t i = 0; i < n; ++i)
            if (i < static_cast<size_t>(0.2 * sr) || i >= static_cast<size_t>(1.4 * sr)) x[i] = 0.0f;
        const auto frames = static_cast<size_t>(1.6 / framePeriod) + 1;
        midi.assign(frames, 0.0);
        shift.assign(frames, 0.0);
        for (size_t i = 40; i < 280; ++i) { midi[i] = midiOf(f0); shift[i] = st; }
    }
    PsolaInput in(const double* amount) const
    {
        return { x.data(), x.size(), sr, framePeriod, midi.data(), shift.data(), midi.size(), amount };
    }
};

Formants middle(const std::vector<float>& y) { return vowelsynth::measure(y, static_cast<size_t>(0.7 * sr), 2048, sr); }

// 출력 음정: Harvest로 0.5 ~ 1.1초 유성 프레임의 중앙값 (반음)
double outputMidi(const std::vector<float>& y)
{
    const auto f = *extractVocalFeatures(std::vector<double>(y.begin(), y.end()), sr);
    std::vector<double> v;
    for (size_t i = 0; i < f.size(); ++i)
        if (f.timeSec[i] >= 0.5 && f.timeSec[i] <= 1.1 && f.midiF0[i] > 0.0) v.push_back(f.midiF0[i]);
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

bool near(const Formants& src, const Formants& out, double r, double tol)
{
    return src.count >= 2 && out.count >= 2 && std::abs(out.hz[0] / src.hz[0] / r - 1.0) < tol && std::abs(out.hz[1] / src.hz[1] / r - 1.0) < tol;
}

void report(const char* what, const Formants& src, const Formants& out)
{
    std::printf("  %s: F1 %.0f -> %.0f (x%.3f), F2 %.0f -> %.0f (x%.3f)\n", what, src.hz[0], out.hz[0], out.hz[0] / src.hz[0],
                src.hz[1], out.hz[1], out.hz[1] / src.hz[1]);
}

void testIdentity()
{
    Fixture fx(0.0);
    check(vocoderShift(fx.in(nullptr), 0, fx.x.size()) == fx.x, "vocoder: shift 0 returns identical samples");
    const auto part = vocoderShift(fx.in(nullptr), 10000, 30000);
    check(part == std::vector<float>(fx.x.begin() + 10000, fx.x.begin() + 30000), "vocoder: shift 0 sub-range identical");
    Fixture unvoiced(7.0);
    std::fill(unvoiced.midi.begin(), unvoiced.midi.end(), 0.0);
    check(vocoderShift(unvoiced.in(nullptr), 0, unvoiced.x.size()) == unvoiced.x, "vocoder: unvoiced frames returned unchanged");
    check(vocoderShift(fx.in(nullptr), 5, 5).empty(), "vocoder: empty range");
}

void testShift()
{
    Fixture fx(7.0);
    const auto y = vocoderShift(fx.in(nullptr), 0, fx.x.size());
    check(y.size() == fx.x.size(), "vocoder: output length matches range");
    const auto cents = (outputMidi(y) - (midiOf(f0) + 7.0)) * 100.0;
    std::printf("  +7 st output F0 error %.1f cents\n", cents);
    check(std::abs(cents) < 20.0, "vocoder: +7 st lands within 20 cents of target");
    check(std::abs(y[static_cast<size_t>(0.1 * sr)]) == 0.0f && std::abs(y[static_cast<size_t>(1.5 * sr)]) == 0.0f, "vocoder: silence stays silent");
}

// 이동이 0에서 +7까지 서서히 변하는 구간: 출력 음정이 프레임마다 목표(원본 + 이동)를 따라간다
void testRamp()
{
    Fixture fx(0.0);
    for (size_t i = 40; i < 280; ++i) fx.shift[i] = 7.0 * static_cast<double>(i - 40) / 239.0;
    const auto y = vocoderShift(fx.in(nullptr), 0, fx.x.size());
    const auto f = *extractVocalFeatures(std::vector<double>(y.begin(), y.end()), sr);
    std::vector<double> err;
    for (size_t i = 0; i < f.size() && i < fx.midi.size(); ++i)
        if (f.timeSec[i] >= 0.5 && f.timeSec[i] <= 1.1 && f.midiF0[i] > 0.0)
            err.push_back(std::abs(f.midiF0[i] - (fx.midi[i] + fx.shift[i])) * 100.0);
    std::nth_element(err.begin(), err.begin() + static_cast<long>(err.size() / 2), err.end());
    std::printf("  ramp: %zu voiced frames, median error %.1f cents\n", err.size(), err[err.size() / 2]);
    check(err.size() > 100 && err[err.size() / 2] < 30.0, "vocoder: ramping shift follows the target within 30 cents (median)");
}

void testFormants()
{
    Fixture fx(7.0);
    const auto src = middle(fx.x);
    const std::vector<double> one(fx.midi.size(), 1.0), zero(fx.midi.size(), 0.0);
    const auto keep = middle(vocoderShift(fx.in(one.data()), 0, fx.x.size()));
    const auto move = middle(vocoderShift(fx.in(zero.data()), 0, fx.x.size()));
    report("vocoder amount=1 +7st", src, keep);
    report("vocoder amount=0 +7st", src, move);
    check(near(src, keep, 1.0, 0.10), "vocoder: amount 1 keeps F1/F2 within 10%");
    check(near(src, move, ratio7, 0.10), "vocoder: amount 0 moves F1/F2 by 2^(7/12) within 10%");
}
} // namespace

int main()
{
    testIdentity();
    testShift();
    testRamp();
    testFormants();
    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
