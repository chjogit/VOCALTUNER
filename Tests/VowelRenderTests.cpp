#include "VowelTestScene.h"
#include "../Source/Render/EditRenderer.h"
#include <cstdio>

// vowelFocus의 렌더 경로 시험 (psola 경로와 warp 경로)
using namespace vwtest;

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

struct RenderFixture
{
    AudioPlanes audio;
    VocalFeatures f;
    VocalBlock b;
    explicit RenderFixture(const Scene& s, bool timing = false)
    {
        audio.sampleRate = sr;
        audio.channels = { s.x };
        f.timeSec.resize(s.midi.size());
        for (size_t i = 0; i < f.timeSec.size(); ++i) f.timeSec[i] = static_cast<double>(i) * period;
        f.midiF0 = s.midi;
        f.rms.assign(s.midi.size(), 0.5);
        const auto first = static_cast<size_t>(std::find_if(s.midi.begin(), s.midi.end(), [](double m) { return m > 0.0; }) - s.midi.begin());
        const auto last = static_cast<size_t>(std::find_if(s.midi.rbegin(), s.midi.rend(), [](double m) { return m > 0.0; }) - s.midi.rbegin());
        b.beginFrame = first; b.endFrame = s.midi.size() - last; b.startSec = static_cast<double>(first) * period; b.endSec = static_cast<double>(b.endFrame) * period;
        b.baseMidi = static_cast<int>(std::lround(s.midi[first + 1])); b.pitchOffset = s.midi[first + 1] - b.baseMidi; b.tidyAmount = 0.0; b.pitchShift = 3.0;
        if (timing) { b.startOffsetSec = 0.04; b.endOffsetSec = -0.06; }   // 박자 편집이 있으면 PsolaWarp 경로
    }
};

double rmsDiff(const std::vector<float>& a, const std::vector<float>& c, double from, double to)
{
    double e = 0.0;
    for (auto i = samplesOf(from); i < samplesOf(to); ++i) e += (static_cast<double>(a[i]) - c[i]) * (a[i] - c[i]);
    return std::sqrt(e / static_cast<double>(samplesOf(to) - samplesOf(from)));
}

Scene vowelThenNoise()
{
    Scene s;
    s.append(std::vector<float>(samplesOf(0.3), 0.0f), false);   // 앞뒤 무음: 유성이 파일 끝에 붙지 않게
    s.append(vowel(0.6));
    s.append(pinkNoise(0.4, 0.1));   // 흰 잡음은 이미 자음 보호가 0으로 만든다
    s.append(std::vector<float>(samplesOf(0.3), 0.0f), false);
    return s;
}

// 같은 입력으로 vowelFocus 꺼짐/켜짐 렌더 (psola 경로와 warp 경로). 꺼짐은 기본과 비트까지 같고,
// 켜면 변화는 숨소리(w 낮은) 구간에만 있고 모음 가운데는 꺼짐 렌더와 같다
void testRenderFlag(bool timing)
{
    const auto s = vowelThenNoise();
    const RenderFixture fx(s, timing);
    RenderOptions off, on;
    off.vowelFocus = false;
    on.vowelFocus = true;
    const auto d = renderEdited(fx.audio, fx.f, { fx.b }), o = renderEdited(fx.audio, fx.f, { fx.b }, off), v = renderEdited(fx.audio, fx.f, { fx.b }, on);
    const char* path = timing ? "warp path" : "psola path";
    check(d && o && v && d->channels[0] == o->channels[0], timing ? "warp path: vowelFocus off is bit-identical to the default" : "psola path: vowelFocus off is bit-identical to the default");
    if (! (o && v)) return;
    const auto core = rmsDiff(o->channels[0], v->channels[0], 0.45, 0.75), noise = rmsDiff(o->channels[0], v->channels[0], 1.05, 1.2);
    std::printf("  %s: on-vs-off RMS difference: vowel core %.2g, noise %.2g\n", path, core, noise);
    check(core < 1e-6, timing ? "warp path: vowelFocus leaves the vowel core equal to the off render" : "psola path: vowelFocus leaves the vowel core equal to the off render");
    check(noise > 1e-3, timing ? "warp path: vowelFocus changes the noise region" : "psola path: vowelFocus changes the noise region");
}

// renderEditedRange(vowelFocus 켬)의 표본은 전체 렌더와 같다 (RenderTests와 같은 허용 오차 1e-4)
void testRangeRender(bool timing)
{
    const auto s = vowelThenNoise();
    const RenderFixture fx(s, timing);
    RenderOptions on;
    on.vowelFocus = true;
    const auto full = renderEdited(fx.audio, fx.f, { fx.b }, on);
    const auto seg = renderEditedRange(fx.audio, fx.f, { fx.b }, samplesOf(0.6), samplesOf(1.15), on);
    double worst = 1e9;
    if (full && seg && seg->audio.length() > 0)
    {
        worst = 0.0;
        for (size_t i = 0; i < seg->audio.length(); ++i)
            worst = std::max(worst, static_cast<double>(std::abs(seg->audio.channels[0][i] - full->channels[0][seg->startSample + i])));
    }
    std::printf("  %s: range vs full worst difference %.2e\n", timing ? "warp path" : "psola path", worst);
    check(worst < 1e-4, timing ? "warp path: renderEditedRange with vowelFocus equals the full render" : "psola path: renderEditedRange with vowelFocus equals the full render");
}
} // namespace

int main()
{
    for (const auto timing : { false, true })
    {
        testRenderFlag(timing);
        testRangeRender(timing);
    }
    std::printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
