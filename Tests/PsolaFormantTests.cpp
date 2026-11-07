#include "VowelSynth.h"
#include "../Source/Render/EditRenderer.h"
#include "../Source/Render/Psola.h"
#include "../Source/Render/PsolaGrain.h"
#include "../Source/Render/PsolaWarp.h"
#include <complex>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// 포먼트 양(formantAmount): 1이면 기존 결과와 같고, 0이면 포먼트가 음정과 같이 움직인다
namespace
{
int failures = 0;
constexpr int sr = 44100;
constexpr double framePeriod = 0.005, f0 = 120.0, pi = 3.14159265358979323846, ratio7 = 1.4983070768766815;

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
    std::vector<float> render() const
    {
        const auto out = renderEdited(audio, features, blocks);
        return out && ! out->channels.empty() ? out->channels[0] : std::vector<float>{};
    }
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
    const auto kept = keep.render(), moved = move.render();
    check(! kept.empty() && kept != keep.audio.channels[0], "renderer: default amount renders audio that differs from the source");
    check(kept != moved, "renderer: block formantAmount changes the audio");
}

// 4-8 kHz 파워 / 전체 파워 (dB), 한 창
double highShareDb(const std::vector<float>& y, size_t begin, size_t n)
{
    double high = 0.0, total = 1e-30;
    for (size_t k = 1; k < n / 2; ++k)
    {
        std::complex<double> c = 0.0;
        for (size_t i = 0; i < n; ++i)
            c += (0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(n))) * y[begin + i] * std::polar(1.0, -2.0 * pi * static_cast<double>(k * i) / static_cast<double>(n));
        const auto power = std::norm(c);
        total += power;
        if (k * sr / n >= 4000 && k * sr / n <= 8000) high += power;
    }
    return 10.0 * std::log10(high / total + 1e-30);
}

// 한 옥타브 내림 + 포먼트가 같이 움직이는 경우(amount 0, 0.5): 조각이 서로 겹쳐야 하고 (이전에는 간격 == 창 길이라 가중치가
// 0으로 떨어진 곳에 원본이 섞여 4-8 kHz가 폭증하고 F2가 틀어졌다), 포먼트는 2^(-12/12 * (1-amount))만큼
void testOctaveDown()
{
    Fixture fx(-12.0);
    const auto src = middle(fx.x);
    const auto srcHigh = highShareDb(fx.x, static_cast<size_t>(0.7 * sr), 2048);
    for (const auto amount : { 0.0, 0.5 })
    {
        const std::vector<double> a(fx.midi.size(), amount);
        const auto y = psolaShift(fx.in(a.data()), 0, fx.x.size());
        const auto out = middle(y);
        const auto high = highShareDb(y, static_cast<size_t>(0.7 * sr), 2048);
        std::printf("  octave down amount %.1f: HF/total %.1f dB (source %.1f), F1 %.0f F2 %.0f\n", amount, high, srcHigh, out.hz[0], out.hz[1]);
        check(high < srcHigh + 15.0, "octave down: no broadband splice noise (4-8 kHz share stays near the source's)");
        check(near(src, out, std::pow(2.0, -(1.0 - amount)), 0.10), "octave down: F1/F2 follow the shift by the requested amount within 10%");
    }
}

// 가장자리: 원본 밖으로 나가는 포먼트 조각은 ratio 1 조각으로 대신하고, 그것도 안 되면 건너뛴다
void testGrainEdges()
{
    const std::vector<float> x(1000, 0.5f);
    PsolaInput in{ x.data(), x.size(), sr, framePeriod, nullptr, nullptr, 0, nullptr };
    const auto weights = [&](long mark, double ratio)
    {
        psola::Synth s;
        s.out.assign(600, 0.0);
        s.weight.assign(600, 0.0);
        psola::addGrain(in, s, mark, 300, 100, ratio);
        return s.weight;
    };
    check(weights(120, 1.5) == weights(120, 1.0), "grain edge: resampled span outside the source falls back to the ratio 1 grain");
    check(weights(20, 1.5) == weights(20, 1.0) && weights(20, 1.0) == std::vector<double>(600, 0.0), "grain edge: skipped when even ratio 1 does not fit");
    check(weights(880, 1.5) == weights(880, 1.0), "grain edge: same fallback near the end");
    double total = 0.0;
    for (const auto w : weights(500, 0.5)) total += w;
    check(total > 50.0, "grain edge: interior resampled grain has weight");
}
} // namespace

int main()
{
    testPsola();
    testWarp();
    testRenderer();
    testOctaveDown();
    testGrainEdges();
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
