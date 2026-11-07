#include "../Source/Render/EditRenderer.h"
#include "../Source/Render/Psola.h"
#include "../Source/Render/PsolaWarp.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// 포먼트 기능 이전 엔진(7a0ea0b)과 비트 단위로 같은지: 같은 합성 모음에 이동 곡선 / 박자 편집을 넣어 렌더한 결과의 FNV-1a 해시.
// 아래 상수는 7a0ea0b를 같은 도구로 빌드해 `PsolaGoldenTests --print`로 얻은 값이다 (GCC x86-64 Release).
// 컴파일러/플랫폼이 부동소수점 연산 순서를 바꾸면 달라질 수 있다: 그때는 7a0ea0b로 다시 뽑아 갱신한다.
// 그래서 비트 단위 비교는 GCC x86-64에서만 한다. 그 밖(MSVC, clang, ARM)에서는 해시를 출력하고 통과한다.
// 포먼트 데이터(formantAmount)는 전혀 쓰지 않는다 (nullptr / 1.0).
// 11/6부터 vowelFocus가 기본 켬이므로, renderEdited 비교는 levelMatch·vowelFocus를 명시적으로 끈 경로로 한다 (= 공개판 1.0.1 출력).
namespace
{
int failures = 0;
constexpr int sr = 44100;
constexpr double framePeriod = 0.005, pi = 3.14159265358979323846;
constexpr size_t frames = 321;
constexpr uint64_t goldenShift = 0xcf447e103a253189ULL, goldenWarp = 0x0ace6a12d00e32f4ULL, goldenEdited = 0x81c2b95f93cf9302ULL;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

uint64_t fnv1a(const std::vector<float>& x)
{
    uint64_t h = 1469598103934665603ULL;
    for (const auto v : x)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, 4);
        for (int i = 0; i < 4; ++i) { h ^= (bits >> (8 * i)) & 0xFFu; h *= 1099511628211ULL; }
    }
    return h;
}

void resonate(std::vector<double>& x, double hz, double bw)
{
    const auto r = std::exp(-pi * bw / sr), c = 2.0 * r * std::cos(2.0 * pi * hz / sr);
    double y1 = 0.0, y2 = 0.0;
    for (auto& v : x) { const auto y = v + c * y1 - r * r * y2; y2 = y1; y1 = y; v = y; }
}

// 0.2~0.7초 /a/ (130 Hz), 0.8~1.4초 /i/ (180 Hz), 비브라토 약간. 총 1.6초
struct Source
{
    std::vector<float> x;
    std::vector<double> midi;
    Source() : midi(frames, 0.0)
    {
        std::vector<double> p(static_cast<size_t>(1.6 * sr), 0.0);
        double phase = 1.0;
        for (size_t i = 0; i < p.size(); ++i)
        {
            const auto t = static_cast<double>(i) / sr;
            const auto f0 = (t < 0.75 ? 130.0 : 180.0) * (1.0 + 0.01 * std::sin(2.0 * pi * 5.0 * t));
            const bool voiced = (t >= 0.2 && t < 0.7) || (t >= 0.8 && t < 1.4);
            if (voiced && phase >= 1.0) { p[i] = 1.0; phase -= 1.0; }
            if (voiced) phase += f0 / sr;
        }
        for (const auto hz : { 700.0, 1100.0, 2500.0 }) resonate(p, hz, 90.0);
        x.assign(p.size(), 0.0f);
        double top = 1e-12;
        for (const auto v : p) top = std::max(top, std::abs(v));
        for (size_t i = 0; i < p.size(); ++i) x[i] = static_cast<float>(0.3 * p[i] / top);
        for (size_t f = 0; f < frames; ++f)
        {
            const auto t = static_cast<double>(f) * framePeriod;
            if ((t >= 0.2 && t < 0.7) || (t >= 0.8 && t < 1.4)) midi[f] = 69.0 + 12.0 * std::log2((t < 0.75 ? 130.0 : 180.0) / 440.0);
        }
    }
};

std::vector<double> shiftCurve(const Source& s)
{
    std::vector<double> sh(frames, 0.0);
    for (size_t f = 0; f < frames; ++f)
        if (s.midi[f] > 0.0) sh[f] = -3.0 + 7.0 * std::sin(2.0 * pi * static_cast<double>(f) / 140.0);
    return sh;
}

