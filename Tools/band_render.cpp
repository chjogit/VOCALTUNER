// 대역 DTW 박자 맞추기를 플러그인과 같은 경로로 렌더한다 (테스트 아님):
//   vt_band_render guide.wav take.wav startSec lengthSec outPrefix
// 두 파일은 같은 곡 시간에서 시작한다고 본다. 테이크 분석 -> 블록 -> fitToReference(박자만, 정도 100%) -> renderEdited.
// 결과: _1_before(기준+원래 테이크), _2_band(기준+맞춘 테이크), _3_band_solo(맞춘 테이크만), 블록 이동량 목록 _moves.txt
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../Source/Alignment/BandAlign.h"
#include "../Source/Alignment/ReferenceFit.h"
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
    const auto map = bandAlignMap(computeBandFrames(readerOf(guide), static_cast<int64_t>(guide.size()), gr), 0.0,
                                  computeBandFrames(readerOf(take), static_cast<int64_t>(take.size()), tr), 0.0);
    TrackRoles roles;
    roles.timing = true;
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
    std::fclose(moves);

    AudioPlanes source;
    source.sampleRate = tr;
    source.channels = { take };
    const auto rendered = renderEdited(source, *features, fitted);
    if (! rendered) return 1;
    const auto& out = rendered->channels.front();
    writeMix(prefix + "_1_before.wav", guide, take, gr);
    writeMix(prefix + "_2_band.wav", guide, out, gr);
    writeWavMono16((prefix + "_3_band_solo.wav").c_str(), out, tr);
    std::printf("blocks %zu, rendered %zu samples\n", fitted.size(), out.size());
    return 0;
}
