#include "../Source/Render/PsolaWarp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace
{
int failures = 0;
constexpr int sr = 44100;
constexpr double framePeriod = 0.005;
constexpr double pi = 3.14159265358979;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

double midiOf(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

// 앞 [0.02, 0.12)초 잡음(무성), [0.2, 1.2)초 200 Hz 모음
struct Fixture
{
    std::vector<float> x;
    std::vector<double> midi;
    Fixture()
    {
        x.assign(static_cast<size_t>(1.5 * sr), 0.0f);
        std::mt19937 rng(7);
        std::normal_distribution<float> noise(0.0f, 0.05f);
        for (auto i = static_cast<size_t>(0.02 * sr); i < static_cast<size_t>(0.12 * sr); ++i) x[i] = noise(rng);
        for (auto i = static_cast<size_t>(0.2 * sr); i < static_cast<size_t>(1.2 * sr); ++i)
        {
            double v = 0.0;
            for (int h = 1; h <= 20; ++h) v += std::sin(2.0 * pi * 200.0 * h * i / sr) / h;
            x[i] = static_cast<float>(0.2 * v);
        }
        midi.assign(static_cast<size_t>(1.5 / framePeriod), 0.0);
        for (size_t k = 0; k < midi.size(); ++k)
            if (k * framePeriod >= 0.2 && k * framePeriod < 1.2) midi[k] = midiOf(200.0);
    }
};

double pitchHz(const std::vector<float>& x, double a, double b)
{
    const auto i0 = static_cast<size_t>(a * sr), n = static_cast<size_t>((b - a) * sr);
    std::vector<double> r(static_cast<size_t>(sr / 80), 0.0);
    double best = 0.0;
    for (int l = sr / 600; l < sr / 80; ++l)
    {
        for (size_t i = 0; i < n; ++i) r[static_cast<size_t>(l)] += static_cast<double>(x[i0 + i]) * x[i0 + i + static_cast<size_t>(l)];
        best = std::max(best, r[static_cast<size_t>(l)]);
    }
    for (int l = sr / 600 + 1; l < sr / 80 - 1; ++l)
        if (r[l] > 0.9 * best && r[l] >= r[l - 1] && r[l] >= r[l + 1]) return static_cast<double>(sr) / l;
    return 0.0;
}

// 박자 지도: 출력 t -> 원본 (t - delay) / rate
struct Map
{
    double delay = 0.0, rate = 1.0;
    double toSource(double t) const { return (t - delay) / rate; }
    double toDisplay(double s) const { return s * rate + delay; }
};

std::vector<float> run(const Fixture& f, const Map& m, double shift, size_t a, size_t b, double totalSec = 1.9)
{
    const std::vector<double> shifts(static_cast<size_t>(totalSec / framePeriod), shift);
    WarpInput in { f.x.data(), f.x.size(), sr, framePeriod, f.midi.data(), f.midi.size(), shifts.data(), shifts.size(),
                   [m](double t) { return m.toSource(t); }, [m](double s) { return m.toDisplay(s); } };
    return psolaWarp(in, a, b);
}

// 같은 소리인지: 겹치는 구간의 상대 오차 (각 샘플의 차 제곱합 / 원본 제곱합)
double relativeError(const std::vector<float>& y, size_t yOffset, const std::vector<float>& x, size_t xOffset, size_t n)
{
    double d = 0.0, e = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double a = y[yOffset + i], b = x[xOffset + i];
        d += (a - b) * (a - b);
        e += b * b;
    }
    return d / (e + 1e-12);
}

void testIdentity()
{
    Fixture f;
    const auto y = run(f, {}, 0.0, 0, f.x.size());
    const auto err = relativeError(y, static_cast<size_t>(0.3 * sr), f.x, static_cast<size_t>(0.3 * sr), static_cast<size_t>(0.8 * sr));
    std::printf("  항등 상대 오차 %.2e\n", err);
    check(err < 0.01, "박자·음정 그대로면 원본과 같은 소리");
}

void testDelay()
{
    Fixture f;
    const auto d = 0.1;
    const auto y = run(f, { d, 1.0 }, 0.0, 0, static_cast<size_t>(1.7 * sr));
    const auto off = static_cast<size_t>(d * sr);
    const auto err = relativeError(y, static_cast<size_t>(0.4 * sr) + off, f.x, static_cast<size_t>(0.4 * sr), static_cast<size_t>(0.6 * sr));
    std::printf("  지연 상대 오차 %.2e\n", err);
    check(err < 0.05, "0.1초 늦추면 같은 소리가 0.1초 뒤에 나온다");
    check(std::abs(y[static_cast<size_t>(0.25 * sr)]) < 1e-6, "늦춘 앞은 조용");
}

void testStretchKeepsPitch()
{
    Fixture f;
    const auto y = run(f, { 0.0, 1.25 }, 0.0, 0, static_cast<size_t>(1.9 * sr));
    const auto hz = pitchHz(y, 0.6, 1.0);
    std::printf("  1.25배 늘림 결과 %.1f Hz\n", hz);
    check(std::abs(midiOf(hz) - midiOf(200.0)) < 0.15, "늘려도 음정이 그대로");
    check(std::abs(y[static_cast<size_t>(1.4 * sr)]) > 0.01, "원래 1.12초쯤 끝나던 모음이 1.4초에도 이어진다");
    check(std::abs(y[static_cast<size_t>(1.6 * sr)]) < 1e-6, "늘어난 끝(1.5초) 뒤는 조용");
}

void testShiftWithMove()
{
    Fixture f;
    const auto y = run(f, { 0.05, 1.0 }, 1.0, 0, static_cast<size_t>(1.7 * sr));
    const auto hz = pitchHz(y, 0.5, 0.9);
    check(std::abs(midiOf(hz) - midiOf(200.0) - 1.0) < 0.15, "박자를 옮기면서 +1반음도 된다");
}

void testNoiseMovedWhole()
{
    Fixture f;
    const auto d = 0.05;
    const auto y = run(f, { d, 1.0 }, 0.0, 0, static_cast<size_t>(1.7 * sr));
    const auto off = static_cast<size_t>(d * sr);
    const auto err = relativeError(y, static_cast<size_t>(0.04 * sr) + off, f.x, static_cast<size_t>(0.04 * sr), static_cast<size_t>(0.06 * sr));
    std::printf("  자음 이동 상대 오차 %.2e\n", err);
    check(err < 1e-3, "무성(잡음)은 늘리지 않고 통째로 옮겨진다");
}

// 자기상관 봉우리를 포물선으로 보간해 기본 주파수를 센트 단위로 잰다
double preciseMidi(const std::vector<float>& x, double a, double b)
{
    const auto i0 = static_cast<size_t>(a * sr), n = static_cast<size_t>((b - a) * sr);
    const auto r = [&](int l) { double s = 0.0; for (size_t i = 0; i < n; ++i) s += static_cast<double>(x[i0 + i]) * x[i0 + i + static_cast<size_t>(l)]; return s; };
    int best = sr / 300;
    double top = -1e9;
    for (int l = sr / 300; l < sr / 120; ++l)
        if (const auto v = r(l); v > top) { top = v; best = l; }
    const auto y0 = r(best - 1), y1 = r(best), y2 = r(best + 1);
    const auto lag = best + 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
    return midiOf(sr / lag);
}

// 늘임이 1에서 조금 벗어난 박자(3~4%)에서 음정이 늘임 비율만큼 변하면 안 된다 (원본 마크 쪽으로 끌어당기는 잠금이 새던 결함)
void testSmallStretchKeepsPitch()
{
    Fixture f;
    const auto base = preciseMidi(f.x, 0.5, 0.9);
    bool ok = true;
    for (const auto rate : { 0.96, 0.97, 1.03, 1.04 })
    {
        const auto y = run(f, { 0.0, rate }, 0.0, 0, static_cast<size_t>(1.9 * sr));
        const auto cents = 100.0 * (preciseMidi(y, 0.5, 0.9) - base);
        std::printf("  늘임 %.2f 음정 변화 %+.1f센트\n", rate, cents);
        ok = ok && std::abs(cents) < 8.0;
    }
    check(ok, "늘임 3~4%여도 음정이 8센트 안에서 그대로");
}

void testSubRange()
{
    Fixture f;
    const Map m { 0.1, 1.15 };
    const auto full = run(f, m, 0.5, 0, static_cast<size_t>(1.8 * sr));
    const auto a = static_cast<size_t>(0.5 * sr);
    const auto part = run(f, m, 0.5, a, static_cast<size_t>(1.1 * sr));
    double worst = 0.0;
    for (size_t i = 0; i < part.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(part[i] - full[a + i])));
    std::printf("  부분/전체 최대 차 %.2e\n", worst);
    check(worst < 1e-4, "부분 구간 렌더 = 전체 렌더의 같은 자리");
}

