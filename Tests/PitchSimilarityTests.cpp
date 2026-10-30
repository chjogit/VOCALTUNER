#include "../Source/Render/EditRenderer.h"
#include "../Source/Analysis/BlockParser.h"
#include "../Source/Analysis/BlockTransforms.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

// 원곡과 보정 결과의 보컬 피치 유사도 검증. 인자 없이 실행하면 합성 멜로디, WAV 경로를 주면 그 녹음으로 검증한다.
namespace
{
int failures = 0;
constexpr int sr = 44100;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

std::vector<float> syntheticMelody()
{
    const double notes[] = { 57, 60, 64, 62, 59, 65 };
    std::vector<float> x(static_cast<size_t>(8.0 * sr), 0.0f);
    double phase[24] = {};
    for (size_t n = 0; n < x.size(); ++n)
    {
        const auto t = static_cast<double>(n) / sr;
        const auto index = static_cast<size_t>(t / 1.2);
        if (index >= 6 || std::fmod(t, 1.2) > 1.05)
            continue;
        const auto local = std::fmod(t, 1.2);
        const auto vibrato = 0.35 * std::sin(2.0 * 3.14159265358979 * 5.6 * local) * std::min(1.0, local / 0.3);
        const auto hz = 440.0 * std::pow(2.0, (notes[index] + vibrato - 69.0) / 12.0);
        double v = 0.0;
        for (int h = 1; h <= 24; ++h)
        {
            phase[h - 1] += 2.0 * 3.14159265358979 * hz * h / sr;
            v += std::sin(phase[h - 1]) / h * (hz * h < 3200.0 ? 1.0 : 0.15);
        }
        x[n] = static_cast<float>(0.2 * v * std::min(1.0, std::min(local / 0.03, (1.05 - local) / 0.03)));
    }
    return x;
}

// 16비트 PCM / 32비트 float WAV (모노 혼합)
bool readWav(const char* path, std::vector<float>& mono, int& rate)
{
    std::ifstream in(path, std::ios::binary);
    if (! in)
        return false;
    char riff[12];
    in.read(riff, 12);
    if (std::memcmp(riff, "RIFF", 4) != 0)
        return false;
    int16_t format = 0, channels = 1, bits = 16;
    int32_t sampleRate = 44100;
    std::vector<char> data;
    for (char id[4]; in.read(id, 4);)
    {
        int32_t size = 0;
        in.read(reinterpret_cast<char*>(&size), 4);
        if (std::memcmp(id, "fmt ", 4) == 0)
        {
            std::vector<char> fmt(static_cast<size_t>(size));
            in.read(fmt.data(), size);
            std::memcpy(&format, fmt.data(), 2);
            std::memcpy(&channels, fmt.data() + 2, 2);
            std::memcpy(&sampleRate, fmt.data() + 4, 4);
            std::memcpy(&bits, fmt.data() + 14, 2);
        }
        else if (std::memcmp(id, "data", 4) == 0)
        {
            data.resize(static_cast<size_t>(size));
            in.read(data.data(), size);
            break;
        }
        else
            in.seekg(size + (size & 1), std::ios::cur);
    }
    const auto bytes = bits / 8;
    const auto frames = data.size() / static_cast<size_t>(bytes * channels);
    mono.assign(frames, 0.0f);
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
        {
            const auto* p = data.data() + (i * static_cast<size_t>(channels) + static_cast<size_t>(c)) * static_cast<size_t>(bytes);
            float v = 0.0f;
            if (bits == 16) { int16_t s; std::memcpy(&s, p, 2); v = static_cast<float>(s) / 32768.0f; }
            else if (bits == 24) { int32_t s = (static_cast<uint8_t>(p[0])) | (static_cast<uint8_t>(p[1]) << 8) | (static_cast<int8_t>(p[2]) << 16); v = static_cast<float>(s) / 8388608.0f; }
            else if (bits == 32 && format == 3) std::memcpy(&v, p, 4);
            mono[i] += v / static_cast<float>(channels);
        }
    rate = sampleRate;
    return frames > 0;
}

struct Similarity { double medianCents = 0, p95Cents = 0, voicedOverlap = 0, maxJumpSemitones = 0; size_t frames = 0; };

Similarity compare(const VocalFeatures& a, const VocalFeatures& b)
{
    std::vector<double> cents;
    size_t voicedA = 0, both = 0;
    double previous = 0.0, maxJump = 0.0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
    {
        if (a.midiF0[i] > 0.0)
            ++voicedA;
        if (a.midiF0[i] > 0.0 && b.midiF0[i] > 0.0)
        {
            ++both;
            cents.push_back(std::abs(a.midiF0[i] - b.midiF0[i]) * 100.0);
        }
        if (b.midiF0[i] > 0.0 && previous > 0.0)
            maxJump = std::max(maxJump, std::abs(b.midiF0[i] - previous));
        previous = b.midiF0[i];
    }
    Similarity s;
    s.frames = cents.size();
    if (cents.empty())
        return s;
    std::sort(cents.begin(), cents.end());
    s.medianCents = cents[cents.size() / 2];
    s.p95Cents = cents[static_cast<size_t>(0.95 * static_cast<double>(cents.size() - 1))];
    s.voicedOverlap = voicedA > 0 ? static_cast<double>(both) / static_cast<double>(voicedA) : 0.0;
    s.maxJumpSemitones = maxJump;
    return s;
}

std::vector<double> toDouble(const std::vector<float>& x) { return { x.begin(), x.end() }; }

Similarity measure(const AudioPlanes& original, const VocalFeatures& features, const std::vector<VocalBlock>& blocks, const char* label)
{
    const auto out = renderEdited(original, features, blocks);
    const auto rendered = *extractVocalFeatures(toDouble(out->channels[0]), original.sampleRate);
    const auto s = compare(features, rendered);
    double peakIn = 0.0, peakOut = 0.0;
    size_t over = 0;
    for (const auto v : original.channels[0]) peakIn = std::max(peakIn, static_cast<double>(std::abs(v)));
    for (const auto v : out->channels[0]) { peakOut = std::max(peakOut, static_cast<double>(std::abs(v))); over += std::abs(v) > 1.0f ? 1u : 0u; }
    std::printf("  %-28s peak in %.3f -> out %.3f, samples over full scale: %zu\n", label, peakIn, peakOut, over);
    std::printf("  %-28s median %.1f cents  p95 %.1f cents  voiced overlap %.1f%%  max jump %.2f st  (%zu frames)\n", label, s.medianCents,
                s.p95Cents, 100.0 * s.voicedOverlap, s.maxJumpSemitones, s.frames);
    return s;
}
} // namespace

