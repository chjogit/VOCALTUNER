#include "VowelSynth.h"
#include "../Source/Analysis/VocalFeatures.h"
#include "../Source/Render/Psola.h"
#include "../Source/Render/PsolaLevel.h"
#include "../Source/Render/PsolaWarp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// PSOLA 음량 보정(levelMatch): 켜면 합성 구간의 RMS가 원본을 따라가고, 음정/포먼트/이동 0 구간은 그대로. 꺼진 기본값은 golden 시험이 지킨다
namespace
{
int failures = 0;
constexpr int sr = 44100;
constexpr double framePeriod = 0.005, f0 = 120.0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

double midiOf(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

// 0.2 ~ 1.4초 /a/ (120 Hz) + 숨결 같은 잡음(약 -25 dB), 앞뒤는 무음. 이동은 프레임 100..240 (0.5 ~ 1.2초)에만
struct Fixture
{
    std::vector<float> x;
    std::vector<double> midi, shift;
    explicit Fixture(double st)
    {
        const auto n = static_cast<size_t>(1.6 * sr);
        x = vowelsynth::synth(vowelsynth::vowelA(), f0, n, sr);
        unsigned seed = 12345;
        for (size_t i = 0; i < n; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const auto noise = (static_cast<double>(seed >> 8) / 8388608.0 - 1.0) * 0.012;
            const bool voiced = i >= static_cast<size_t>(0.2 * sr) && i < static_cast<size_t>(1.4 * sr);
            x[i] = voiced ? static_cast<float>(x[i] + noise) : 0.0f;
        }
        const auto frames = static_cast<size_t>(1.6 / framePeriod) + 1;
        midi.assign(frames, 0.0);
        shift.assign(frames, 0.0);
        for (size_t i = 40; i < 280; ++i) { midi[i] = midiOf(f0); shift[i] = i >= 100 && i < 240 ? st : 0.0; }
    }
    PsolaInput in(bool level) const { return { x.data(), x.size(), sr, framePeriod, midi.data(), shift.data(), midi.size(), nullptr, level }; }
};

double rmsDb(const std::vector<float>& y, const std::vector<float>& x, double from, double to)
{
    double ey = 0.0, ex = 0.0;
    for (auto i = static_cast<size_t>(from * sr); i < static_cast<size_t>(to * sr); ++i) { ey += y[i] * static_cast<double>(y[i]); ex += x[i] * static_cast<double>(x[i]); }
    return 10.0 * std::log10(ey / ex);
}

double outputMidi(const std::vector<float>& y)
{
    const auto f = *extractVocalFeatures(std::vector<double>(y.begin(), y.end()), sr);
    std::vector<double> v;
    for (size_t i = 0; i < f.size(); ++i)
        if (f.timeSec[i] >= 0.65 && f.timeSec[i] <= 1.05 && f.midiF0[i] > 0.0) v.push_back(f.midiF0[i]);
    std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

void testLevel()
{
    double worstOn = 0.0, worstOff = 0.0;
    for (const auto st : { -2.0, 2.0, 7.0, 12.0 })
    {
        Fixture fx(st);
        const auto off = psolaShift(fx.in(false), 0, fx.x.size()), on = psolaShift(fx.in(true), 0, fx.x.size());
        const auto dOff = rmsDb(off, fx.x, 0.6, 1.1), dOn = rmsDb(on, fx.x, 0.6, 1.1);
        std::printf("  %+.0f st: RMS change off %+.2f dB, on %+.2f dB\n", st, dOff, dOn);
        worstOn = std::max(worstOn, std::abs(dOn));
        worstOff = std::max(worstOff, std::abs(dOff));
    }
    check(worstOff > 0.7, "the fixture shows the loss without level match (so the test can fail)");
    check(worstOn <= 0.5, "level match: voiced RMS within 0.5 dB at -2, +2, +7, +12 st");
}

void testUnchanged()
{
    Fixture fx(7.0);
    const auto off = psolaShift(fx.in(false), 0, fx.x.size()), on = psolaShift(fx.in(true), 0, fx.x.size());
    check(std::equal(on.begin() + static_cast<long>(0.25 * sr), on.begin() + static_cast<long>(0.4 * sr), fx.x.begin() + static_cast<long>(0.25 * sr)),
          "level match: voiced shift-0 region is bit-identical to the source");
    check(std::equal(on.begin() + static_cast<long>(1.3 * sr), on.end(), fx.x.begin() + static_cast<long>(1.3 * sr)), "level match: samples after the shifted run are bit-identical");
    check(std::equal(on.begin(), on.begin() + static_cast<long>(0.4 * sr), off.begin()), "level match leaves everything before the run exactly as without it");
    check(std::abs(outputMidi(on) - outputMidi(off)) < 0.1, "level match: F0 unchanged (within 10 cents)");
    const auto fo = vowelsynth::measure(off, static_cast<size_t>(0.8 * sr), 2048, sr), fn = vowelsynth::measure(on, static_cast<size_t>(0.8 * sr), 2048, sr);
    check(fo.count >= 2 && fn.count >= 2 && std::abs(fn.hz[0] / fo.hz[0] - 1.0) < 0.03 && std::abs(fn.hz[1] / fo.hz[1] - 1.0) < 0.03, "level match: F1/F2 unchanged (within 3%)");
}

// 인접 창 gain 차 <= 1 dB, 범위 +-6 dB
void testSmooth()
{
    Fixture fx(7.0);
    const auto off = psolaShift(fx.in(false), 0, fx.x.size());
    std::vector<double> mask(fx.x.size(), 0.0);
    for (auto i = static_cast<size_t>(0.5 * sr); i < static_cast<size_t>(1.2 * sr); ++i) mask[i] = 1.0;
    const auto db = psola::levelGainsDb(off, fx.x, psola::maskWeight(mask, sr), sr);
    double step = 0.0, top = 0.0;
    for (size_t k = 1; k < db.size(); ++k) step = std::max(step, std::abs(db[k] - db[k - 1]));
    for (const auto g : db) top = std::max(top, std::abs(g));
    std::printf("  gain curve: %zu windows, max step %.2f dB, max |gain| %.2f dB\n", db.size(), step, top);
    check(step <= 1.0 && top <= 6.0 + 1e-9 && db.size() > 100, "level match: gain curve has no step above 1 dB and stays within +-6 dB");
}

void testWarp()
{
    Fixture fx(7.0);
    std::vector<double> shift(fx.midi.size(), 0.0);
    for (size_t i = 100; i < 240; ++i) shift[i] = 7.0;
    const auto id = [](double t) { return t; };
    WarpInput in { fx.x.data(), fx.x.size(), sr, framePeriod, fx.midi.data(), fx.midi.size(), shift.data(), shift.size(), id, id, nullptr, false };
    const auto off = psolaWarp(in, 0, fx.x.size());
    in.levelMatch = true;
    const auto on = psolaWarp(in, 0, fx.x.size());
    const auto dOff = rmsDb(off, fx.x, 0.6, 1.1), dOn = rmsDb(on, fx.x, 0.6, 1.1);
    std::printf("  warp +7 st: RMS change off %+.2f dB, on %+.2f dB\n", dOff, dOn);
    check(std::abs(dOn) <= 0.5 && on.size() == off.size(), "level match: psolaWarp voiced RMS within 0.5 dB");
    const auto part = psolaWarp(in, 20000, 30000);
    check(std::equal(part.begin(), part.end(), on.begin() + 20000), "level match: psolaWarp sub-range equals the whole render");
}
} // namespace

int main()
{
    testLevel();
    testUnchanged();
    testSmooth();
    testWarp();
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
