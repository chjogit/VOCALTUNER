#include "../Source/Render/ConsonantShield.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
int failures = 0;
constexpr int sr = 48000;
constexpr double period = 0.005, pi = 3.14159265358979;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

// 0~1초 모음(200 Hz 배음), 1~2초 치찰음(고역 잡음), 2~3초 모음
std::vector<float> speech()
{
    std::vector<float> x(3 * sr);
    std::srand(7);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto t = static_cast<double>(i) / sr;
        if (i >= sr && i < 2 * sr)
        {
            const auto n = static_cast<double>(std::rand()) / RAND_MAX - 0.5;
            x[i] = static_cast<float>(0.4 * (n - 0.9 * (i > 0 ? x[i - 1] / 0.4 + 0.0 : 0.0) * 0.0) * (i % 2 ? 1.0 : -1.0));   // 고역 위주 (부호 교대)
        }
        else
            x[i] = static_cast<float>(0.2 * (std::sin(2 * pi * 200 * t) + 0.5 * std::sin(2 * pi * 400 * t) + 0.3 * std::sin(2 * pi * 600 * t)));
    }
    return x;
}

void testClassification()
{
    const auto x = speech();
    const auto flag = fricativeFrames(x.data(), x.size(), sr, period, 600);
    size_t vowel = 0, hiss = 0;
    for (size_t i = 0; i < 600; ++i)
    {
        vowel += (i < 190 || i >= 410) && flag[i];
        hiss += (i >= 205 && i < 395) && flag[i];
    }
    check(vowel == 0, "모음 프레임은 보호하지 않는다");
    check(hiss > 185, "치찰음 프레임은 보호한다");
}

void testShiftZeroed()
{
    const auto x = speech();
    const std::vector<double> shift(600, 1.0);
    const auto out = shieldedShift(x.data(), x.size(), sr, period, shift);
    check(out[100] == 1.0 && out[500] == 1.0, "모음의 이동량은 그대로");
    check(out[300] == 0.0, "치찰음의 이동량은 0");
    check(out[199] > 0.0 && out[199] < 1.0, "경계에서 서서히 줄어든다");
    bool monotone = true;
    for (size_t i = 190; i < 210; ++i) monotone = monotone && out[i] <= out[i - 1] + 1e-12;
    check(monotone, "경계 램프가 단조롭다");
}
} // namespace

int main()
{
    testClassification();
    testShiftZeroed();
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
