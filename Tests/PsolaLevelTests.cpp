#include "VowelSynth.h"
#include "../Source/Analysis/VocalFeatures.h"
#include "../Source/Render/Psola.h"
#include "../Source/Render/PsolaLevel.h"
#include "../Source/Render/EditRenderer.h"
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
    explicit Fixture(double st, double peak = 0.3)
    {
        const auto n = static_cast<size_t>(1.6 * sr);
        x = vowelsynth::synth(vowelsynth::vowelA(), f0, n, sr, peak);
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
    const auto db = psola::levelGainsDb(off, fx.x, psola::maskWeight(mask, sr), sr, 0);
    double step = 0.0, top = 0.0;
    for (size_t k = 1; k < db.size(); ++k) step = std::max(step, std::abs(db[k] - db[k - 1]));
    for (const auto g : db) top = std::max(top, std::abs(g));
    std::printf("  gain curve: %zu windows, max step %.2f dB, max |gain| %.2f dB\n", db.size(), step, top);
    check(step <= 0.5 + 1e-9 && top <= 6.0 + 1e-9 && db.size() > 100, "level match: gain curve has no step above 0.5 dB and stays within +-6 dB");
}

// 거의 풀스케일 입력: 보정한 출력이 표본마다 둘레 20 ms 원본 최대를 (+0.01 넘게) 넘지 않는다 (클립 방지)
void testPeakGuard()
{
    Fixture fx(-2.0, 0.97);
    const auto on = psolaShift(fx.in(true), 0, fx.x.size()), off = psolaShift(fx.in(false), 0, fx.x.size());
    const auto half = static_cast<size_t>(0.01 * sr);
    double worstOn = -1.0, worstOff = -1.0, peakOut = 0.0;
    for (size_t i = 0; i < on.size(); i += 7)
    {
        float local = 0.0f;
        for (auto k = i > half ? i - half : 0; k < std::min(on.size(), i + half); ++k) local = std::max(local, std::abs(fx.x[k]));
        worstOn = std::max(worstOn, static_cast<double>(std::abs(on[i]) - local));
        worstOff = std::max(worstOff, static_cast<double>(std::abs(off[i]) - local));
        peakOut = std::max(peakOut, static_cast<double>(std::abs(on[i])));
    }
    std::vector<double> mask(fx.x.size(), 0.0);
    for (auto i = static_cast<size_t>(0.6 * sr); i < static_cast<size_t>(1.1 * sr); ++i) mask[i] = 1.0;
    const auto db = psola::levelGainsDb(off, fx.x, psola::maskWeight(mask, sr), sr, 0);
    const auto gMax = std::pow(10.0, *std::max_element(db.begin(), db.end()) / 20.0);   // 보정이 최대로 걸었을 gain
    double unguarded = -1.0;
    for (auto i = static_cast<size_t>(0.6 * sr); i < static_cast<size_t>(1.1 * sr); i += 7)
    {
        float local = 0.0f;
        for (auto k = i > half ? i - half : 0; k < std::min(on.size(), i + half); ++k) local = std::max(local, std::abs(fx.x[k]));
        unguarded = std::max(unguarded, std::abs(off[i]) * gMax - local);
    }
    check(unguarded > 0.01, "the peak fixture needs the guard: unguarded gain would exceed the local source peak by > 0.01");
    std::printf("  peak guard -2 st: unguarded excess bound %+.4f; max excess over local source peak: on %+.4f, off %+.4f, out peak %.3f\n", unguarded, worstOn, worstOff, peakOut);
    check(worstOn <= 0.01, "level match: |out| <= local source peak + 0.01 on a near-full-scale fixture");
}

