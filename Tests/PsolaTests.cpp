#include "../Source/Render/Psola.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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

// 배음 20개 모음 같은 소리, [0.2, 1.2)초 유성
std::vector<float> makeVoice(double hz, double total)
{
    std::vector<float> x(static_cast<size_t>(total * sr), 0.0f);
    for (auto i = static_cast<size_t>(0.2 * sr); i < static_cast<size_t>(1.2 * sr); ++i)
    {
        double v = 0.0;
        for (int h = 1; h <= 20; ++h)
            v += std::sin(2.0 * pi * hz * h * i / sr) / h;
        x[i] = static_cast<float>(0.2 * v);
    }
    return x;
}

// 자기상관으로 [a, b)초의 기본 주파수
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
    // 옥타브 아래로 잡히지 않게: 최댓값의 90%를 넘는 첫 봉우리
    for (int l = sr / 600 + 1; l < sr / 80 - 1; ++l)
        if (r[l] > 0.9 * best && r[l] >= r[l - 1] && r[l] >= r[l + 1])
            return static_cast<double>(sr) / l;
    return 0.0;
}

struct Fixture
{
    std::vector<float> x;
    std::vector<double> midi, shift;
    PsolaInput in() const { return { x.data(), x.size(), sr, framePeriod, midi.data(), shift.data(), midi.size() }; }
};

Fixture makeFixture(double hz, double shiftSemis)
{
    Fixture f;
    f.x = makeVoice(hz, 1.5);
    const auto frames = static_cast<size_t>(1.5 / framePeriod);
    f.midi.assign(frames, 0.0);
    f.shift.assign(frames, 0.0);
    for (size_t k = 0; k < frames; ++k)
        if (k * framePeriod >= 0.2 && k * framePeriod < 1.2) { f.midi[k] = midiOf(hz); f.shift[k] = shiftSemis; }
    return f;
}

void testMarks()
{
    const auto f = makeFixture(200.0, 0.0);
    const auto marks = pitchMarks(f.in(), 0, f.x.size());
    check(marks.size() > 180 && marks.size() < 220, "200 Hz 1초에 마크 약 200개");
    bool even = true;
    for (size_t k = 1; k < marks.size(); ++k)
        even = even && std::abs(marks[k] - marks[k - 1] - sr / 200.0) < 3.0;
    check(even, "마크 간격이 한 주기");
}

// 유성 시작 프레임이 어디든 첫 주기 간격이 맞아야 한다 (샘플→프레임 반올림으로 앞 무성 프레임을 읽던 버그)
void testRunStartAnyFrame()
{
    bool ok = true;
    for (size_t start = 37; start < 64; ++start)
    {
        auto f = makeFixture(200.0, 0.0);
        for (size_t i = 0; i < f.x.size(); ++i)   // 처음부터 소리가 있게 (마크 위치만 시험)
            f.x[i] = static_cast<float>(0.2 * std::sin(2.0 * pi * 200.0 * i / sr) + 0.1 * std::sin(4.0 * pi * 200.0 * i / sr));
        for (size_t k = 0; k < start; ++k) f.midi[k] = 0.0;
        for (size_t k = start; k < 240; ++k) f.midi[k] = midiOf(200.0);
        const auto marks = pitchMarks(f.in(), 0, f.x.size());
        ok = ok && marks.size() > 2 && std::abs(marks[1] - marks[0] - sr / 200.0) < 3.0;
    }
    check(ok, "유성 시작 프레임과 무관하게 첫 마크 간격이 한 주기");
}

void testPassthrough()
{
    const auto f = makeFixture(200.0, 0.0);
    const auto y = psolaShift(f.in(), 0, f.x.size());
    bool same = y.size() == f.x.size();
    for (size_t i = 0; same && i < y.size(); ++i) same = y[i] == f.x[i];
    check(same, "이동 0이면 원본 그대로");
}

void testShiftUp()
{
    const auto f = makeFixture(200.0, 1.0);
    const auto y = psolaShift(f.in(), 0, f.x.size());
    const auto hz = pitchHz(y, 0.5, 0.9);
    std::printf("  +1반음 결과 %.1f Hz (목표 %.1f)\n", hz, 200.0 * std::pow(2.0, 1.0 / 12.0));
    check(std::abs(midiOf(hz) - midiOf(200.0) - 1.0) < 0.15, "+1반음이면 음정이 1반음 오른다");
    check(y[static_cast<size_t>(0.1 * sr)] == f.x[static_cast<size_t>(0.1 * sr)], "무성(앞 침묵)은 그대로");
}

void testShiftDown()
{
    const auto f = makeFixture(220.0, -2.0);
    const auto y = psolaShift(f.in(), 0, f.x.size());
    const auto hz = pitchHz(y, 0.5, 0.9);
    std::printf("  -2반음 결과 %.1f Hz\n", hz);
    check(std::abs(midiOf(hz) - midiOf(220.0) + 2.0) < 0.15, "-2반음이면 음정이 2반음 내린다");
    double peak = 0.0;
    for (const auto v : y) peak = std::max(peak, static_cast<double>(std::abs(v)));
    check(peak < 1.0, "레벨이 터지지 않는다");
}

void testSubRange()
{
    const auto f = makeFixture(200.0, 1.0);
    const auto full = psolaShift(f.in(), 0, f.x.size());
    const auto part = psolaShift(f.in(), static_cast<size_t>(0.4 * sr), static_cast<size_t>(1.0 * sr));
    check(part.size() == static_cast<size_t>(0.6 * sr), "부분 구간 길이");
    const auto hz = pitchHz(part, 0.1, 0.4);
    check(std::abs(midiOf(hz) - midiOf(200.0) - 1.0) < 0.15, "부분 구간도 1반음 오른다");
    double worst = 0.0;
    for (size_t i = 0; i < part.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(part[i] - full[static_cast<size_t>(0.4 * sr) + i])));
    std::printf("  부분/전체 최대 차 %.2e\n", worst);
    check(worst < 1e-4, "부분 구간 렌더 = 전체 렌더의 같은 자리 (구간 이음에서 위상이 어긋나지 않음)");
}
} // namespace

int main()
{
    testMarks();
    testRunStartAnyFrame();
    testPassthrough();
    testShiftUp();
    testShiftDown();
    testSubRange();
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
