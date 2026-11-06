#include "VowelSynth.h"
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

double relErr(double measured, double truth) { return std::abs(measured - truth) / truth; }

// F1이 기본 주파수 가까이(2.5배 미만)면 하모닉 표본이 F1을 제대로 못 잡는다: 오차 한계를 f0의 일부로 넓힌다
double allowedHz(double truth, double f0, double rel, double f0Share)
{
    return std::max(rel * truth, truth < 2.5 * f0 ? f0Share * f0 : 0.0);
}

void testLpc(const char* name, const vowelsynth::Vowel& v, double f0)
{
    const auto x = vowelsynth::synth(v, f0, 16384, sr);
    const auto f = vowelsynth::measure(x, 6000, 2048, sr);
    const auto e1 = f.count > 0 ? relErr(f.hz[0], v.hz[0]) : 9.0, e2 = f.count > 1 ? relErr(f.hz[1], v.hz[1]) : 9.0;
    std::printf("  %s f0=%.0f: F1=%.0f (%.1f%%) F2=%.0f (%.1f%%) count=%d\n", name, f0, f.hz[0], 100 * e1, f.hz[1], 100 * e2, f.count);
    check(e1 * v.hz[0] <= allowedHz(v.hz[0], f0, 0.08, 0.25) && e2 < 0.08, "LPC F1/F2 within 8% (F1 near f0: within f0/4)");
}

// 로컬 최대 중 목표 주파수에 가장 가까운 것의 주파수
double nearestPeakHz(const std::vector<double>& env, double hz)
{
    const auto nfft = 2 * (env.size() - 1);
    double best = 1e9;
    for (size_t b = 2; b + 2 < env.size(); ++b)
    {
        if (! (env[b] > env[b - 1] && env[b] >= env[b + 1])) continue;
        const auto at = static_cast<double>(b) * sr / static_cast<double>(nfft);
        if (std::abs(at - hz) < std::abs(best - hz)) best = at;
    }
    return best;
}

void testEnvelope(const char* name, const vowelsynth::Vowel& v, double f0)
{
    const auto x = vowelsynth::synth(v, f0, 16384, sr);
    std::vector<double> frame(x.begin() + 6000, x.begin() + 6000 + 2048);
    const auto env = trueEnvelope(frame.data(), frame.size(), sr, f0);
    const auto p1 = nearestPeakHz(env, v.hz[0]), p2 = nearestPeakHz(env, v.hz[1]);
    std::printf("  %s f0=%.0f: peaks %.0f / %.0f Hz (truth %.0f / %.0f)\n", name, f0, p1, p2, v.hz[0], v.hz[1]);
    check(env.size() == trueEnvelopeFftSize(frame.size()) / 2 + 1, "envelope has nfft/2+1 bins");
    check(std::abs(p1 - v.hz[0]) <= allowedHz(v.hz[0], f0, 0.12, 0.5) && relErr(p2, v.hz[1]) < 0.12,
          "True Envelope peaks near F1/F2 (F1 near f0: within f0/2)");
}

void testEnvelopeAboveSpectrum()
{
    const auto v = vowelsynth::vowelA();
    const auto x = vowelsynth::synth(v, 120.0, 16384, sr);
    std::vector<double> frame(x.begin() + 6000, x.begin() + 6000 + 2048);
    const auto env = trueEnvelope(frame.data(), frame.size(), sr, 120.0);
    double sum = 0.0;
    for (const auto e : env) sum += e;
    check(std::isfinite(sum), "envelope is finite");
    check(trueEnvelope(nullptr, 0, sr, 120.0).empty(), "empty input gives empty envelope");
    check(! trueEnvelope(frame.data(), frame.size(), sr, 0.0).empty(), "unvoiced fallback works");
}

void testDegenerate()
{
    const std::vector<double> silence(2048, 0.0);
    check(lpcFormants(silence.data(), silence.size(), sr).count == 0, "silence has no formants");
    check(lpcFormants(silence.data(), 10, sr).count == 0, "too-short frame has no formants");
}
} // namespace

int main()
{
    for (const auto f0 : { 120.0, 220.0 })
    {
        testLpc("/a/", vowelsynth::vowelA(), f0);
        testLpc("/i/", vowelsynth::vowelI(), f0);
        testEnvelope("/a/", vowelsynth::vowelA(), f0);
        testEnvelope("/i/", vowelsynth::vowelI(), f0);
    }
    testEnvelopeAboveSpectrum();
    testDegenerate();
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
