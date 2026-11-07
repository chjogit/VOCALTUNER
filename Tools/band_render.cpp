// 대역 DTW 박자 맞추기를 플러그인과 같은 경로로 렌더한다 (테스트 아님):
//   vt_band_render guide.wav take.wav startSec lengthSec outPrefix
// 두 파일은 같은 곡 시간에서 시작한다고 본다. 테이크 분석 -> 블록 -> fitToReference(박자만, 정도 100%) -> renderEdited.
// 결과: _1_before(기준+원래 테이크), _2_band(기준+맞춘 테이크), _3_band_solo(맞춘 테이크만), 블록 이동량 목록 _moves.txt
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../Source/Alignment/BandAlign.h"
#include "../Source/Alignment/ReferenceFit.h"
#include "../Source/Render/EditGrid.h"
#include "../Source/Render/EditTimeMap.h"
#include "../Source/Render/EditRenderer.h"
#include "WavIO.h"

namespace
{
StereoReader readerOf(const std::vector<float>& x)
{
    return [&x](int64_t start, int count, float* l, float* r) {
        for (int i = 0; i < count; ++i)
        {
            const auto k = start + i;
            l[i] = r[i] = k >= 0 && k < static_cast<int64_t>(x.size()) ? x[static_cast<size_t>(k)] : 0.0f;
        }
    };
}

std::vector<float> excerpt(const std::vector<float>& x, int rate, double start, double length)
{
    const auto a = std::min(x.size(), static_cast<size_t>(start * rate));
    const auto b = std::min(x.size(), a + static_cast<size_t>(length * rate));
    return { x.begin() + static_cast<long>(a), x.begin() + static_cast<long>(b) };
}

void writeMix(const std::string& path, const std::vector<float>& a, const std::vector<float>& b, int rate)
{
    std::vector<float> mix(std::max(a.size(), b.size()), 0.0f);
    for (size_t i = 0; i < mix.size(); ++i) mix[i] = 0.5f * (i < a.size() ? a[i] : 0.0f) + 0.5f * (i < b.size() ? b[i] : 0.0f);
    writeWavMono16(path.c_str(), mix, rate);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 6) { std::fprintf(stderr, "usage: vt_band_render guide.wav take.wav startSec lengthSec outPrefix\n"); return 2; }
    std::vector<float> guideAll, takeAll;
    int gr = 0, tr = 0;
    if (! readWavMono(argv[1], guideAll, gr) || ! readWavMono(argv[2], takeAll, tr) || gr != tr) { std::fprintf(stderr, "cannot read wav (same rate needed)\n"); return 1; }
    const auto start = std::atof(argv[3]), length = std::atof(argv[4]);
    const std::string prefix = argv[5];
    const auto guide = excerpt(guideAll, gr, start, length), take = excerpt(takeAll, tr, start, length);
    const auto features = extractVocalFeatures(std::vector<double>(take.begin(), take.end()), tr);
    if (! features) return 1;
    const auto blocks = parseBlocks(*features);
    BandAlignOptions bo;   // 실험용: 환경 변수
    if (const char* v = std::getenv("BA_TRANSIENT")) bo.transientWeight = std::atof(v);
        if (const char* v = std::getenv("BA_RISEDB")) bo.riseBelowDb = std::atof(v);
        if (const char* v = std::getenv("BA_SSNAP")) bo.startSnapSec = std::atof(v);
        if (const char* v = std::getenv("BA_SBLEND")) bo.startBlendSec = std::atof(v);
        if (const char* v = std::getenv("BA_PASSES")) bo.passes = std::atoi(v);
        if (const char* v = std::getenv("BA_SNAP")) bo.snapOnsetSec = std::atof(v);
        if (const char* v = std::getenv("BA_SNAPW")) bo.snapWidthSec = std::atof(v);
        if (const char* v = std::getenv("BA_ONSET")) bo.onsetThreshold = static_cast<float>(std::atof(v));
        if (const char* v = std::getenv("BA_STRETCH")) bo.maxStretch = std::atof(v);
        if (const char* v = std::getenv("BA_LEVEL")) bo.levelWeight = std::atof(v);
        if (const char* v = std::getenv("BA_STRONG")) bo.strongBelowDb = std::atof(v);
        if (const char* v = std::getenv("BA_START")) bo.startPenaltyPerSec = std::atof(v);
    if (const char* v = std::getenv("BA_DIST")) bo.shiftPenaltyPerSec = std::atof(v);
    if (const char* v = std::getenv("BA_DIFF")) bo.maxDifferenceSec = std::atof(v);
    const auto map = bandAlignMap(computeBandFrames(readerOf(guide), static_cast<int64_t>(guide.size()), gr), 0.0,
                                  computeBandFrames(readerOf(take), static_cast<int64_t>(take.size()), tr), 0.0, bo);
    if (const char* v = std::getenv("BR_TRACE"))   // "from,to" 곡 시간: 그 사이 이동량을 20 ms마다
    {
        const auto a = std::atof(v) - start, b = std::atof(std::strchr(v, ',') + 1) - start;
        for (auto t = a; t <= b; t += 0.02) std::printf("  map %.2f %+.3f\n", t + start, map.map(t) - t);
    }
    TrackRoles roles;
    roles.timing = true;
    roles.rigidGaps = std::getenv("BR_ELASTIC") == nullptr;
    std::vector<VocalBlock> fitted = blocks;
    for (const auto& u : fitToReference(blocks, 0.0, map, {}, 0.0, roles)) fitted[u.index] = u.block;

