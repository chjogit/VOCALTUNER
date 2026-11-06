// 가이드 박자 찾기 진단 (테스트 아님): vt_guided_timing_diag guide.wav take.wav [targetOffsetSec] [referenceOffsetSec]
// 현재 방식(GuidedTiming)의 창별 점수와, 유성 덩어리마다 세 방식(현재 / 프레이즈 / 대역 DTW)이 옮기는 양을 찍는다.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <string>
#include "../Source/Alignment/BandAlign.h"
#include "../Source/Alignment/GuidedTiming.h"
#include "../Source/Alignment/PhraseTiming.h"
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

double midShift(const TimeMap& m, double a, double b) { return m.map(0.5 * (a + b)) - 0.5 * (a + b); }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: vt_guided_timing_diag guide.wav take.wav [targetOffset] [referenceOffset]\n"); return 2; }
    std::vector<float> ref, tgt;
    int refRate = 0, tgtRate = 0;
    if (! readWavMono(argv[1], ref, refRate) || ! readWavMono(argv[2], tgt, tgtRate)) { std::fprintf(stderr, "cannot read wav\n"); return 1; }
    const double tOff = argc > 3 ? std::atof(argv[3]) : 0.0, rOff = argc > 4 ? std::atof(argv[4]) : 0.0;
    const auto features = extractVocalFeatures(std::vector<double>(tgt.begin(), tgt.end()), tgtRate);
    const auto refFeatures = extractVocalFeatures(std::vector<double>(ref.begin(), ref.end()), refRate);
    if (! features || ! refFeatures) return 1;
    const auto read = readerOf(ref), readTake = readerOf(tgt);

    GuidedTimingOptions o;
    o.onWindow = [](double start, double voiced, double conf, double lag) {
        std::printf("window %7.2f-%7.2f voiced %4.0f%% conf %5.2f lag %+.2f %s\n", start, start + 9.0, voiced * 100, conf, lag,
                    voiced < 0.3 ? "SKIP(voiced)" : conf < 1.5 ? "SKIP(conf)" : "");
    };
    const auto map = guidedTimeMap(static_cast<int64_t>(ref.size()), refRate, read, *features, tOff, rOff, o);
    PhraseTimingOptions po;
    if (const char* v = std::getenv("PT_JUMP")) po.jumpCostPerSec = std::atof(v);
    const auto phrase = phraseTimeMap(*refFeatures, rOff, *features, tOff, po);
    const auto band = bandAlignMap(computeBandFrames(read, static_cast<int64_t>(ref.size()), refRate), rOff,
                                   computeBandFrames(readTake, static_cast<int64_t>(tgt.size()), tgtRate), tOff);

    // 유성 덩어리 (0.4 s 넘는 무음에서 끊음): 덩어리 가운데에서의 이동량
    const auto period = features->framePeriodMs / 1000.0;
    double segStart = -1, last = -1;
    const auto print = [&](double a, double b) {
        std::printf("  %7.2f-%7.2f current %+.3f phrase %+.3f band %+.3f\n", a, b, midShift(map, a, b), midShift(phrase, a, b), midShift(band, a, b));
    };
    std::printf("voiced phrases (take):\n");
    for (size_t i = 0; i < features->size(); ++i)
    {
        const auto t = static_cast<double>(i) * period + tOff;
        if (features->midiF0[i] <= 0) continue;
        if (segStart >= 0 && t - last > 0.4) { print(segStart, last); segStart = -1; }
        if (segStart < 0) segStart = t;
        last = t;
    }
    if (segStart >= 0) print(segStart, last);

    // 청취 (선택, 6번째 인자 = 시작초, 7번째 = 길이초, 8번째 = 파일 앞부분): 기준 + 원래 테이크 / 기준 + 대역 DTW로 옮긴 테이크
    // 옮김은 단순 가변 지연(선형 보간)이라 늘이고 줄인 곳은 음정이 아주 조금 흔들린다. 플러그인 합성과 같지 않다
    if (argc > 7)
    {
        const auto from = std::atof(argv[5]), length = std::atof(argv[6]);
        const std::string prefix = argv[7];
        const auto n = static_cast<size_t>(length * refRate);
        std::vector<float> before(n), after(n);
        double t = from - tOff;   // 테이크 소스 시간 (기준 곡 시간 tau에 놓일 곳)
        for (size_t i = 0; i < n; ++i)
        {
            const auto tau = from + static_cast<double>(i) / refRate;
            // map(t + tOff) = tau 인 t를 앞으로만 찾는다 (map은 단조)
            while (band.map(t + tOff + 1.0 / tgtRate) <= tau && t < static_cast<double>(tgt.size()) / tgtRate) t += 1.0 / tgtRate;
            const auto sample = [&](double s) {
                const auto x = s * tgtRate;
                const auto k = static_cast<long>(std::floor(x));
                if (k < 0 || k + 1 >= static_cast<long>(tgt.size())) return 0.0f;
                const auto f = static_cast<float>(x - k);
                return (1.0f - f) * tgt[static_cast<size_t>(k)] + f * tgt[static_cast<size_t>(k + 1)];
            };
            const auto refIndex = static_cast<long>(std::lround((tau - rOff) * refRate));
            const auto g = refIndex >= 0 && refIndex < static_cast<long>(ref.size()) ? ref[static_cast<size_t>(refIndex)] : 0.0f;
            before[i] = 0.5f * g + 0.5f * sample(tau - tOff);
            after[i] = 0.5f * g + 0.5f * sample(t);
        }
        writeWavMono16((prefix + "_1_before.wav").c_str(), before, refRate);
        writeWavMono16((prefix + "_2_band.wav").c_str(), after, refRate);
    }
    return 0;
}
