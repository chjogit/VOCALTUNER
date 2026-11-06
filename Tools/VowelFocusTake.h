// vt_vowel_focus의 한 테이크 분석: 가중치 CSV, 낮은 w 구간 wav, 이동 렌더 (vowel_focus.cpp 안에서만 포함)
#pragma once
#include "../Source/Render/ConsonantShield.h"
#include "../Source/Render/Psola.h"
#include "../Source/Render/VowelWeight.h"
#include "HfSpike.h"
#include "WavIO.h"
#include <cstdio>
#include <string>

namespace vf
{
using namespace voicemetrics;

struct Take
{
    std::string name;
    std::vector<float> x;
    std::vector<double> midi, w;
    std::vector<VowelFeatures> feat;
    int sr = 48000;
};

struct Segment { size_t begin, end; };   // 프레임 [begin, end)

inline bool load(const char* path, const char* name, double from, double to, Take& t)
{
    std::vector<float> all;
    if (! readWavMono(path, all, t.sr)) return false;
    t.name = name;
    const auto b = static_cast<size_t>(from * t.sr), e = std::min(all.size(), static_cast<size_t>(to * t.sr));
    t.x.assign(all.begin() + static_cast<long>(b), all.begin() + static_cast<long>(e));
    t.midi = harvestMidi(t.x, t.sr, 71.0);
    t.w = vowelWeights(t.x.data(), t.x.size(), t.sr, framePeriod, t.midi.data(), t.midi.size(), 0, t.midi.size());
    t.feat = vowelFeatures(t.x.data(), t.x.size(), t.sr, framePeriod, t.midi.data(), t.midi.size(), 0, t.midi.size());
    return true;
}

inline void writeCsv(const Take& t, const std::string& path)
{
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (! f) return;
    std::fprintf(f, "frame,time,midi,w,raw,periodicity,lowShare,relLevelDb,flatness,fricative\n");
    for (size_t i = 0; i < t.w.size(); ++i)
    {
        const auto& v = t.feat[i];
        std::fprintf(f, "%zu,%.3f,%.2f,%.3f,%.3f,%.3f,%.3f,%.1f,%.4f,%d\n", i, (static_cast<double>(i) + 1.0) * framePeriod, t.midi[i], t.w[i],
                     rawVowelWeight(v), v.periodicity, v.lowShare, v.relLevelDb, v.flatness, v.fricative ? 1 : 0);
    }
    std::fclose(f);
}

// 유성 프레임에서 w < 0.5 가 minFrames 이상 이어진 구간
inline std::vector<Segment> lowSegments(const Take& t, size_t minFrames)
{
    std::vector<Segment> out;
    for (size_t i = 0; i < t.w.size();)
    {
        if (t.midi[i] <= 0.0 || t.w[i] >= 0.5) { ++i; continue; }
        auto j = i;
        while (j < t.w.size() && t.midi[j] > 0.0 && t.w[j] < 0.5) ++j;
        if (j - i >= minFrames) out.push_back({ i, j });
        i = j;
    }
    return out;
}

inline double meanOf(const Take& t, const Segment& s, double VowelFeatures::*field)
{
    double sum = 0.0;
    for (auto i = s.begin; i < s.end; ++i) sum += t.feat[i].*field;
    return sum / static_cast<double>(s.end - s.begin);
}

inline double meanW(const Take& t, const Segment& s)
{
    double sum = 0.0;
    for (auto i = s.begin; i < s.end; ++i) sum += t.w[i];
    return sum / static_cast<double>(s.end - s.begin);
}

// 구간마다 앞뒤 100 ms를 붙인 wav와 index 한 줄. 구간 시간은 프레임 중심 기준
inline void exportSegments(const Take& t, const std::vector<Segment>& segs, const std::string& dir, std::FILE* index)
{
    for (size_t k = 0; k < segs.size(); ++k)
    {
        const auto& s = segs[k];
        const auto a = static_cast<size_t>(static_cast<double>(s.begin) * framePeriod * t.sr), b = static_cast<size_t>((static_cast<double>(s.end) + 1.0) * framePeriod * t.sr);
        const auto ctx = static_cast<size_t>(0.1 * t.sr), from = a > ctx ? a - ctx : 0, to = std::min(t.x.size(), b + ctx);
        char name[64];
        std::snprintf(name, sizeof name, "%s_%03zu_%.2f-%.2fs.wav", t.name.c_str(), k + 1, static_cast<double>(a) / t.sr, static_cast<double>(b) / t.sr);
        writeWavMono16((dir + "/" + name).c_str(), std::vector<float>(t.x.begin() + static_cast<long>(from), t.x.begin() + static_cast<long>(to)), t.sr);
        std::fprintf(index, "%s  %.3f-%.3f s (%.0f ms)  w=%.2f periodicity=%.2f lowShare=%.2f relLevel=%.1f dB flatness=%.3f  %s\n", t.name.c_str(),
                     static_cast<double>(a) / t.sr, static_cast<double>(b) / t.sr, 1000.0 * static_cast<double>(b - a) / t.sr, meanW(t, s),
                     meanOf(t, s, &VowelFeatures::periodicity), meanOf(t, s, &VowelFeatures::lowShare), meanOf(t, s, &VowelFeatures::relLevelDb),
                     meanOf(t, s, &VowelFeatures::flatness), name);
    }
}
} // namespace vf