// 어느 구간을 렌더해도 같은 표본은 같은 값 (격자가 절대 표본에 묶임)
void testSubRange()
{
    Fixture fx(7.0);
    const auto whole = psolaShift(fx.in(true), 0, fx.x.size());
    bool same = true;
    for (const auto& [a, b] : { std::pair<size_t, size_t>{ 21001, 40003 }, { 30007, 50011 }, { 33333, 33999 }, { 45678, 69000 } })
    {
        const auto part = psolaShift(fx.in(true), a, b);
        same = same && std::equal(part.begin(), part.end(), whole.begin() + static_cast<long>(a));
    }
    check(same, "level match: psolaShift sub-ranges (odd offsets) equal the whole render");
}

// renderEdited: levelMatch 꺼짐은 기본과 같고(golden이 해시를 지킨다), 켜면 달라진다
void testRenderEdited()
{
    Fixture fx(7.0);
    AudioPlanes audio;
    audio.sampleRate = sr;
    audio.channels = { fx.x };
    VocalFeatures f;
    f.timeSec.resize(fx.midi.size());
    for (size_t i = 0; i < f.timeSec.size(); ++i) f.timeSec[i] = static_cast<double>(i) * framePeriod;
    f.midiF0 = fx.midi;
    f.rms.assign(fx.midi.size(), 0.5);
    VocalBlock b;
    b.beginFrame = 100; b.endFrame = 240; b.startSec = 0.5; b.endSec = 1.2;
    b.baseMidi = static_cast<int>(std::lround(fx.midi[101])); b.pitchOffset = fx.midi[101] - b.baseMidi; b.tidyAmount = 0.0; b.pitchShift = 7.0;
    RenderOptions off, on;
    on.levelMatch = true;
    const auto d = renderEdited(audio, f, { b }), o = renderEdited(audio, f, { b }, off), v = renderEdited(audio, f, { b }, on);
    check(d && o && v && d->channels[0] == o->channels[0], "renderEdited: levelMatch off equals the default render");
    check(v && v->channels[0] != o->channels[0], "renderEdited: levelMatch on differs");
}

// 합성이 덮지 못한 틈(출력 0)이 이웃 창의 gain을 끌어올리지 않는다 (warp 마스크는 합성 coverage로 거른다)
void testCoverageGap()
{
    Fixture fx(7.0);
    std::vector<float> y(fx.x.size());
    for (size_t i = 0; i < y.size(); ++i) y[i] = 0.9f * fx.x[i];   // 합성이 -0.9 dB 작게 나온 것으로
    std::vector<double> mask(y.size(), 0.0), cover(y.size(), 1.0);
    for (auto i = static_cast<size_t>(0.5 * sr); i < static_cast<size_t>(1.2 * sr); ++i) mask[i] = 1.0;
    for (auto i = static_cast<size_t>(0.80 * sr); i < static_cast<size_t>(0.85 * sr); ++i) { cover[i] = 0.0; y[i] = 0.0f; }
    auto plain = y, gated = y;
    psola::applyLevelMatch(plain, fx.x, psola::maskWeight(mask, sr), sr, 0);
    psola::applyLevelMatch(gated, fx.x, psola::maskWeight(psola::coveredMask(mask, cover), sr), sr, 0);
    const auto gainDb = [&](const std::vector<float>& out) { return rmsDb(out, y, 0.85, 0.87); };
    std::printf("  coverage gap: gain next to the gap plain %+.2f dB, gated %+.2f dB (ideal at most +0.92)\n", gainDb(plain), gainDb(gated));
    check(gainDb(plain) > gainDb(gated) + 0.5, "the gap fixture boosts the neighbours when the gap counts (so the test can fail)");
    check(gainDb(gated) < 0.92 + 0.3, "level match gated on synthesis coverage: the gap does not inflate the neighbouring gain");
    check(std::all_of(gated.begin() + static_cast<long>(0.80 * sr), gated.begin() + static_cast<long>(0.85 * sr), [](float v) { return v == 0.0f; }), "level match leaves the uncovered gap at zero");
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
    check(std::abs(dOff) > 0.7, "the warp fixture shows the loss without level match (so the test can fail)");
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
    testCoverageGap();
    testPeakGuard();
    testSubRange();
    testRenderEdited();
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
