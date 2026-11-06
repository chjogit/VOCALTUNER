#include "../Source/Render/VowelWeight.h"
#include "../Source/Render/EditRenderer.h"
#include "VowelSynth.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace
{
int failures = 0;
constexpr int sr = 48000;
constexpr double period = 0.005, f0 = 200.0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

size_t samplesOf(double sec) { return static_cast<size_t>(sec * sr); }

struct Scene
{
    std::vector<float> x;
    std::vector<double> midi;
    void append(const std::vector<float>& part, bool voiced = true)
    {
        x.insert(x.end(), part.begin(), part.end());
        midi.resize(x.size() / samplesOf(period), voiced ? 69.0 + 12.0 * std::log2(f0 / 440.0) : 0.0);
    }
    std::vector<double> weights(size_t a = 0, size_t b = static_cast<size_t>(-1)) const
    {
        return vowelWeights(x.data(), x.size(), sr, period, midi.data(), midi.size(), a, b);
    }
};

std::vector<float> vowel(double sec, double peak = 0.3) { return vowelsynth::synth(vowelsynth::vowelA(), f0, samplesOf(sec), sr, peak); }

std::vector<float> whiteNoise(double sec, double peak)
{
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    std::vector<float> x(samplesOf(sec));
    for (auto& v : x) v = static_cast<float>(peak) * d(rng);
    return x;
}

// Paul Kellet 근사 핑크 잡음
std::vector<float> pinkNoise(double sec, double peak)
{
    auto w = whiteNoise(sec, 1.0);
    double b0 = 0, b1 = 0, b2 = 0;
    for (auto& v : w)
    {
        b0 = 0.99765 * b0 + v * 0.0990460; b1 = 0.96300 * b1 + v * 0.2965164; b2 = 0.57000 * b2 + v * 1.0526913;
        v = static_cast<float>(peak * 0.25 * (b0 + b1 + b2 + v * 0.1848));
    }
    return w;
}

// 치찰음처럼 고역 위주 잡음 (부호 교대)
std::vector<float> hiss(double sec, double peak)
{
    auto w = whiteNoise(sec, peak);
    for (size_t i = 0; i < w.size(); i += 2) w[i] = -w[i];
    return w;
}

// 비음처럼 낮고 조용한 유성: 저역 통과 펄스열, 약한 포먼트, 모음보다 RMS 25 dB 낮다
std::vector<float> nasal(double sec)
{
    std::vector<double> x(samplesOf(sec), 0.0);
    for (double t = 0.0; t < static_cast<double>(x.size()); t += sr / f0) x[static_cast<size_t>(std::lround(t))] = 1.0;
    for (int i = 0; i < 4; ++i) vowelsynth::onePole(x, 0.97);
    vowelsynth::resonate(x, 250.0, 200.0, sr);
    const auto ref = vowel(0.5);   // RMS를 모음에 맞춘 뒤 25 dB 낮춘다
    double eRef = 0.0, eNasal = 1e-24;
    for (const auto v : ref) eRef += static_cast<double>(v) * v;
    for (const auto v : x) eNasal += v * v;
    const auto gain = std::sqrt(eRef / static_cast<double>(ref.size()) / (eNasal / static_cast<double>(x.size()))) * std::pow(10.0, -25.0 / 20.0);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float>(gain * x[i]);
    return y;
}

double lowestIn(const std::vector<double>& w, double a, double b) { return *std::min_element(w.begin() + static_cast<long>(a / period), w.begin() + static_cast<long>(b / period)); }
double highestIn(const std::vector<double>& w, double a, double b) { return *std::max_element(w.begin() + static_cast<long>(a / period), w.begin() + static_cast<long>(b / period)); }

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

struct RenderFixture
{
    AudioPlanes audio;
    VocalFeatures f;
    VocalBlock b;
    explicit RenderFixture(const Scene& s)
    {
        audio.sampleRate = sr;
        audio.channels = { s.x };
        f.timeSec.resize(s.midi.size());
        for (size_t i = 0; i < f.timeSec.size(); ++i) f.timeSec[i] = static_cast<double>(i) * period;
        f.midiF0 = s.midi;
        f.rms.assign(s.midi.size(), 0.5);
        b.beginFrame = 10; b.endFrame = s.midi.size() - 10; b.startSec = 0.05; b.endSec = 0.95;
        b.baseMidi = static_cast<int>(std::lround(s.midi[11])); b.pitchOffset = s.midi[11] - b.baseMidi; b.tidyAmount = 0.0; b.pitchShift = 3.0;
    }
};

// 같은 입력으로 vowelFocus 꺼짐/켜짐 렌더: 꺼짐은 기본과 비트까지 같고, 켜면 숨소리 구간의 이동이 줄어 출력이 달라진다
void testRenderFlag()
{
    Scene s;
    s.append(vowel(0.6));
    s.append(whiteNoise(0.4, 0.1));
    const RenderFixture fx(s);
    RenderOptions off, on;
    off.vowelFocus = false;
    on.vowelFocus = true;
    const auto d = renderEdited(fx.audio, fx.f, { fx.b }), o = renderEdited(fx.audio, fx.f, { fx.b }, off), v = renderEdited(fx.audio, fx.f, { fx.b }, on);
    check(d && o && v && d->channels[0] == o->channels[0], "vowelFocus off: render bit-identical to the default");
    check(v && v->channels[0] != o->channels[0], "vowelFocus on: render differs (shift reduced in the noise)");
}
} // namespace

int main()
{
    testVowel();
    testNoise();
    testFricative();
    testNasal();
    testSequence();
    testRange();
    testApply();
    testRenderFlag();
    std::printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