    auto* moves = std::fopen((prefix + "_moves.txt").c_str(), "w");
    for (size_t i = 0; i < fitted.size(); ++i)
        std::fprintf(moves, "%8.3f-%8.3f  start %+6.3f  end %+6.3f%s\n", blocks[i].startSec + start, blocks[i].endSec + start,
                     fitted[i].startOffsetSec - blocks[i].startOffsetSec, fitted[i].endOffsetSec - blocks[i].endOffsetSec,
                     blocks[i].isConsonant ? "  (consonant)" : "");
    // 늘임·줄임 정도: 블록 길이 비 (자음 제외)
    size_t over = 0, counted = 0;
    double worst = 0.0;
    for (size_t i = 0; i < fitted.size(); ++i)
    {
        if (blocks[i].isConsonant) continue;
        const auto before = blocks[i].endSec - blocks[i].startSec;
        const auto after = fitted[i].displayEndSec() - fitted[i].displayStartSec();
        if (before <= 0.0) continue;
        const auto change = std::abs(after / before - 1.0);
        ++counted;
        over += change > 0.15 ? 1 : 0;
        worst = std::max(worst, change);
    }
    std::fprintf(moves, "stretch: %zu of %zu blocks change length by more than 15%%, worst %.0f%%\n", over, counted, 100.0 * worst);
    std::printf("stretch: %zu of %zu blocks change length by more than 15%%, worst %.0f%%\n", over, counted, 100.0 * worst);
    // 박자만 바꿨는데 합성기가 음정 변화로 받는 양: 표시 프레임의 목표 음정 - 그 프레임이 가져오는 원본 프레임의 음정
    {
        const auto grid = buildGrid(take.size(), tr, *features, fitted);
        size_t moved = 0, wrong = 0;
        double sum = 0.0, worstCents = 0.0;
        for (size_t i = 0; i < grid.size(); ++i)
        {
            const auto j = static_cast<size_t>(grid.sourceFrame[i]);
            if (static_cast<size_t>(grid.sourceFrame[i]) == std::min(i, features->size() - 1)) continue;
            if (grid.targetMidi[i] <= 0.0 || features->midiF0[j] <= 0.0) continue;
            const auto cents = 100.0 * std::abs(grid.targetMidi[i] - features->midiF0[j]);
            ++moved;
            wrong += cents > 5.0 ? 1 : 0;
            if (cents > 5.0 && std::getenv("BR_LIST"))
                std::printf("  bad frame %.3f s (source %.3f) %+.0f cents\n", static_cast<double>(i) * 0.005 + start,
                            static_cast<double>(j) * 0.005 + start, 100.0 * (grid.targetMidi[i] - features->midiF0[j]));
            sum += cents;
            worstCents = std::max(worstCents, cents);
        }
        std::printf("timing-only pitch error in grid: %zu of %zu moved voiced frames > 5 cents, mean %.1f, worst %.0f cents\n", wrong, moved,
                    moved ? sum / static_cast<double>(moved) : 0.0, worstCents);
    }
    std::fclose(moves);
    {
        // 렌더러가 쓰는 시간 대응(표시 -> 원본)을 1 ms마다 적어 둔다 (밖에서 원본과 맞대어 비교할 때 쓴다)
        const EditTimeMap tm(fitted, *features);
        auto* mapFile = std::fopen((prefix + "_timemap.txt").c_str(), "w");
        for (double t = 0.0; t < length; t += 0.001) std::fprintf(mapFile, "%.3f %.6f\n", t, tm.toSource(t));
        std::fclose(mapFile);
    }
    if (const char* v = std::getenv("BR_PROBE"))   // "from,to" 파일 시간: 표시 시각마다 원본 시각과 기울기, 원본 유성 여부
    {
        const EditTimeMap tm(fitted, *features);
        const auto a = std::atof(v), b = std::atof(std::strchr(v, ',') + 1);
        for (auto t = a; t <= b; t += 0.005)
        {
            const auto s = tm.toSource(t);
            const auto j = static_cast<size_t>(std::lround(s / 0.005));
            std::printf("  probe %.3f -> %.4f slope %.3f midi %.1f rms %.4f\n", t, s, (tm.toSource(t + 0.004) - tm.toSource(t - 0.004)) / 0.008,
                        j < features->size() ? features->midiF0[j] : -1.0, j < features->size() ? features->rms[j] : -1.0);
        }
    }