// 모음 둘 사이에 긴 무성이 있을 때, 둘째 모음 첫 마크의 주기가 앞 모음 끝 마크와의 거리로 잡히면 조각이 수 초 길이가 되어 시각이 밀린다
void testSecondRun()
{
    Fixture f;
    f.x.assign(static_cast<size_t>(4.5 * sr), 0.0f);
    f.midi.assign(static_cast<size_t>(4.5 / framePeriod), 0.0);
    for (const auto start : { 0.2, 3.2 })
        for (auto i = static_cast<size_t>(start * sr); i < static_cast<size_t>((start + 0.5) * sr); ++i)
        {
            double v = 0.0;
            for (int h = 1; h <= 20; ++h) v += std::sin(2.0 * pi * 200.0 * h * i / sr) / h;
            f.x[i] = static_cast<float>(0.2 * v);
            f.midi[static_cast<size_t>(i / sr / framePeriod)] = midiOf(200.0);
        }
    const auto y = run(f, { 0.0, 0.95 }, 0.0, 0, static_cast<size_t>(4.2 * sr), 4.5);
    const auto at = [&](double sec, double len)
    {
        double e = 0.0;
        for (size_t i = 0; i < static_cast<size_t>(len * sr); ++i) e += y[static_cast<size_t>(sec * sr) + i] * y[static_cast<size_t>(sec * sr) + i];
        return std::sqrt(e / (len * sr));
    };
    std::printf("  둘째 모음 자리(3.1초) rms %.3f, 원래 자리(3.4초) rms %.3f\n", at(3.1, 0.2), at(3.45, 0.1));
    check(at(3.1, 0.2) > 0.1, "둘째 모음이 박자 지도대로 3.04초쯤에 시작한다");
    check(at(2.0, 0.5) < 1e-4, "두 모음 사이는 조용");
    const auto part = run(f, { 0.0, 0.95 }, 0.0, static_cast<size_t>(2.8 * sr), static_cast<size_t>(3.6 * sr), 4.5);
    double worst = 0.0;
    for (size_t i = 0; i < part.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(part[i] - y[static_cast<size_t>(2.8 * sr) + i])));
    check(worst < 1e-4, "앞 모음 뒤에서 이어진 렌더와 둘째 모음만 렌더가 같은 소리 (무성 격자로 복귀)");
}
} // namespace

int main()
{
    testIdentity();
    testDelay();
    testStretchKeepsPitch();
    testShiftWithMove();
    testSmallStretchKeepsPitch();
    testNoiseMovedWhole();
    testSubRange();
    testSecondRun();
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