int main(int argc, char** argv)
{
    AudioPlanes original;
    original.sampleRate = sr;
    if (argc > 1)
    {
        std::vector<float> mono;
        if (! readWav(argv[1], mono, original.sampleRate))
        {
            std::printf("[FAIL] cannot read %s\n", argv[1]);
            return 1;
        }
        original.channels = { std::move(mono) };
        std::printf("source: %s (%.1f s)\n", argv[1], static_cast<double>(original.length()) / original.sampleRate);
    }
    else
        original.channels = { syntheticMelody() };

    const auto features = *extractVocalFeatures(toDouble(original.channels[0]), original.sampleRate);
    auto blocks = parseBlocks(features);
    check(! blocks.empty(), "blocks parsed");

    // 1) 기본 자동 보정(1% 정돈)은 원곡의 음정을 거의 그대로 둔다
    const auto defaults = measure(original, features, blocks, "default auto-correction");
    check(defaults.medianCents < 12.0 && defaults.p95Cents < 60.0, "default correction stays within 12 / 60 cents of the original");
    check(defaults.voicedOverlap > 0.9, "voiced frames are preserved");

    // 1b) 실제 녹음에서는 +2 반음 이동도 확인 (큰 소리에서 갈라짐/터짐이 생기는지: 최대치와 풀스케일 초과 샘플 수)
    if (argc > 1)
    {
        auto shifted = blocks;
        for (auto& b : shifted)
            b.pitchShift = 2.0;
        measure(original, features, shifted, "pitch +2 semitones");
    }

    // 2) 펴기 -> 굳히기 -> 비브라토 -> 굳히기: 순서대로 완결되어 음정이 튀지 않는다
    auto edited = blocks;
    for (auto& b : edited)
        b.flattenAmount = 0.5;
    for (auto& b : edited)
        b = withShapeBaked(features, b);
    for (auto& b : edited)
        if (! b.isConsonant)
            b.vibratoDepth = 0.4;
    for (auto& b : edited)
        b = withShapeBaked(features, b);
    const auto stepwise = measure(original, features, edited, "flatten 50% + vibrato 0.4");
    check(stepwise.medianCents < 60.0, "stacked edits keep the melody (median < 60 cents)");
    check(stepwise.maxJumpSemitones < 6.0, "no pitch glitches between frames");

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
