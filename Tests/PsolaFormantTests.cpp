#include "VowelSynth.h"
#include "../Source/Render/EditRenderer.h"
#include "../Source/Render/Psola.h"
#include "../Source/Render/PsolaWarp.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

// 포먼트 양(formantAmount): 1이면 기존 결과와 같고, 0이면 포먼트가 음정과 같이 움직인다
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
    std::vector<double> midi, shift, one;
    Fixture(double st)
    {
        const auto n = static_cast<size_t>(1.6 * sr);
        x = vowelsynth::synth(vowelsynth::vowelA(), f0, n, sr);
        for (size_t i = 0; i < n; ++i)
            if (i < static_cast<size_t>(0.2 * sr) || i >= static_cast<size_t>(1.4 * sr)) x[i] = 0.0f;
        const auto frames = static_cast<size_t>(1.6 / framePeriod) + 1;
        midi.assign(frames, 0.0);
        shift.assign(frames, 0.0);
        one.assign(frames, 1.0);
        for (size_t i = 40; i < 280; ++i) { midi[i] = midiOf(f0); shift[i] = st; }
    }
    PsolaInput in(const double* amount) const
    {
        return { x.data(), x.size(), sr, framePeriod, midi.data(), shift.data(), midi.size(), amount };
    }
};

Formants middle(const std::vector<float>& y) { return vowelsynth::measure(y, static_cast<size_t>(0.7 * sr), 2048, sr); }

void report(const char* what, const Formants& src, const Formants& out)
{
    std::printf("  %s: F1 %.0f -> %.0f (x%.3f), F2 %.0f -> %.0f (x%.3f)\n", what, src.hz[0], out.hz[0], out.hz[0] / src.hz[0],
                src.hz[1], out.hz[1], out.hz[1] / src.hz[1]);
}

bool near(const Formants& src, const Formants& out, double r, double tol)
{
    return src.count >= 2 && out.count >= 2 && std::abs(out.hz[0] / src.hz[0] / r - 1.0) < tol && std::abs(out.hz[1] / src.hz[1] / r - 1.0) < tol;
}

void testPsola()
{
    Fixture fx(7.0);
    const auto src = middle(fx.x);
    const auto plain = psolaShift(fx.in(nullptr), 0, fx.x.size());
    check(plain == psolaShift(fx.in(fx.one.data()), 0, fx.x.size()), "psola: amount 1 is bit-identical to nullptr");
    for (size_t i = 100; i < 200; ++i) fx.shift[i] = 0.05 * static_cast<double>(i - 100);
    check(psolaShift(fx.in(nullptr), 0, fx.x.size()) == psolaShift(fx.in(fx.one.data()), 0, fx.x.size()),
          "psola: amount 1 is bit-identical with a moving shift curve");
    Fixture a(7.0), b(7.0);
    const std::vector<double> zero(a.midi.size(), 0.0);
    const auto keep = middle(psolaShift(a.in(nullptr), 0, a.x.size()));
    const auto move = middle(psolaShift(b.in(zero.data()), 0, b.x.size()));
    report("psola amount=1 +7st", src, keep);
    report("psola amount=0 +7st", src, move);
    check(near(src, keep, 1.0, 0.10), "psola: amount 1 keeps F1/F2 within 10%");
    check(near(src, move, ratio7, 0.10), "psola: amount 0 moves F1/F2 by 2^(7/12) within 10%");
}

WarpInput warpInput(const Fixture& fx, const double* amount)
{
    return { fx.x.data(), fx.x.size(), sr, framePeriod, fx.midi.data(), fx.midi.size(), fx.shift.data(), fx.shift.size(),
             [](double t) { return t; }, [](double s) { return s; }, amount };
}

void testWarp()
{
    Fixture fx(7.0);
    const auto src = middle(fx.x);
    const auto plain = psolaWarp(warpInput(fx, nullptr), 0, fx.x.size());
    check(plain == psolaWarp(warpInput(fx, fx.one.data()), 0, fx.x.size()), "warp: amount 1 is bit-identical to nullptr");
    const std::vector<double> zero(fx.midi.size(), 0.0);
    const auto keep = middle(plain);
    const auto move = middle(psolaWarp(warpInput(fx, zero.data()), 0, fx.x.size()));
    report("warp amount=1 +7st", src, keep);
    report("warp amount=0 +7st", src, move);
    check(near(src, keep, 1.0, 0.10), "warp: amount 1 keeps F1/F2 within 10%");
    check(near(src, move, ratio7, 0.10), "warp: amount 0 moves F1/F2 by 2^(7/12) within 10%");
}

// 편집 경로: 블록의 formantAmount가 렌더까지 이어진다
struct Render
{
    AudioPlanes audio;
    VocalFeatures features;
    std::vector<VocalBlock> blocks;
    explicit Render(double amount)
    {
        Fixture fx(0.0);
        audio.sampleRate = sr;
        audio.channels = { fx.x };
        features.timeSec.resize(fx.midi.size());
        for (size_t i = 0; i < fx.midi.size(); ++i) features.timeSec[i] = static_cast<double>(i) * framePeriod;
        features.midiF0 = fx.midi;
        features.rms.assign(fx.midi.size(), 0.5);
        VocalBlock b;
        b.beginFrame = 40; b.endFrame = 280; b.startSec = 0.2; b.endSec = 1.4;
        b.baseMidi = static_cast<int>(std::lround(midiOf(f0)));
        b.pitchOffset = midiOf(f0) - b.baseMidi;
        b.tidyAmount = 0.0;
        b.pitchShift = 7.0;
        b.formantAmount = amount;
        blocks = { b };
    }
    std::vector<float> render() const { return renderEdited(audio, features, blocks)->channels[0]; }
};

void testRenderer()
{
    const Render keep(1.0), move(0.0);
    const auto src = middle(keep.audio.channels[0]);
    const auto a = middle(keep.render()), b = middle(move.render());
    report("renderer amount=1 +7st", src, a);
    report("renderer amount=0 +7st", src, b);
    check(near(src, a, 1.0, 0.10), "renderer: block formantAmount 1 keeps formants");
    check(near(src, b, ratio7, 0.10), "renderer: block formantAmount 0 moves formants");
    auto plainBlocks = keep.blocks;
    plainBlocks[0].formantAmount = 1.0;
    check(renderEdited(keep.audio, keep.features, plainBlocks)->channels == std::vector<std::vector<float>>{ keep.render() },
          "renderer: default amount is deterministic");
}
} // namespace

int main()
{
    testPsola();
    testWarp();
    testRenderer();
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
