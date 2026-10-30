#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include "../Source/Alignment/ReferenceFit.h"

namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}
bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

constexpr double pi = 3.14159265358979323846;
constexpr int sampleRate = 44100;

struct Syllable
{
    double start, length, hz;
    int vowel;          // 모음 종류 (포먼트 조합)
};

// 모음별 포먼트 (Hz)
const double formants[3][2] = { { 700, 1100 }, { 300, 2300 }, { 500, 1500 } };

// 배음에 포먼트 모양을 입힌 모음 + 선택적 호흡 잡음 구간
std::vector<double> synthesize(const std::vector<Syllable>& syllables, const std::vector<std::pair<double, double>>& breaths, double total)
{
    std::vector<double> y(static_cast<size_t>(total * sampleRate), 0.0);
    // 표준 라이브러리 분포는 구현마다 값이 달라 직접 만든다 (플랫폼과 무관하게 같은 잡음)
    unsigned state = 7u;
    const auto uniform = [&state]
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 16777216.0;
    };
    const auto noise = [&uniform](int) { double sum = -6.0; for (int k = 0; k < 12; ++k) sum += uniform(); return sum; };
    for (const auto& s : syllables)
    {
        const auto begin = static_cast<size_t>(s.start * sampleRate);
        const auto end = std::min(y.size(), static_cast<size_t>((s.start + s.length) * sampleRate));
        for (size_t n = begin; n < end; ++n)
        {
            const auto t = static_cast<double>(n - begin) / sampleRate;
            const auto env = std::min({ 1.0, t / 0.02, (s.length - t) / 0.02 });
            double v = 0.0;
            for (int h = 1; h * s.hz < 7000.0; ++h)
            {
                const auto f = h * s.hz;
                double a = 0.01 / h;
                for (const auto& F : formants[s.vowel])
                    a += std::exp(-std::pow((f - F) / 150.0, 2.0));
                v += a * std::sin(2.0 * pi * f * t);
            }
            y[n] += 0.15 * env * v;
        }
    }
    for (const auto& [from, to] : breaths)
    {
        double previous = 0.0;
        for (auto n = static_cast<size_t>(from * sampleRate); n < std::min(y.size(), static_cast<size_t>(to * sampleRate)); ++n)
        {
            const auto w = noise(0);
            y[n] += 0.012 * (w - previous);    // 1차 고역 통과: 고역이 많은 잡음
            previous = w;
        }
    }
    for (auto& v : y)
        v += 1e-4 * noise(0);
    return y;
}

struct Track
{
    std::vector<double> audio;
    VocalFeatures features;
    SpectralFrames frames;
    std::vector<SectionKind> kinds;
};

Track analyse(std::vector<double> audio)
{
    Track t;
    t.audio = std::move(audio);
    t.features = *extractVocalFeatures(t.audio, sampleRate);
    t.frames = computeSpectralFrames(t.audio, sampleRate);
    t.kinds = classifyFrames(t.frames, t.features);
    return t;
}

size_t countKind(const std::vector<Section>& sections, SectionKind kind)
{
    return static_cast<size_t>(std::count_if(sections.begin(), sections.end(), [&](const Section& s) { return s.kind == kind; }));
}
} // namespace

