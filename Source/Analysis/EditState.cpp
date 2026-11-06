#include "EditState.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "BlockHistory.h"
#include "EditStateIO.h"

namespace
{
constexpr uint32_t magic = 'V' | ('T' << 8) | ('E' << 16) | (static_cast<uint32_t>('D') << 24);   // "VTED"
constexpr size_t minBlockBytes = 150;   // 블록 하나가 차지하는 최소 바이트 (개수 검사용)
constexpr uint32_t formatVersion = 3;   // 2: formantAmount 추가, 3: 지문에 내용 해시 추가 (1, 2도 읽는다)
constexpr uint32_t baseVersion = 1;     // 해시가 없고 모든 블록의 formantAmount가 1이면 이 형식 그대로 쓴다 (예전 빌드가 읽을 수 있게)
constexpr uint32_t formantVersion = 2;
constexpr uint32_t hashVersion = 3;

void writeFingerprint(ByteWriter& w, const EditFingerprint& f, uint32_t version)
{
    w.f64(f.sampleRate);
    w.u64(static_cast<uint64_t>(f.numSamples));
    w.u64(f.numFrames);
    if (version >= hashVersion)
        w.u64(f.contentHash);
}

EditFingerprint readFingerprint(ByteReader& r, uint32_t version)
{
    EditFingerprint f;
    f.sampleRate = r.f64();
    f.numSamples = static_cast<int64_t>(r.u64());
    f.numFrames = static_cast<size_t>(r.u64());
    if (version >= hashVersion)
        f.contentHash = r.u64();
    return f;
}

void writeStructure(ByteWriter& w, const VocalBlock& b)
{
    w.i32(b.id);
    w.u64(b.beginFrame);
    w.u64(b.endFrame);
    w.f64(b.startSec);
    w.f64(b.endSec);
    w.i32(b.baseMidi);
    w.f64(b.pitchOffset);
    w.boolean(b.isConsonant);
    w.boolean(b.isContinuation);
    w.str(b.lyric);
}

// 디코더가 받아들이는 값으로: 유한하지 않으면 1, 아니면 [0, 1]
double writableFormant(double amount) noexcept
{
    return std::isfinite(amount) ? std::clamp(amount, 0.0, 1.0) : 1.0;
}

void writeEdits(ByteWriter& w, const VocalBlock& b, uint32_t version)
{
    w.f64(b.pitchShift);
    w.f64(b.gainDb);
    w.f64(b.startOffsetSec);
    w.f64(b.endOffsetSec);
    w.u32(static_cast<uint32_t>(b.curveOffset.size()));
    for (const auto v : b.curveOffset) w.f32(v);
    w.u32(static_cast<uint32_t>(b.curveWarp.size()));
    for (const auto v : b.curveWarp) w.f32(v);
    w.u32(static_cast<uint32_t>(b.userHandles.size()));
    for (const auto v : b.userHandles) w.u64(v);
    for (const auto v : { b.transitionAfter, b.vibratoDepth, b.vibratoRateHz, b.flattenAmount, b.tidyAmount, b.driftAmount })
        w.f64(v);
    w.i32(b.centerTargetMidi);
    if (version >= 2)
        w.f64(writableFormant(b.formantAmount));
}

void readStructure(ByteReader& r, VocalBlock& b)
{
    b.id = r.i32();
    b.beginFrame = static_cast<size_t>(r.u64());
    b.endFrame = static_cast<size_t>(r.u64());
    b.startSec = r.f64();
    b.endSec = r.f64();
    b.baseMidi = r.i32();
    b.pitchOffset = r.f64();
    b.isConsonant = r.boolean();
    b.isContinuation = r.boolean();
    b.lyric = r.str();
}

std::vector<float> readFloats(ByteReader& r)
{
    const auto n = r.u32();
    std::vector<float> v;
    if (! r.fits(n, 4))
        return v;
    v.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
        v.push_back(r.f32());
    return v;
}

std::vector<size_t> readFrames(ByteReader& r)
{
    const auto n = r.u32();
    std::vector<size_t> v;
    if (! r.fits(n, 8))
        return v;
    v.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
        v.push_back(static_cast<size_t>(r.u64()));
    return v;
}

void readEdits(ByteReader& r, VocalBlock& b, uint32_t version)
{
    b.pitchShift = r.f64();
    b.gainDb = r.f64();
    b.startOffsetSec = r.f64();
    b.endOffsetSec = r.f64();
    b.curveOffset = readFloats(r);
    b.curveWarp = readFloats(r);
    b.userHandles = readFrames(r);
    b.transitionAfter = r.f64();
    b.vibratoDepth = r.f64();
    b.vibratoRateHz = r.f64();
    b.flattenAmount = r.f64();
    b.tidyAmount = r.f64();
    b.driftAmount = r.f64();
    b.centerTargetMidi = r.i32();
    b.formantAmount = version >= 2 ? r.f64() : 1.0;
}

bool curveSizeOk(size_t curveSize, const VocalBlock& b)
{
    return curveSize == 0 || curveSize == b.endFrame - b.beginFrame;
}

bool handlesOk(const VocalBlock& b)
{
    for (const auto h : b.userHandles)
        if (h < b.beginFrame || h > b.endFrame)
            return false;
    return true;
}

bool formantOk(double amount) noexcept
{
    return std::isfinite(amount) && amount >= 0.0 && amount <= 1.0;
}

// 구조(범위/길이)와 포먼트 양만 검사하고 나머지 편집 값의 범위는 보지 않는다 (플러그인이 직접 쓴 데이터)
// 범위가 프레임 안에 있고 앞 블록과 겹치지 않으며, 곡선/핸들이 범위에 맞아야 함
bool blockValid(const VocalBlock& b, size_t previousEnd, size_t numFrames)
{
    return b.beginFrame < b.endFrame && b.endFrame <= numFrames && b.beginFrame >= previousEnd
           && curveSizeOk(b.curveOffset.size(), b) && curveSizeOk(b.curveWarp.size(), b) && handlesOk(b) && formantOk(b.formantAmount);
}

bool headerOk(ByteReader& r, const EditFingerprint& expected, uint32_t& version)
{
    const auto m = r.u32();
    version = r.u32();
    if (! r.ok || m != magic || version < 1 || version > formatVersion)
        return false;
    auto fingerprint = readFingerprint(r, version);
    if (version < hashVersion)
        fingerprint.contentHash = expected.contentHash;   // 해시 없이 저장된 예전 형식: 길이만 비교
    return r.ok && fingerprint == expected;
}

bool readBlocks(ByteReader& r, size_t numFrames, uint32_t version, std::vector<VocalBlock>& blocks)
{
    const auto count = r.u32();
    if (! r.fits(count, minBlockBytes))
        return false;
    blocks.reserve(count);
    size_t previousEnd = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        VocalBlock b;
        readStructure(r, b);
        readEdits(r, b, version);
        if (! r.ok || ! blockValid(b, previousEnd, numFrames))
            return false;
        previousEnd = b.endFrame;
        blocks.push_back(std::move(b));
    }
    return true;
}

