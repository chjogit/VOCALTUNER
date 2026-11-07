#include "../Source/Alignment/BandAlign.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

constexpr int rate = 22050;
constexpr double pi = 3.14159265358979323846;

// 음절: 자음(잡음 40 ms) + 모음(배음, 음절마다 다른 모음 색). 길이가 조금씩 달라 리듬이 되풀이되지 않는다
struct Syllable { double start, length, midi; int vowel; };

std::vector<Syllable> phrase()
{
    std::vector<Syllable> s;
    double t = 0.5;
    for (int i = 0; i < 18; ++i)
    {
        const auto length = 0.2 + 0.06 * ((i * 5) % 4);
        s.push_back({ t, length, 60.0 + ((i * 7) % 5) * 2.0, i % 4 });
        t += length + (i == 8 ? 0.8 : 0.03);
    }
    return s;
}

std::vector<float> render(const std::vector<Syllable>& syllables, double seconds, double transpose, double delay, unsigned seed)
{
    std::vector<float> x(static_cast<size_t>(seconds * rate), 0.0f);
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    // 모음 색: 배음별 세기 (포먼트 대신 대역마다 다른 에너지)
    static const double colour[4][12] = { { 1, .8, .5, .2, .1, .05, .03, .02, .01, .01, .01, .01 },
                                          { .4, .5, .9, 1, .6, .3, .1, .05, .03, .02, .01, .01 },
                                          { .5, .2, .1, .3, .6, 1, .8, .4, .2, .1, .05, .02 },
                                          { .3, .3, .2, .1, .1, .2, .4, .7, 1, .8, .5, .3 } };
    for (const auto& s : syllables)
    {
        const auto begin = static_cast<size_t>((s.start + delay) * rate);
        const auto consonant = static_cast<size_t>(0.04 * rate);
        const auto total = static_cast<size_t>(s.length * rate);
        const auto hz = 440.0 * std::pow(2.0, (s.midi + transpose - 69.0) / 12.0);
        float hp = 0.0f, last = 0.0f;
        for (size_t i = 0; i < total && begin + i < x.size(); ++i)
        {
            double v = 0.0;
            if (i < consonant)
            {
                const auto n = noise(rng);
                hp = n - last;   // 고역 잡음 (치찰음 비슷하게)
                last = n;
                v = 0.15 * hp;
            }
            else
            {
                const auto t = static_cast<double>(i - consonant) / rate;
                for (int h = 1; h <= 12; ++h)
                    v += 0.05 * colour[s.vowel][h - 1] * std::sin(2.0 * pi * hz * h * t);
                v *= std::min(1.0, static_cast<double>(total - i) / (0.02 * rate));
            }
            x[begin + i] += static_cast<float>(v);
        }
    }
    return x;
}

BandFrames bandsOf(const std::vector<float>& x)
{
    const StereoReader read = [&x](int64_t start, int count, float* l, float* r) {
        for (int i = 0; i < count; ++i)
        {
            const auto k = start + i;
            l[i] = r[i] = k >= 0 && k < static_cast<int64_t>(x.size()) ? x[static_cast<size_t>(k)] : 0.0f;
        }
    };
    return computeBandFrames(read, static_cast<int64_t>(x.size()), rate);
}

double shiftAt(const TimeMap& map, double t) { return map.map(t) - t; }

void testIdentical()
{
    const auto bands = bandsOf(render(phrase(), 10.0, 0.0, 0.0, 1));
    const auto map = bandAlignMap(bands, 0.0, bands, 0.0);
    check(! map.empty() && std::abs(shiftAt(map, 2.0)) < 0.011 && std::abs(shiftAt(map, 5.0)) < 0.011, "identical tracks: no shift");
}

// 둘째 프레이즈 가운데(음절 13)부터 4반음 위로, 0.12초 늦게 들어오는 화음 -> 이동량 -0.12
void testMidPhraseHarmony()
{
    const auto all = phrase();
    const std::vector<Syllable> late(all.begin() + 13, all.end());
    const auto ref = bandsOf(render(all, 10.0, 0.0, 0.0, 1));
    const auto tgt = bandsOf(render(late, 10.0, 4.0, 0.12, 2));
    const auto map = bandAlignMap(ref, 0.0, tgt, 0.0);
    const auto middle = late[2].start + 0.12 + 0.1;
    check(! map.empty() && std::abs(shiftAt(map, middle) + 0.12) < 0.025, "mid-phrase harmony (+4 st, 0.12 s late) is pulled back");
}

void testOffsetsAndEarly()
{
    const auto all = phrase();
    std::vector<Syllable> late(all.begin() + 4, all.begin() + 9);
    for (auto& s : late) s.start -= 0.3;   // 타깃 클립은 곡 0.3초에서 시작
    const auto ref = bandsOf(render(all, 10.0, 0.0, 0.0, 1));
    const auto tgt = bandsOf(render(late, 10.0, 7.0, -0.08, 3));
    const auto map = bandAlignMap(ref, 0.0, tgt, 0.3);
    const auto middle = late[2].start + 0.3 - 0.08 + 0.1;
    check(! map.empty() && std::abs(shiftAt(map, middle) - 0.08) < 0.025, "clip offset: early harmony (+7 st) is pushed later");
}

void testLimits()
{
    const auto all = phrase();
    const auto ref = bandsOf(render(all, 10.0, 0.0, 0.0, 1));
    const auto tgt = bandsOf(render(all, 10.0, 0.0, 0.25, 2));
    BandAlignOptions o;
    o.maxShiftSec = 0.1;
    const auto map = bandAlignMap(ref, 0.0, tgt, 0.0, o);
    bool within = true;
    for (double t = 0.5; t < 9.0; t += 0.05) within = within && std::abs(shiftAt(map, t)) <= 0.1 + 1e-9;
    check(within, "max shift limits every move");
    check(bandAlignMap(ref, 0.0, bandsOf(std::vector<float>(10 * rate, 0.0f)), 0.0).empty(), "silent target gives an empty map");
    check(bandAlignMap(ref, 0.0, BandFrames {}, 0.0).empty(), "empty target gives an empty map");
}
} // namespace

int main()
{
    testIdentical();
    testMidPhraseHarmony();
    testOffsetsAndEarly();
    testLimits();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
