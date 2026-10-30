// 원본 vs 렌더 스펙트럼 차이 (#3 아티팩트와 치찰음 구분용)
//   spectral_diff original.wav rendered.wav [frames.csv]
//   spectral_diff original.wav --render [반음] [frames.csv]   기본 자동보정(+반음 이동)으로 렌더해 render.wav 로 저장 후 비교
// 두 파일은 같은 타임라인이어야 한다 (타이밍 편집 없는 렌더/바운스).
#include "EditRenderer.h"
#include "SpectralDiff.h"
#include "WavIO.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
std::vector<float> renderDefault(const std::vector<float>& mono, int rate, double semitones)
{
    AudioPlanes source;
    source.sampleRate = rate;
    source.channels = { mono };
    const auto features = *extractVocalFeatures(std::vector<double>(mono.begin(), mono.end()), rate);
    auto blocks = parseBlocks(features);
    for (auto& b : blocks)
        b.pitchShift += semitones;
    return renderEdited(source, features, blocks)->channels[0];
}

// 구간이 원본의 어떤 블록 위에 있는지: 유성 / 자음 / 빈 곳
const char* placeOf(const std::vector<VocalBlock>& blocks, double t)
{
    for (const auto& b : blocks)
        if (t >= b.startSec && t < b.endSec)
            return b.isConsonant ? "consonant" : "voiced";
    return "gap";
}

void writeCsv(const char* path, const std::vector<SpectralDiffFrame>& frames)
{
    if (FILE* f = std::fopen(path, "w"))
    {
        std::fprintf(f, "time_sec,low_diff_db,high_diff_db,rendered_high_db,flagged\n");
        for (const auto& x : frames)
            std::fprintf(f, "%.4f,%.2f,%.2f,%.2f,%d\n", x.timeSec, x.lowDiffDb, x.highDiffDb, x.renderedHighDb, x.flagged ? 1 : 0);
        std::fclose(f);
    }
}

std::vector<SpectralDiffFrame> report(const std::vector<float>& original, const std::vector<float>& rendered, int rate)
{
    const SpectralDiffOptions options;
    const auto frames = spectralDiff(original, rendered, rate, options);
    auto regions = flaggedRegions(frames, static_cast<double>(options.fftSize) / rate);
    const auto blocks = parseBlocks(*extractVocalFeatures(std::vector<double>(original.begin(), original.end()), rate));
    size_t nearChunk = 0, byPlace[3] = {};
    for (const auto& r : regions)
    {
        const auto mid = 0.5 * (r.startSec + r.endSec);
        nearChunk += std::abs(mid - 10.0 * std::round(mid / 10.0)) < 0.15 ? 1u : 0u;
        const std::string p = placeOf(blocks, mid);
        ++byPlace[p == "voiced" ? 0 : p == "consonant" ? 1 : 2];
    }
    std::printf("frames %zu, flagged regions %zu (voiced %zu / consonant %zu / gap %zu), near 10s chunk boundary %zu\n",
                frames.size(), regions.size(), byPlace[0], byPlace[1], byPlace[2], nearChunk);
    std::sort(regions.begin(), regions.end(), [](const auto& a, const auto& b) { return a.peakDb > b.peakDb; });
    for (size_t i = 0; i < std::min<size_t>(20, regions.size()); ++i)
        std::printf("  %8.3f - %8.3f s  +%5.1f dB HF  %s\n", regions[i].startSec, regions[i].endSec, regions[i].peakDb,
                    placeOf(blocks, 0.5 * (regions[i].startSec + regions[i].endSec)));
    return frames;
}

bool isCsv(const char* arg) { const auto n = std::strlen(arg); return n > 4 && std::strcmp(arg + n - 4, ".csv") == 0; }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: spectral_diff original.wav (rendered.wav | --render [semitones]) [frames.csv]\n");
        return 2;
    }
    std::vector<float> original, rendered;
    int rate = 44100, renderedRate = 44100, next = 3;
    if (! readWavMono(argv[1], original, rate))
        return std::printf("cannot read %s\n", argv[1]), 1;
    if (std::strcmp(argv[2], "--render") == 0)
    {
        const auto semitones = argc > 3 && ! isCsv(argv[3]) ? std::atof(argv[next++]) : 0.0;
        rendered = renderDefault(original, rate, semitones);
        writeWavMono16("render.wav", rendered, rate);
        std::printf("rendered (+%.2f st) -> render.wav\n", semitones);
    }
    else if (! readWavMono(argv[2], rendered, renderedRate) || renderedRate != rate)
        return std::printf("cannot read %s (or sample rate differs)\n", argv[2]), 1;
    const auto frames = report(original, rendered, rate);
    if (argc > next)
        writeCsv(argv[next], frames);
    return 0;
}