uint32_t versionFor(const std::vector<VocalBlock>& blocks, const EditFingerprint& fingerprint)
{
    if (fingerprint.contentHash != 0)
        return hashVersion;
    for (const auto& b : blocks)
        if (writableFormant(b.formantAmount) != 1.0)
            return formantVersion;
    return baseVersion;
}
} // namespace

uint64_t audioContentHash(const std::vector<double>& mono) noexcept
{
    uint64_t h = 1469598103934665603ull;   // FNV-1a 64
    const auto mix = [&h](uint64_t v, int bytes) {
        for (int i = 0; i < bytes; ++i)
        {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 1099511628211ull;
        }
    };
    mix(static_cast<uint64_t>(mono.size()), 8);
    for (const auto x : mono)
    {
        // 16비트 양자화: 같은 녹음이면 빌드·부동소수 차이에 흔들리지 않게
        const auto v = std::isfinite(x) ? std::clamp(x, -1.0, 1.0) : 0.0;
        mix(static_cast<uint64_t>(static_cast<uint16_t>(static_cast<int16_t>(std::lround(v * 32767.0)))), 2);
    }
    return h != 0 ? h : 1;
}

std::vector<uint8_t> encodeEditState(const std::vector<VocalBlock>& blocks, const EditFingerprint& fingerprint)
{
    ByteWriter w;
    const auto version = versionFor(blocks, fingerprint);
    w.u32(magic);
    w.u32(version);
    writeFingerprint(w, fingerprint, version);
    w.u32(static_cast<uint32_t>(blocks.size()));
    for (const auto& b : blocks)
    {
        writeStructure(w, b);
        writeEdits(w, b, version);
    }
    return std::move(w.bytes);
}

std::optional<std::vector<VocalBlock>> decodeEditState(const uint8_t* data, size_t size, const EditFingerprint& expected)
{
    ByteReader r(data, size);
    std::vector<VocalBlock> blocks;
    uint32_t version = 0;
    if (data == nullptr || ! headerOk(r, expected, version) || ! readBlocks(r, expected.numFrames, version, blocks) || ! r.atEnd())
        return std::nullopt;
    return blocks;
}

bool hasEdits(const std::vector<VocalBlock>& blocks, const std::vector<VocalBlock>& original)
{
    if (blocks.size() != original.size())
        return true;
    for (size_t i = 0; i < blocks.size(); ++i)
        if (blocks[i].beginFrame != original[i].beginFrame || blocks[i].endFrame != original[i].endFrame
            || ! sameEdits(blocks[i], original[i]))
            return true;
    return false;
}