    AudioPlanes source;
    source.sampleRate = tr;
    source.channels = { take };
    const auto rendered = renderEdited(source, *features, fitted);
    if (! rendered) return 1;
    const auto& out = rendered->channels.front();
    writeMix(prefix + "_1_before.wav", guide, take, gr);
    writeMix(prefix + "_2_band.wav", guide, out, gr);
    writeWavMono16((prefix + "_3_band_solo.wav").c_str(), out, tr);
    // 음정 계단 측정: 출력의 음정 곡선(Harvest)을 원본 시각으로 되돌려 원본 음정과 비교.
    // 박자만 바꿨으므로 같아야 한다. 프레임 사이 변화가 원본보다 크게 튀는 곳(계단의 턱)을 센다
    {
        // BR_CONTROL: 출력 대신 원본을 101샘플 늦춘 것을 재서, 분석 자체의 흔들림(바닥값)을 본다
        const bool control = std::getenv("BR_CONTROL") != nullptr;
        std::vector<double> measured(out.begin(), out.end());
        auto controlBlocks = blocks;
        if (control)
        {
            measured.assign(101, 0.0);
            measured.insert(measured.end(), take.begin(), take.end());
            measured.resize(take.size());
            for (auto& b : controlBlocks) { b.startOffsetSec = 101.0 / tr; b.endOffsetSec = 101.0 / tr; }
        }
        FeatureOptions measureOptions;
        if (const char* v = std::getenv("BR_FP")) measureOptions.framePeriodMs = std::atof(v);
        const auto outFeatures = extractVocalFeatures(measured, tr, measureOptions);
        const auto measurePeriod = measureOptions.framePeriodMs / 1000.0;
        const EditTimeMap tm(control ? controlBlocks : fitted, *features);
        size_t count = 0, steps = 0, off = 0;
        double sum = 0.0;
        double previousDev = 0.0;
        bool havePrevious = false;
        for (size_t i = 0; outFeatures && i < outFeatures->size(); ++i)
        {
            const auto t = static_cast<double>(i) * measurePeriod;
            const auto s = tm.toSource(t) / 0.005;
            const auto j = static_cast<size_t>(std::floor(s));
            if (j + 1 >= features->size() || outFeatures->midiF0[i] <= 0.0 || features->midiF0[j] <= 0.0 || features->midiF0[j + 1] <= 0.0)
            { havePrevious = false; continue; }
            const auto expected = features->midiF0[j] + (features->midiF0[j + 1] - features->midiF0[j]) * (s - static_cast<double>(j));
            const auto dev = 100.0 * (outFeatures->midiF0[i] - expected);
            ++count;
            sum += std::abs(dev);
            off += std::abs(dev) > 20.0 ? 1 : 0;
            if (havePrevious && std::abs(dev - previousDev) > 15.0) ++steps;
            previousDev = dev;
            havePrevious = true;
        }
        std::printf("pitch vs original: %zu voiced frames, mean |dev| %.1f cents, %.1f%% off by >20 cents, %zu step jumps >15 cents between frames\n",
                    count, count ? sum / static_cast<double>(count) : 0.0, count ? 100.0 * static_cast<double>(off) / static_cast<double>(count) : 0.0, steps);
    }
    std::printf("blocks %zu, rendered %zu samples\n", fitted.size(), out.size());
    return 0;
}
