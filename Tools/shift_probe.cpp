// 음정 이동 왜곡 위치 찾기용 렌더 CLI (테스트 아님). 플러그인과 같은 경로(분석 -> 블록 -> renderEdited)로 렌더한다.
//   vt_shift_probe take.wav list                       블록 목록 (번호, 시작, 끝, 중심 MIDI, 자음 여부)
//   vt_shift_probe take.wav render plan.txt out.wav    plan.txt 줄마다 "블록번호 반음" 또는 "all 반음" (자음 블록 제외)
// 출력은 32비트 float 모노 wav (양자화로 측정이 흔들리지 않게)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include "../Source/Render/EditRenderer.h"
#include "../Source/Render/EditShiftCurves.h"
#include "../Source/Analysis/BlockTransforms.h"
#include "../Source/Analysis/PitchFlow.h"
#include "WavIO.h"

namespace
{
bool writeWavFloat(const char* path, const std::vector<float>& x, int rate)
{
    std::ofstream out(path, std::ios::binary);
    const auto put32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<uint32_t>(x.size() * 4);
    out.write("RIFF", 4); put32(36 + bytes); out.write("WAVEfmt ", 8);
    put32(16); put16(3); put16(1); put32(static_cast<uint32_t>(rate)); put32(static_cast<uint32_t>(rate) * 4); put16(4); put16(32);
    out.write("data", 4); put32(bytes);
    out.write(reinterpret_cast<const char*>(x.data()), static_cast<std::streamsize>(bytes));
    return static_cast<bool>(out);
}

void applyPlan(const char* path, std::vector<VocalBlock>& blocks)
{
    std::ifstream in(path);
    std::string who;
    double shift = 0.0;
    while (in >> who >> shift)
    {
        if (who == "tidy" || who == "drift" || who == "flatten")   // (앞 글자 규칙보다 먼저: "tidy"가 "t블록" 박자 줄로 읽히지 않게) 모든 유성 블록의 중심 음정 / 음정 흐름 / 펴기 양 (UI 슬라이더)
        {
            for (auto& b : blocks)
                if (! b.isConsonant) (who == "tidy" ? b.tidyAmount : who == "drift" ? b.driftAmount : b.flattenAmount) = shift;
            continue;
        }
        if (who == "all")
        {
            for (auto& b : blocks)
                if (! b.isConsonant) b.pitchShift = shift;
            continue;
        }
        if (who[0] == 't')   // "t블록번호 초": 블록을 그만큼 옮긴다 (박자 편집 -> 문서 전체가 PsolaWarp 경로)
        {
            const auto k = static_cast<size_t>(std::atoi(who.c_str() + 1));
            if (k < blocks.size()) blocks[k].startOffsetSec = blocks[k].endOffsetSec = shift;
            continue;
        }
        if (who[0] == 's')   // "s블록번호 초": 블록 끝만 그만큼 옮긴다 (늘이기/줄이기)
        {
            const auto k = static_cast<size_t>(std::atoi(who.c_str() + 1));
            if (k < blocks.size()) blocks[k].endOffsetSec = shift;
            continue;
        }
        if (who[0] == 'u')   // "u블록번호 시작초 끝초": 시작·끝 이동을 따로 (맞추기처럼 이어진 늘이기/줄이기)
        {
            const auto k = static_cast<size_t>(std::atoi(who.c_str() + 1));
            double end = 0.0;
            in >> end;
            if (k < blocks.size()) { blocks[k].startOffsetSec = shift; blocks[k].endOffsetSec = end; }
            continue;
        }
        if (who[0] == 'c')   // "c블록번호 반음": 검사기 양성 대조용 - 블록 곡선에 6프레임(30 ms)마다 0/반음을 번갈아 넣은 계단
        {
            const auto k = static_cast<size_t>(std::atoi(who.c_str() + 1));
            if (k < blocks.size())
            {
                auto& b = blocks[k];
                b.curveOffset.assign(b.endFrame - b.beginFrame, 0.0f);
                for (size_t i = 0; i < b.curveOffset.size(); ++i) b.curveOffset[i] = (i / 6) % 2 ? static_cast<float>(shift) : 0.0f;
            }
            continue;
        }
        if (who[0] == 'v' || who[0] == 'r')   // "v블록번호 반음": 5.5 Hz 사인 선, "r블록번호 반음": 블록 처음 0 -> 끝 반음 직선 (그린 선 흉내)
        {
            const auto k = static_cast<size_t>(std::atoi(who.c_str() + 1));
            if (k < blocks.size())
            {
                auto& b = blocks[k];
                const auto n = b.endFrame - b.beginFrame;
                b.curveOffset.assign(n, 0.0f);
                for (size_t i = 0; i < n; ++i)
                    b.curveOffset[i] = static_cast<float>(who[0] == 'v' ? shift * std::sin(2.0 * 3.14159265358979 * 5.5 * 0.005 * static_cast<double>(i))
                                                                        : shift * static_cast<double>(i) / static_cast<double>(std::max<size_t>(1, n - 1)));
            }
            continue;
        }
        if (who == "joints")   // "joints 양": 모든 블록의 음정 이음 정도 (transitionAfter, 기본 0.5)
        {
            for (auto& b : blocks) b.transitionAfter = shift;
            continue;
        }
        const auto k = static_cast<size_t>(std::atoi(who.c_str()));
        if (k < blocks.size()) blocks[k].pitchShift = shift;
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: vt_shift_probe take.wav list | render plan.txt out.wav\n"); return 2; }
    std::vector<float> take;
    int rate = 0;
    if (! readWavMono(argv[1], take, rate)) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    const auto features = extractVocalFeatures(std::vector<double>(take.begin(), take.end()), rate);
    if (! features) return 1;
    auto blocks = parseBlocks(*features);
    if (std::strcmp(argv[2], "list") == 0)
    {
        for (size_t k = 0; k < blocks.size(); ++k)
            std::printf("%zu %.3f %.3f %.2f %d\n", k, blocks[k].startSec, blocks[k].endSec,
                        blocks[k].baseMidi + blocks[k].pitchOffset, blocks[k].isConsonant ? 1 : 0);
        return 0;
    }
    if (argc < 5) return 2;
    applyPlan(argv[3], blocks);
    AudioPlanes src;
    src.sampleRate = rate;
    src.channels.push_back(take);
    if (std::strcmp(argv[2], "parts") == 0)   // parts plan.txt 프레임: 그 프레임을 가진 블록의 편집 음정 구성
    {
        const auto frame = static_cast<size_t>(std::atoi(argv[4]));
        for (size_t k = 0; k < blocks.size(); ++k)
            if (frame >= blocks[k].beginFrame && frame < blocks[k].endFrame)
                std::printf("block %zu midi %.3f flow %.3f shape %.3f edited %.3f tidy %.2f base %d off %.3f target %d\n", k, features->midiF0[frame],
                            flowPitchShift(*features, blocks, k, frame), shapeOffsetAt(*features, blocks[k], frame), editedPitchAt(*features, blocks, k, frame),
                            blocks[k].tidyAmount, blocks[k].baseMidi, blocks[k].pitchOffset, blocks[k].centerTargetMidi);
        return 0;
    }
    if (std::strcmp(argv[2], "map") == 0)   // map plan.txt dummy: 출력 1 ms마다 렌더러가 쓰는 원본 시각 (초)
    {
        const auto grid = buildGrid(take.size(), rate, *features, blocks);
        for (double t = 0.0; t < static_cast<double>(take.size()) / rate; t += 0.001)
            std::printf("%.6f\n", grid.timeMap->toSource(t));
        return 0;
    }
    if (std::strcmp(argv[2], "line") == 0)   // line plan.txt dummy: 출력 프레임마다 가져오는 원본 프레임, 그 원본 MIDI, 그린 선(목표 MIDI)
    {
        const auto grid = buildGrid(take.size(), rate, *features, blocks);
        for (size_t i = 0; i < grid.size(); ++i)
        {
            const auto j = static_cast<size_t>(grid.sourceFrame[i]);
            std::printf("%.3f %zu %.3f %.3f\n", i * features->framePeriodMs / 1000.0, j, features->midiF0[j], grid.targetMidi[i]);
        }
        return 0;
    }
    if (std::strcmp(argv[2], "curve") == 0)   // curve plan.txt dummy: 프레임마다 원본 MIDI, 계획 이동, 자음 보호+모음 중심 뒤 이동
    {
        AudioPlanes one;
        one.sampleRate = rate;
        one.channels.push_back(take);
        auto grid = buildGrid(take.size(), rate, *features, blocks);
        grid.vowelFocus = std::getenv("VT_NOVF") == nullptr;
        const ChunkSpan all { 0, features->size(), 0, features->size() };
        const auto raw = psolaShiftCurve(*features, grid, all, 0.05);
        const auto prot = protectedShift(one, *features, grid, raw, 0, features->size());
        for (size_t i = 0; i < features->size(); ++i)
            std::printf("%.3f %.2f %.3f %.3f\n", i * features->framePeriodMs / 1000.0, features->midiF0[i], raw[i], prot[i]);
        return 0;
    }
    RenderOptions options;
    if (std::getenv("VT_NOVF")) options.vowelFocus = false;   // 실험: 모음 중심 이동 끄기
    const auto out = renderEdited(src, *features, blocks, options);
    if (! out) return 1;
    if (const char* r = std::getenv("VT_RANGECHECK"))   // 실험: 0.5초 구간 렌더(미리 듣기)와 전체 렌더의 최대 차이
    {
        double worst = 0.0, worstAt = 0.0, worstFrom = 0.0;
        for (double a = std::getenv("VT_RANGEFROM") ? std::atof(std::getenv("VT_RANGEFROM")) : 0.0; a + 0.5 < take.size() / static_cast<double>(rate); a += std::atof(r))
        {
            const auto b0 = static_cast<size_t>(a * rate), b1 = static_cast<size_t>((a + 0.5) * rate);
            const auto seg = renderEditedRange(src, *features, blocks, b0, b1, options);
            if (! seg) return 1;
            for (size_t i = b0; i < b1; ++i)
                if (i >= seg->startSample && i < seg->endSample())
                {
                    const auto d = static_cast<double>(std::abs(seg->audio.channels[0][i - seg->startSample] - out->channels[0][i]));
                    if (d > worst) { worst = d; worstAt = static_cast<double>(i) / rate; worstFrom = a; }
                }
        }
        std::printf("range max diff %.3g at %.4f s (range from %.2f s)\n", worst, worstAt, worstFrom);
    }
    return writeWavFloat(argv[4], out->channels[0], rate) ? 0 : 1;
}
