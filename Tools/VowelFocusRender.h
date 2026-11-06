// vt_vowel_focus의 렌더와 측정: 현재(자음 보호만) vs 모음 중심 (vowel_focus.cpp 안에서만 포함)
#pragma once
#include "VowelFocusTake.h"

namespace vf
{
struct Render { std::vector<double> shift; std::vector<float> y; };

// 유성 프레임에 일정 이동 -> 자음 보호 -> (focus면) 모음 가중치. 렌더는 PSOLA만, 포먼트 양 1 (포먼트 이동 없음), 음량 보정 없음
inline Render render(const Take& t, double semitones, bool focus)
{
    std::vector<double> shift(t.midi.size(), 0.0);
    for (size_t i = 0; i < shift.size(); ++i) shift[i] = t.midi[i] > 0.0 ? semitones : 0.0;
    shift = shieldedShift(t.x.data(), t.x.size(), t.sr, framePeriod, shift);
    if (focus) applyVowelFocus(shift, t.x.data(), t.x.size(), t.sr, framePeriod, t.midi.data(), t.midi.size(), 0, t.midi.size());
    const PsolaInput in{ t.x.data(), t.x.size(), t.sr, framePeriod, t.midi.data(), shift.data(), shift.size(), nullptr, false };
    return { shift, psolaShift(in, 0, t.x.size()) };
}

struct Metrics { double within20 = 0, withinApplied = 0, lowShare = 0, lowShift = 0, hfSpike = 0, levelDb = 0; size_t strong = 0; };

// w >= 0.8 프레임의 F0 정확도 (Harvest on 출력 vs 원본 + 이동), w < 0.5 유성 프레임 비율과 그 프레임의 평균 |이동|, HF 스파이크, 유성 RMS 변화
inline Metrics measureRender(const Take& t, const Render& r, double semitones)
{
    const auto mo = harvestMidi(r.y, t.sr, 40.0);
    Metrics m;
    size_t good = 0, goodApplied = 0, voiced = 0, low = 0;
    double lowSum = 0.0;
    for (size_t i = 0; i < t.midi.size() && i < mo.size(); ++i)
    {
        if (t.midi[i] <= 0.0) continue;
        ++voiced;
        if (t.w[i] < 0.5) { ++low; lowSum += std::abs(r.shift[i]); }
        if (t.w[i] < 0.8) continue;
        ++m.strong;
        good += mo[i] > 0.0 && std::abs(mo[i] - t.midi[i] - semitones) * 100.0 <= 20.0;
        goodApplied += mo[i] > 0.0 && std::abs(mo[i] - t.midi[i] - r.shift[i]) * 100.0 <= 20.0;
    }
    m.within20 = m.strong ? 100.0 * static_cast<double>(good) / static_cast<double>(m.strong) : 0.0;
    m.withinApplied = m.strong ? 100.0 * static_cast<double>(goodApplied) / static_cast<double>(m.strong) : 0.0;
    m.lowShare = voiced ? 100.0 * static_cast<double>(low) / static_cast<double>(voiced) : 0.0;
    m.lowShift = low ? lowSum / static_cast<double>(low) : 0.0;
    m.hfSpike = hfSpikeDb(t.x, r.y, t.midi, t.sr);
    m.levelDb = voicedLevelDb(t.x, r.y, t.midi, t.sr);
    return m;
}

// 청취 파일: 원본, 0.5 s 무음, 현재, 0.5 s 무음, 모음 중심. 레벨 맞춤 없음, 클립될 때만 전체를 줄인다 (줄였으면 true)
inline bool writeListen(const Take& t, double semitones, const Render& cur, const Render& foc, const std::string& path)
{
    const std::vector<float> gap(static_cast<size_t>(0.5 * t.sr), 0.0f);
    std::vector<float> all(t.x);
    for (const auto* part : { &gap, &cur.y, &gap, &foc.y }) all.insert(all.end(), part->begin(), part->end());
    float peak = 0.0f;
    for (const auto v : all) peak = std::max(peak, std::abs(v));
    const auto scale = peak > 0.999f ? 0.999f / peak : 1.0f;
    for (auto& v : all) v *= scale;
    (void) semitones;
    writeWavMono16(path.c_str(), all, t.sr);
    return scale < 1.0f;
}
} // namespace vf
