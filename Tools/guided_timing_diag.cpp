// 가이드 박자 찾기 진단 (테스트 아님): vt_guided_timing_diag guide.wav take.wav [targetOffsetSec] [referenceOffsetSec]
#include <cstdio>
#include <cstdlib>
#include "../Source/Alignment/GuidedTiming.h"
#include "../Source/Alignment/PhraseTiming.h"
#include "WavIO.h"

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: vt_guided_timing_diag guide.wav take.wav [targetOffset] [referenceOffset]\n"); return 2; }
    std::vector<float> ref, tgt;
    int refRate = 0, tgtRate = 0;
    if (! readWavMono(argv[1], ref, refRate) || ! readWavMono(argv[2], tgt, tgtRate)) { std::fprintf(stderr, "cannot read wav\n"); return 1; }
    const double tOff = argc > 3 ? std::atof(argv[3]) : 0.0, rOff = argc > 4 ? std::atof(argv[4]) : 0.0;
    std::vector<double> mono(tgt.begin(), tgt.end());
    const auto features = extractVocalFeatures(mono, tgtRate);
    if (! features) return 1;
    const StereoReader read = [&ref](int64_t start, int count, float* l, float* r) {
        for (int i = 0; i < count; ++i)
        {
            const auto k = start + i;
            l[i] = r[i] = k >= 0 && k < static_cast<int64_t>(ref.size()) ? ref[static_cast<size_t>(k)] : 0.0f;
        }
    };
    GuidedTimingOptions o;
    o.onWindow = [](double start, double voiced, double conf, double lag) {
        std::printf("window %7.2f-%7.2f voiced %4.0f%% conf %5.2f lag %+.2f %s\n", start, start + 9.0, voiced * 100, conf, lag,
                    voiced < 0.3 ? "SKIP(voiced)" : conf < 1.5 ? "SKIP(conf)" : "");
    };
    const auto map = guidedTimeMap(static_cast<int64_t>(ref.size()), refRate, read, *features, tOff, rOff, o);
    std::printf("map points %zu\n", map.from().size());
    for (size_t i = 0; i < map.from().size(); ++i)
        std::printf("  %7.2f -> %+.3f\n", map.from()[i], map.to()[i] - map.from()[i]);
    const auto refFeatures = extractVocalFeatures(std::vector<double>(ref.begin(), ref.end()), refRate);
    if (! refFeatures) return 1;
    PhraseTimingOptions po;
    if (const char* v = std::getenv("PT_JUMP")) po.jumpCostPerSec = std::atof(v);
    const auto phrase = phraseTimeMap(*refFeatures, rOff, *features, tOff, po);
    // 유성 구간 (0.4 s 이상 무음으로 끊긴 덩어리): 현재 방식 / 프레이즈 방식 이동량
    const auto period = features->framePeriodMs / 1000.0;
    double segStart = -1, last = -1;
    std::printf("voiced phrases (take):\n");
    for (size_t i = 0; i < features->size(); ++i)
    {
        const auto t = static_cast<double>(i) * period + tOff;
        if (features->midiF0[i] > 0)
        {
            if (segStart < 0 || t - last > 0.4) { if (segStart >= 0) std::printf("  %7.2f-%7.2f shift %+.3f phrase %+.3f\n", segStart, last, map.map(segStart) - segStart, phrase.map(0.5 * (segStart + last)) - 0.5 * (segStart + last)); segStart = t; }
            last = t;
        }
    }
    if (segStart >= 0) std::printf("  %7.2f-%7.2f shift %+.3f phrase %+.3f\n", segStart, last, map.map(segStart) - segStart, phrase.map(0.5 * (segStart + last)) - 0.5 * (segStart + last));
    return 0;
}
