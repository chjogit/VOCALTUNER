#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <vector>

// 도구용 최소 WAV 입출력 (16/24비트 PCM, 32비트 float 읽기 → 모노 혼합 / 16비트 모노 쓰기)
inline bool readWavMono(const char* path, std::vector<float>& mono, int& rate)
{
    std::ifstream in(path, std::ios::binary);
    char riff[12];
    if (! in.read(riff, 12) || std::memcmp(riff, "RIFF", 4) != 0)
        return false;
    int16_t format = 0, channels = 1, bits = 16;
    int32_t sampleRate = 44100;
    std::vector<char> data;
    for (char id[4]; in.read(id, 4);)
    {
        uint32_t size = 0;
        in.read(reinterpret_cast<char*>(&size), 4);
        if (std::memcmp(id, "fmt ", 4) == 0)
        {
            std::vector<char> fmt(size);
            in.read(fmt.data(), size);
            std::memcpy(&format, fmt.data(), 2);
            std::memcpy(&channels, fmt.data() + 2, 2);
            std::memcpy(&sampleRate, fmt.data() + 4, 4);
            std::memcpy(&bits, fmt.data() + 14, 2);
        }
        else if (std::memcmp(id, "data", 4) == 0)
        {
            data.resize(size);
            in.read(data.data(), size);
            break;
        }
        else
            in.seekg(size + (size & 1u), std::ios::cur);
    }
    const auto bytes = static_cast<size_t>(bits / 8), ch = static_cast<size_t>(channels);
    const auto frames = data.size() / (bytes * ch);
    mono.assign(frames, 0.0f);
    for (size_t i = 0; i < frames; ++i)
        for (size_t c = 0; c < ch; ++c)
        {
            const auto* p = data.data() + (i * ch + c) * bytes;
            float v = 0.0f;
            if (bits == 16) { int16_t s; std::memcpy(&s, p, 2); v = static_cast<float>(s) / 32768.0f; }
            else if (bits == 24) { int32_t s = static_cast<uint8_t>(p[0]) | (static_cast<uint8_t>(p[1]) << 8) | (static_cast<int8_t>(p[2]) * 65536); v = static_cast<float>(s) / 8388608.0f; }
            else if (bits == 32 && format == 3) std::memcpy(&v, p, 4);
            mono[i] += v / static_cast<float>(channels);
        }
    rate = sampleRate;
    return frames > 0;
}

inline bool writeWavMono16(const char* path, const std::vector<float>& x, int rate)
{
    std::ofstream out(path, std::ios::binary);
    const auto put32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto dataBytes = static_cast<uint32_t>(x.size() * 2);
    out.write("RIFF", 4); put32(36 + dataBytes); out.write("WAVEfmt ", 8);
    put32(16); put16(1); put16(1); put32(static_cast<uint32_t>(rate)); put32(static_cast<uint32_t>(rate) * 2); put16(2); put16(16);
    out.write("data", 4); put32(dataBytes);
    for (const auto v : x)
        put16(static_cast<uint16_t>(static_cast<int16_t>(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f)));
    return static_cast<bool>(out);
}