uint64_t shiftHash()
{
    const Source s;
    const auto sh = shiftCurve(s);
    const PsolaInput in { s.x.data(), s.x.size(), sr, framePeriod, s.midi.data(), sh.data(), frames };
    return fnv1a(psolaShift(in, 0, s.x.size()));
}

uint64_t warpHash()
{
    const Source s;
    const auto sh = shiftCurve(s);
    const auto toSource = [](double t) { return t < 0.8 ? t : 0.8 + (t - 0.8) * 0.7; };
    const auto toDisplay = [](double u) { return u < 0.8 ? u : 0.8 + (u - 0.8) / 0.7; };
    const WarpInput in { s.x.data(), s.x.size(), sr, framePeriod, s.midi.data(), frames, sh.data(), frames, toSource, toDisplay };
    return fnv1a(psolaWarp(in, 0, s.x.size()));
}

// 두 블록: 이동 곡선(curveOffset)이 있는 +3 반음, 시작/끝 타이밍을 옮긴 -2 반음
std::vector<VocalBlock> editedBlocks(const Source& s)
{
    VocalBlock a, b;
    a.beginFrame = 40; a.endFrame = 140; a.startSec = 0.2; a.endSec = 0.7;
    b.beginFrame = 160; b.endFrame = 280; b.startSec = 0.8; b.endSec = 1.4;
    for (auto* blk : { &a, &b })
    {
        blk->baseMidi = static_cast<int>(std::lround(s.midi[blk->beginFrame + 1]));
        blk->pitchOffset = s.midi[blk->beginFrame + 1] - blk->baseMidi;
        blk->tidyAmount = 0.0;
    }
    a.pitchShift = 3.0;
    for (size_t i = 0; i < a.endFrame - a.beginFrame; ++i) a.curveOffset.push_back(0.4f * std::sin(0.2f * static_cast<float>(i)));
    b.pitchShift = -2.0;
    b.startOffsetSec = 0.04;
    b.endOffsetSec = -0.06;
    return { a, b };
}

uint64_t editedHash(bool& ok)
{
    const Source s;
    AudioPlanes audio;
    audio.sampleRate = sr;
    audio.channels = { s.x };
    VocalFeatures f;
    f.timeSec.resize(frames);
    for (size_t i = 0; i < frames; ++i) f.timeSec[i] = static_cast<double>(i) * framePeriod;
    f.midiF0 = s.midi;
    f.rms.assign(frames, 0.5);
    RenderOptions legacy;
    legacy.levelMatch = false;
    legacy.vowelFocus = false;
    const auto out = renderEdited(audio, f, editedBlocks(s), legacy);
    ok = out.has_value() && out->channels.size() == 1;
    return ok ? fnv1a(out->channels[0]) : 0;
}

void checkHash(const char* what, uint64_t got, uint64_t golden)
{
    std::printf("  %s: %016llx (golden %016llx)\n", what, static_cast<unsigned long long>(got), static_cast<unsigned long long>(golden));
#if defined(__GNUC__) && ! defined(__clang__) && defined(__x86_64__)
    check(got == golden, what);
#else
    (void) golden;
    std::printf("[SKIP] %s: bit-exact hashes are only checked with GCC on x86-64\n", what);
#endif
}
} // namespace

int main(int argc, char** argv)
{
    bool ok = false;
    const auto shift = shiftHash(), warp = warpHash(), edited = editedHash(ok);
    if (argc > 1 && std::strcmp(argv[1], "--print") == 0)
    {
        std::printf("constexpr uint64_t goldenShift = 0x%016llxULL, goldenWarp = 0x%016llxULL, goldenEdited = 0x%016llxULL;\n",
                    static_cast<unsigned long long>(shift), static_cast<unsigned long long>(warp), static_cast<unsigned long long>(edited));
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    check(ok, "renderEdited returns audio");
    const auto plain = fnv1a(Source().x);
    check(shift != plain && warp != plain && edited != plain, "the golden renders actually changed the audio");
    checkHash("psolaShift with a shift curve matches the pre-formant engine bit for bit", shift, goldenShift);
    checkHash("psolaWarp with a shift curve and a timing map matches the pre-formant engine bit for bit", warp, goldenWarp);
    checkHash("renderEdited with curves and timing edits matches the pre-formant engine bit for bit", edited, goldenEdited);
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
