#include <cmath>
#include <cstdio>
#include <vector>
#include "../Source/Alignment/GuidedTiming.h"
#include "../Source/Alignment/MixReference.h"
#include "../Source/Alignment/ReferenceFit.h"

namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

constexpr double pi = 3.14159265358979323846;
constexpr int rate = 44100;
double hzOf(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

// 가운데(좌우 같음)에 보컬 음, 왼쪽에만 다른 음의 기타를 같은 크기로 섞은 2초 믹스
struct Mix { std::vector<float> left, right; };
Mix makeMix(double vocalMidi, double guitarMidi)
{
    Mix m;
    const auto n = static_cast<size_t>(2.0 * rate);
    m.left.resize(n);
    m.right.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        const auto t = static_cast<double>(i) / rate;
        double vocal = 0.0, guitar = 0.0;
        for (int h = 1; h <= 8; ++h)
        {
            vocal += std::sin(2.0 * pi * h * hzOf(vocalMidi) * t) / h;
            guitar += std::sin(2.0 * pi * h * hzOf(guitarMidi) * t) / h;
        }
        m.left[i] = static_cast<float>(0.2 * vocal + 0.2 * guitar);
        m.right[i] = static_cast<float>(0.2 * vocal);
    }
    return m;
}

MelodyTrack extract(const Mix& m, double guideMidi)
{
    const StereoReader read = [&m](int64_t start, int count, float* l, float* r)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto k = start + i;
            const auto inside = k >= 0 && k < static_cast<int64_t>(m.left.size());
            l[i] = inside ? m.left[static_cast<size_t>(k)] : 0.0f;
            r[i] = inside ? m.right[static_cast<size_t>(k)] : 0.0f;
        }
    };
    return extractGuidedMelody(static_cast<int64_t>(m.left.size()), rate, read, [guideMidi](double) { return guideMidi; });
}

VocalBlock voicedBlock(double start, double end, double midi)
{
    VocalBlock b;
    b.startSec = start;
    b.endSec = end;
    b.baseMidi = static_cast<int>(std::lround(midi));
    b.pitchOffset = midi - b.baseMidi;
    return b;
}
} // namespace

int main()
{
    // --- 추출: 따라 부른 음이 1반음 낮을 때, 왼쪽 기타(2반음 위)가 아니라 가운데 보컬 음을 찾는다
    const auto mix = makeMix(57.0, 59.0);
    {
        const auto melody = extract(mix, 56.0);
        const auto found = melodyPitchOver(melody, 0.5, 1.5);
        check(std::abs(found - 57.0) < 0.3, "finds the centred vocal note, not the side guitar");
    }
    // 제 음으로 불렀으면 근거가 없다 -> 옮기지 않는다
    {
        const auto melody = extract(mix, 57.0);
        check(melodyPitchOver(melody, 0.5, 1.5) < 0.0, "no evidence when the singer is already on the note");
    }

    // --- 근거 조건
    {
        MelodyTrack m;
        m.midi.assign(100, 60.0f);
        m.confidence.assign(100, 1.0f);
        m.evidence.assign(100, 0.5f);
        check(std::abs(melodyPitchOver(m, 0.2, 0.6) - 60.0) < 1e-6, "median of frames with evidence");
        check(melodyPitchOver(m, 0.2, 0.3) < 0.0, "short blocks are left alone");
        for (size_t i = 20; i < 50; ++i)
            m.evidence[i] = 0.0f;
        check(melodyPitchOver(m, 0.2, 0.6) < 0.0, "too few frames with evidence -> no reference");
    }

    // --- 믹스 판별: 쉬는 구간이 있으면 보컬 트랙
    {
        std::vector<double> vocal(1000, 0.5), mixed(1000, 0.5);
        for (size_t i = 0; i < 250; ++i)
            vocal[i * 4] = 0.0;
        check(! looksLikeMix(vocal), "a vocal track with pauses is not a mix");
        check(looksLikeMix(mixed), "continuous sound is a mix");
    }

    // --- 박자: 원곡 가운데에서 부른 음이 0.25초 늦게 나오면 그만큼 늦춘다 (왼쪽의 기타는 무시)
    {
        const double lag = 0.25, seconds = 12.0;
        const auto noteAt = [](double t) { return 60.0 + static_cast<double>(static_cast<int>(t / 0.4) % 5); };
        const auto n = static_cast<size_t>(seconds * rate);
        std::vector<float> left(n), right(n);
        double vocalPhase = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = static_cast<double>(i) / rate;
            const auto voice = t >= lag ? noteAt(t - lag) : 0.0;
            vocalPhase += 2.0 * pi * hzOf(voice) / rate;
            double v = 0.0;
            for (int h = 1; h <= 5 && voice > 0.0; ++h)
                v += 0.3 / h * std::sin(h * vocalPhase);
            const auto guitar = 0.3 * std::sin(2.0 * pi * hzOf(noteAt(t + 0.2) + 3.0) * t);
            left[i] = static_cast<float>(v + guitar);
            right[i] = static_cast<float>(v);
        }
        VocalFeatures sung;
        for (size_t k = 0; k < static_cast<size_t>(seconds / 0.005); ++k)
        {
            sung.timeSec.push_back(0.005 * static_cast<double>(k));
            sung.midiF0.push_back(noteAt(0.005 * static_cast<double>(k)));
        }
        const StereoReader read = [&](int64_t start, int count, float* l, float* r)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto at = start + i;
                const bool inside = at >= 0 && at < static_cast<int64_t>(n);
                l[i] = inside ? left[static_cast<size_t>(at)] : 0.0f;
                r[i] = inside ? right[static_cast<size_t>(at)] : 0.0f;
            }
        };
        const auto map = guidedTimeMap(static_cast<int64_t>(n), rate, read, sung, 0.0, 0.0);
        check(! map.empty() && std::abs(map.map(6.0) - 6.0 - lag) < 0.02, "guided timing finds the sung notes' lag in the mix");
    }

    // --- 맞추기: 근거 있는 블록만, 2반음 넘게 다르면 안 옮김
    {
        MelodyTrack m;
        m.midi.assign(300, 0.0f);
        m.confidence.assign(300, 1.0f);
        m.evidence.assign(300, 0.6f);
        for (size_t i = 0; i < 100; ++i) m.midi[i] = 60.0f;        // 0..1초: 60
        for (size_t i = 100; i < 200; ++i) m.midi[i] = 65.0f;      // 1..2초: 65 (부른 음과 너무 다름)
        for (size_t i = 200; i < 300; ++i) { m.midi[i] = 62.0f; m.evidence[i] = 0.0f; }   // 근거 없음
        const std::vector<VocalBlock> blocks { voicedBlock(0.2, 0.8, 59.4), voicedBlock(1.2, 1.8, 62.0), voicedBlock(2.2, 2.8, 61.0) };
        TrackRoles roles;
        roles.pitch = true;
        const auto up = fitToReference(blocks, 0.0, TimeMap(), {}, 0.0, roles, &m);
        check(std::abs(up[0].block.pitchShift - 0.6) < 1e-6, "moves a block onto the melody");
        check(up[1].block.pitchShift == 0.0, "leaves a block that differs by more than 2 semitones");
        check(up[2].block.pitchShift == 0.0, "leaves a block without evidence");
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