int main()
{
    // --- TimeMap
    {
        const TimeMap map({ 1.0, 2.0, 3.0 }, { 1.5, 2.5, 4.5 });
        check(near(map.map(1.5), 2.0) && near(map.map(2.5), 3.5), "map interpolates linearly");
        check(near(map.map(0.0), 0.5) && near(map.map(4.0), 5.5), "map keeps the edge offset outside the range");
        check(TimeMap().map(2.0) == 2.0, "empty map is the identity");
    }

    // --- 합성 보컬: 같은 가사를 레퍼런스(기준)와 타깃(어긋남, 4반음 위 화음)으로
    const std::vector<int> lyric { 0, 1, 2, 0, 2, 1, 0, 1 };
    const std::vector<double> delay { 0.08, -0.05, 0.12, 0.0, -0.09, 0.15, 0.05, -0.03 };
    std::vector<Syllable> refSyllables, tgtSyllables;
    std::vector<std::pair<double, double>> refBreaths, tgtBreaths;
    for (size_t n = 0; n < lyric.size(); ++n)
    {
        const auto start = 0.6 + 0.8 * static_cast<double>(n);
        refSyllables.push_back({ start, 0.55, 220.0 * std::pow(2.0, (n % 3) * 2.0 / 12.0), lyric[n] });
        tgtSyllables.push_back({ start + delay[n], 0.55, 220.0 * std::pow(2.0, ((n % 3) * 2.0 + 4.0) / 12.0), lyric[n] });
    }
    refBreaths.push_back({ 3.58, 3.72 });
    tgtBreaths.push_back({ 3.58, 3.68 });
    const auto total = 7.6;
    const auto ref = analyse(synthesize(refSyllables, refBreaths, total));
    const auto tgt = analyse(synthesize(tgtSyllables, tgtBreaths, total));

    // --- 구간 분류
    {
        const auto sections = toSections(ref.kinds, ref.frames);
        check(countKind(sections, SectionKind::Voiced) >= 7, "classifier finds the sung syllables as voiced");
        check(countKind(sections, SectionKind::Breath) >= 1, "classifier finds the breath");
        const auto breath = std::find_if(sections.begin(), sections.end(), [](const Section& s) { return s.kind == SectionKind::Breath; });
        check(breath != sections.end() && breath->startSec > 3.45 && breath->endSec < 3.85, "breath section is at the right place");
    }

    // --- 정렬
    const TrackFrames refFrames { &ref.frames, &ref.kinds, 0.0 };
    const TrackFrames tgtFrames { &tgt.frames, &tgt.kinds, 0.0 };
    const auto aligned = alignToReference(refFrames, tgtFrames);
    check(aligned.ok && ! aligned.targetToReference.empty(), "alignment succeeds");
    {
        AlignOptions reporting;
        int calls = 0;
        reporting.progress = [&calls](double fraction) { ++calls; return fraction >= 0.0 && fraction <= 1.0; };
        check(alignToReference(refFrames, tgtFrames, reporting).ok && calls > 0, "alignment reports progress");
        AlignOptions cancelling;
        cancelling.progress = [](double) { return false; };
        const auto cancelled = alignToReference(refFrames, tgtFrames, cancelling);
        check(! cancelled.ok && cancelled.cancelled, "alignment can be cancelled");
    }
    {
        double worst = 0.0;
        for (size_t n = 0; n < lyric.size(); ++n)
        {
            const auto mappedStart = aligned.targetToReference.map(tgtSyllables[n].start + 0.05);
            worst = std::max(worst, std::abs(mappedStart - (refSyllables[n].start + 0.05)));
        }
        std::printf("worst syllable error: %.1f ms\n", worst * 1000.0);
        check(worst < 0.04, "every syllable lands within 40 ms of the reference");
        const auto breathMapped = aligned.targetToReference.map(tgtBreaths[0].first + 0.02);
        check(std::abs(breathMapped - (refBreaths[0].first + 0.02)) < 0.05, "the breath is aligned too");
    }

    // --- 격자에 맞지 않는 어긋남 (7 ms 단위가 아닌 값)
    {
        std::vector<Syllable> shifted = tgtSyllables;
        const double odd[] = { 0.083, -0.047, 0.121, 0.017, -0.092, 0.138, 0.056, -0.031 };
        for (size_t n = 0; n < shifted.size(); ++n)
            shifted[n].start = refSyllables[n].start + odd[n];
        const auto t2 = analyse(synthesize(shifted, {}, total));
        const TrackFrames second { &t2.frames, &t2.kinds, 0.0 };
        const auto r2 = alignToReference(refFrames, second);
        double worst = 0.0;
        for (size_t n = 0; n < shifted.size(); ++n)
            worst = std::max(worst, std::abs(r2.targetToReference.map(shifted[n].start + 0.05) - (refSyllables[n].start + 0.05)));
        std::printf("worst off-grid error: %.1f ms\n", worst * 1000.0);
        check(r2.ok && worst < 0.03, "off-grid offsets are recovered within 30 ms");

        // 클립 위치(offset)가 다른 트랙: 곡 시간으로 정렬
        const TrackFrames moved { &t2.frames, &t2.kinds, 0.25 };
        const TrackFrames refMoved { &ref.frames, &ref.kinds, 0.25 };
        const auto r3 = alignToReference(refMoved, moved);
        check(r3.ok && std::abs(r3.targetToReference.map(shifted[2].start + 0.25 + 0.05) - (refSyllables[2].start + 0.25 + 0.05)) < 0.03, "clip offsets are handled in song time");
    }

    // --- 블롭에 적용 (타이밍)
    {
        std::vector<VocalBlock> blocks;
        for (size_t n = 0; n < lyric.size(); ++n)
        {
            VocalBlock b;
            b.id = static_cast<int>(n);
            b.startSec = tgtSyllables[n].start;
            b.endSec = tgtSyllables[n].start + tgtSyllables[n].length;
            b.baseMidi = 60;
            blocks.push_back(b);
        }
        TrackRoles roles;
        roles.timing = true;
        const auto updates = fitToReference(blocks, 0.0, aligned.targetToReference, {}, 0.0, roles);
        double worst = 0.0;
        for (size_t n = 0; n < lyric.size(); ++n)
            worst = std::max(worst, std::abs(updates[n].block.displayStartSec() - refSyllables[n].start));
        check(worst < 0.04, "fitted blocks start where the reference syllables start");
        bool ordered = true;
        for (size_t n = 1; n < updates.size(); ++n)
            ordered = ordered && updates[n].block.displayStartSec() >= updates[n - 1].block.displayEndSec() - 1e-9;
        check(ordered, "fitted blocks stay in order without overlap");

        roles.timingAmount = 0.5;
        const auto half = fitToReference(blocks, 0.0, aligned.targetToReference, {}, 0.0, roles);
        check(near(half[2].block.displayStartSec(), 0.5 * (blocks[2].startSec + updates[2].block.displayStartSec()), 1e-6), "timing amount blends between original and fitted");
        roles.timingAmount = 0.0;
        check(near(fitToReference(blocks, 0.0, aligned.targetToReference, {}, 0.0, roles)[2].block.displayStartSec(), blocks[2].startSec), "amount 0 changes nothing");
    }

    // --- 음정 역할
    {
        check(near(snappedPitchShift(64.3, 60.0, PitchRelation::Unison, 1.0), -4.3), "unison pulls to the reference note");
        check(near(snappedPitchShift(72.3, 60.0, PitchRelation::Unison, 1.0), -0.3), "unison keeps the octave");
        check(near(snappedPitchShift(64.3, 60.0, PitchRelation::Harmony, 1.0), -0.3), "harmony snaps the interval to a whole semitone");
        check(near(snappedPitchShift(64.3, 60.0, PitchRelation::Harmony, 0.5), -0.15), "pitch amount scales the correction");

        VocalBlock ref1, tgt1;
        ref1.startSec = 1.0; ref1.endSec = 2.0; ref1.baseMidi = 60;
        tgt1.startSec = 1.1; tgt1.endSec = 2.1; tgt1.baseMidi = 64; tgt1.pitchOffset = 0.3;
        check(near(referencePitchOver({ ref1 }, 0.0, 1.2, 1.8), 60.0) && referencePitchOver({ ref1 }, 0.0, 3.0, 4.0) < 0.0, "reference pitch lookup");
        TrackRoles roles;
        roles.pitch = true;
        roles.relation = PitchRelation::Harmony;
        roles.smoothAmount = 0.8;
        const auto fitted = fitToReference({ tgt1 }, 0.0, TimeMap({ 0.0, 10.0 }, { -0.1, 9.9 }), { ref1 }, 0.0, roles);
        check(near(fitted[0].block.pitchShift, -0.3) && near(fitted[0].block.transitionAfter, 0.8), "harmony fit sets pitch shift and smoothness");
        check(near(fitted[0].block.displayStartSec(), 1.1), "pitch-only role leaves the timing alone");
    }

    // --- 레퍼런스 옥타브 오분석: 흐름(음정 이동)만 따른다
    {
        const auto block = [](double start, double end, double midi)
        {
            VocalBlock b;
            b.startSec = start; b.endSec = end;
            b.baseMidi = static_cast<int>(std::lround(midi));
            b.pitchOffset = midi - b.baseMidi;
            return b;
        };
        check(near(foldedToOctaveOf(72.2, 60.0), 60.2) && near(foldedToOctaveOf(69.0, 60.0), 69.0), "fold only about an octave or more");

        // 한 블롭에 바른 레퍼런스(60)와 한 옥타브 틀린 레퍼런스(72)가 반씩 겹침: 평균 66으로 끌려가면 안 된다
        TrackRoles roles;
        roles.pitch = true;
        const auto mixed = fitToReference({ block(1.0, 2.0, 60.2) }, 0.0, TimeMap(), { block(1.0, 1.5, 60.0), block(1.5, 2.0, 72.0) }, 0.0, roles);
        std::printf("  mixed-octave shift %.2f\n", mixed[0].block.pitchShift);
        check(near(mixed[0].block.pitchShift, -0.2), "octave error inside the reference does not pull to a tritone");

        // 화음(3도 위) 타깃, 레퍼런스 가운데 블록이 한 옥타브 위로 잘못 잡힘 -> 간격 4를 유지
        roles.relation = PitchRelation::Harmony;
        const std::vector<VocalBlock> ref { block(0.0, 1.0, 60.0), block(1.0, 2.0, 74.0), block(2.0, 3.0, 64.0) };
        const std::vector<VocalBlock> tgt { block(0.0, 1.0, 64.1), block(1.0, 2.0, 66.1), block(2.0, 3.0, 68.1) };
        const auto harmony = fitToReference(tgt, 0.0, TimeMap(), ref, 0.0, roles);
        bool keeps = true;
        for (const auto& u : harmony)
            keeps = keeps && near(u.block.pitchShift, -0.1);
        check(keeps, "harmony follows the reference contour through an octave error");
    }

    return failures == 0 ? 0 : 1;
}
