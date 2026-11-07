// 문장 중간에 들어오는 화음의 박자 찾기 평가 (테스트 아님, 정답을 만들어서 아는 재현 가능한 측정)
//   vt_phrase_timing_eval lead.wav startSec durationSec outDir
// 실제 보컬(lead)을 기준으로 두고, 프레이즈마다 가운데부터 끝까지만 잘라 interval 반음 옮긴 뒤(PSOLA) 알려진 시차 d만큼 늦춰 놓은
// 따라가는 트랙을 만든다. 정답: 그 조각의 타깃 -> 기준 이동량 = -d. 현재 방식(GuidedTiming)과 프레이즈 방식(PhraseTiming)을 비교한다.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include "../Source/Alignment/GuidedTiming.h"
#include "../Source/Alignment/PhraseTiming.h"
#include "../Source/Alignment/BandAlign.h"
#include "../Source/Render/Psola.h"
#include "WavIO.h"

namespace
{
struct Segment { double begin, end, delay; };   // 기준 시간의 잘라 낸 구간, 늦춘 양

std::vector<Segment> segmentsOf(const VocalFeatures& f, double entryFraction)
{
    static const double delays[] = { 0.15, -0.12, 0.25, -0.2, 0.08, -0.06, 0.3, -0.25 };
    std::vector<std::pair<double, double>> runs;
    const auto period = f.framePeriodMs / 1000.0;
    for (size_t i = 0; i < f.size(); ++i)
    {
        if (f.midiF0[i] <= 0.0) continue;
        const auto t = static_cast<double>(i) * period;
        if (! runs.empty() && t - runs.back().second <= 0.25) runs.back().second = t;
        else runs.push_back({ t, t });
    }
    std::vector<Segment> out;
    for (const auto& r : runs)
        if (r.second - r.first >= 1.2 && r.first > 0.5)
        {
            const auto entry = r.first + entryFraction * (r.second - r.first);
            out.push_back({ entry, r.second + 0.05, delays[out.size() % 8] });
        }
    return out;
}

std::vector<float> buildTarget(const std::vector<float>& lead, int sr, const VocalFeatures& f, const std::vector<Segment>& segs,
                               double interval, const std::vector<double>* placeShift)
{
    std::vector<double> shift(f.size(), 0.0);
    for (const auto& s : segs)
        for (size_t i = 0; i < f.size(); ++i)
        {
            const auto t = static_cast<double>(i) * f.framePeriodMs / 1000.0;
            if (t >= s.begin - 0.05 && t <= s.end + 0.05 && f.midiF0[i] > 0.0) shift[i] = interval;
        }
    std::vector<float> moved = lead;
    if (interval != 0.0)
    {
        // EVAL_HARD: 포먼트도 음정과 같이 움직여(다른 목소리 색) 고역을 깎고, 음량 -6 dB, 잡음 -50 dBFS를 더한다
        const bool hard = std::getenv("EVAL_HARD") != nullptr;
        const std::vector<double> formant(f.size(), 0.0);
        const PsolaInput in { lead.data(), lead.size(), sr, f.framePeriodMs / 1000.0, f.midiF0.data(), shift.data(), f.size(),
                              hard ? formant.data() : nullptr };
        moved = psolaShift(in, 0, lead.size());
    }
    if (std::getenv("EVAL_HARD") != nullptr)
    {
        uint32_t seed = 12345u;
        float low = 0.0f;
        const auto a = static_cast<float>(std::exp(-2.0 * 3.14159265358979 * 3000.0 / sr));
        for (auto& v : moved)
        {
            low = (1.0f - a) * v + a * low;
            seed = seed * 1664525u + 1013904223u;
            v = 0.5f * (0.4f * v + 0.6f * low) + 0.00316f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
        }
    }
    std::vector<float> out(lead.size(), 0.0f);
    const auto fade = static_cast<size_t>(0.005 * sr);
    for (size_t n = 0; n < segs.size(); ++n)
    {
        const auto& s = segs[n];
        const auto a = static_cast<size_t>(s.begin * sr), b = std::min(lead.size(), static_cast<size_t>(s.end * sr));
        const auto place = s.delay + (placeShift != nullptr ? (*placeShift)[n] : 0.0);
        for (size_t i = a; i < b; ++i)
        {
            const auto j = static_cast<long>(i) + std::lround(place * sr);
            if (j < 0 || j >= static_cast<long>(out.size())) continue;
            const auto g = std::min({ 1.0f, static_cast<float>(i - a) / fade, static_cast<float>(b - i) / fade });
            out[static_cast<size_t>(j)] += g * moved[i];
        }
    }
    return out;
}

std::vector<double> estimates(const TimeMap& map, const std::vector<Segment>& segs)
{
    std::vector<double> out;
    for (const auto& s : segs)
    {
        double sum = 0.0; int n = 0;
        for (auto t = s.begin + s.delay; t < s.end + s.delay; t += 0.01, ++n) sum += map.map(t) - t;
        out.push_back(n > 0 ? sum / n : 0.0);
    }
    return out;
}

void summary(std::FILE* md, const char* name, const std::vector<Segment>& segs, const std::vector<double>& est)
{
    std::vector<double> err;
    for (size_t i = 0; i < segs.size(); ++i) err.push_back(std::abs(est[i] + segs[i].delay));
    auto sorted = err;
    std::sort(sorted.begin(), sorted.end());
    const auto within = static_cast<double>(std::count_if(err.begin(), err.end(), [](double e) { return e <= 0.03; }));
    std::fprintf(md, "| %s | %zu | %.0f ms | %.0f%% | %.0f ms |\n", name, segs.size(), 1000.0 * sorted[sorted.size() / 2],
                 100.0 * within / static_cast<double>(err.size()), 1000.0 * sorted.back());
}

void writeMix(const std::string& path, const std::vector<float>& lead, const std::vector<float>& target, int sr)
{
    std::vector<float> mix(lead.size());
    for (size_t i = 0; i < mix.size(); ++i) mix[i] = 0.5f * lead[i] + 0.5f * target[i];
    writeWavMono16(path.c_str(), mix, sr);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 5) { std::fprintf(stderr, "usage: vt_phrase_timing_eval lead.wav startSec durationSec outDir\n"); return 2; }
    std::vector<float> all;
    int sr = 0;
    if (! readWavMono(argv[1], all, sr)) { std::fprintf(stderr, "cannot read wav\n"); return 1; }
    const auto a = static_cast<size_t>(std::atof(argv[2]) * sr);
    const auto b = std::min(all.size(), a + static_cast<size_t>(std::atof(argv[3]) * sr));
    const std::vector<float> lead(all.begin() + static_cast<long>(a), all.begin() + static_cast<long>(b));
    const std::string out = argv[4];
    std::filesystem::create_directories(out);
    const auto ref = extractVocalFeatures(std::vector<double>(lead.begin(), lead.end()), sr);
    if (! ref) return 1;
    const auto segs = segmentsOf(*ref, 0.5);
    const StereoReader read = [&lead](int64_t start, int count, float* l, float* r) {
        for (int i = 0; i < count; ++i)
        {
            const auto k = start + i;
            l[i] = r[i] = k >= 0 && k < static_cast<int64_t>(lead.size()) ? lead[static_cast<size_t>(k)] : 0.0f;
        }
    };
    const auto refBands = computeBandFrames(read, static_cast<int64_t>(lead.size()), sr);
    auto* md = std::fopen((out + "/results.md").c_str(), "w");
    std::fprintf(md, "# 문장 중간 진입 화음 박자 찾기 (%s, %s s부터 %s s)\n\n정답 시차를 알고 만든 따라가는 트랙. 오차 = |찾은 이동량 - 정답|.\n",
                 argv[1], argv[2], argv[3]);
    for (const auto interval : { 0.0, 4.0, 7.0 })
    {
        const auto target = buildTarget(lead, sr, *ref, segs, interval, nullptr);
        const auto tf = extractVocalFeatures(std::vector<double>(target.begin(), target.end()), sr);
        if (! tf) return 1;
        const auto oldMap = guidedTimeMap(static_cast<int64_t>(lead.size()), sr, read, *tf, 0.0, 0.0);
        PhraseTimingOptions po;   // 실험용: 환경 변수로 바꿀 수 있다 (기본은 옵션 기본값)
        if (const char* v = std::getenv("PT_JUMP")) po.jumpCostPerSec = std::atof(v);
        if (const char* v = std::getenv("PT_TRANSIENT")) po.transientWeight = std::atof(v);
        if (const char* v = std::getenv("PT_PITCH")) po.pitchWeight = std::atof(v);
        if (const char* v = std::getenv("PT_CONF")) po.minConfidence = std::atof(v);
        if (const char* v = std::getenv("PT_PIECE")) po.maxPieceSec = std::atof(v);
        const auto newMap = phraseTimeMap(*ref, 0.0, *tf, 0.0, po);
        const StereoReader readTarget = [&target](int64_t start, int count, float* l, float* r) {
            for (int i = 0; i < count; ++i)
            {
                const auto k = start + i;
                l[i] = r[i] = k >= 0 && k < static_cast<int64_t>(target.size()) ? target[static_cast<size_t>(k)] : 0.0f;
            }
        };
        BandAlignOptions bo;   // 실험용: 환경 변수로 바꿀 수 있다
        if (const char* v = std::getenv("BA_SHIFT")) bo.maxShiftSec = std::atof(v);
        if (const char* v = std::getenv("BA_REPEAT")) bo.repeatPenalty = std::atof(v);
        if (const char* v = std::getenv("BA_SKIP")) bo.skipPenalty = std::atof(v);
        if (const char* v = std::getenv("BA_DIST")) bo.shiftPenaltyPerSec = std::atof(v);
        if (const char* v = std::getenv("BA_STRETCH")) bo.maxStretch = std::atof(v);
        if (const char* v = std::getenv("BA_MEDIAN")) bo.medianSec = std::atof(v);
        if (const char* v = std::getenv("BA_TRANSIENT")) bo.transientWeight = std::atof(v);
        if (const char* v = std::getenv("BA_SSNAP")) bo.startSnapSec = std::atof(v);
        if (const char* v = std::getenv("BA_SBLEND")) bo.startBlendSec = std::atof(v);
        if (const char* v = std::getenv("BA_PASSES")) bo.passes = std::atoi(v);
        if (const char* v = std::getenv("BA_SNAP")) bo.snapOnsetSec = std::atof(v);
        if (const char* v = std::getenv("BA_SNAPW")) bo.snapWidthSec = std::atof(v);
        if (const char* v = std::getenv("BA_ONSET")) bo.onsetThreshold = static_cast<float>(std::atof(v));
        if (const char* v = std::getenv("BA_LEVEL")) bo.levelWeight = std::atof(v);
        if (const char* v = std::getenv("BA_STRONG")) bo.strongBelowDb = std::atof(v);
        if (const char* v = std::getenv("BA_START")) bo.startPenaltyPerSec = std::atof(v);
        if (const char* v = std::getenv("BA_DIFF")) bo.maxDifferenceSec = std::atof(v);
        if (const char* v = std::getenv("BA_SMOOTH")) bo.smoothSec = std::atof(v);
        const auto bandMap = bandAlignMap(refBands, 0.0, computeBandFrames(readTarget, static_cast<int64_t>(target.size()), sr), 0.0, bo);
        const auto oldEst = estimates(oldMap, segs), newEst = estimates(newMap, segs), bandEst = estimates(bandMap, segs);
        std::fprintf(md, "\n## 화음 간격 %+.0f 반음\n\n| 방식 | 조각 | 오차 중앙값 | 30 ms 안 | 최대 오차 |\n|---|---|---|---|---|\n", interval);
        summary(md, "현재 (GuidedTiming)", segs, oldEst);
        summary(md, "프레이즈 (PhraseTiming)", segs, newEst);
        summary(md, "대역 DTW (BandAlign)", segs, bandEst);
        std::fprintf(md, "\n| 조각 (기준 시간) | 정답 | 현재 | 프레이즈 | 대역 DTW |\n|---|---|---|---|---|\n");
        for (size_t i = 0; i < segs.size(); ++i)
            std::fprintf(md, "| %.2f-%.2f | %+.3f | %+.3f | %+.3f | %+.3f |\n", segs[i].begin, segs[i].end, -segs[i].delay, oldEst[i], newEst[i], bandEst[i]);
        if (interval == 4.0)
        {
            writeMix(out + "/listen_1_before.wav", lead, target, sr);
            writeMix(out + "/listen_2_current.wav", lead, buildTarget(lead, sr, *ref, segs, interval, &oldEst), sr);
            writeMix(out + "/listen_3_phrase.wav", lead, buildTarget(lead, sr, *ref, segs, interval, &newEst), sr);
            writeMix(out + "/listen_4_band.wav", lead, buildTarget(lead, sr, *ref, segs, interval, &bandEst), sr);
        }
    }
    std::fclose(md);
    return 0;
}
