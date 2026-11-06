#include "EditState.h"
#include <limits>
#include "EditStateIO.h"

namespace
{
constexpr uint32_t magic = 'V' | ('T' << 8) | ('E' << 16) | (static_cast<uint32_t>('D') << 24);   // "VTED"
constexpr size_t minBlockBytes = 150;   // 블록 하나가 차지하는 최소 바이트 (개수 검사용)
constexpr uint32_t formatVersion = 1;

void writeFingerprint(ByteWriter& w, const EditFingerprint& f)
{
    w.f64(f.sampleRate);
    w.u64(static_cast<uint64_t>(f.numSamples));
    w.u64(f.numFrames);
}

EditFingerprint readFingerprint(ByteReader& r)
{
    EditFingerprint f;
    f.sampleRate = r.f64();
    f.numSamples = static_cast<int64_t>(r.u64());
    f.numFrames = static_cast<size_t>(r.u64());
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

void writeEdits(ByteWriter& w, const VocalBlock& b)
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

void readEdits(ByteReader& r, VocalBlock& b)
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

// 범위가 프레임 안에 있고 앞 블록과 겹치지 않으며, 곡선/핸들이 범위에 맞아야 함
bool blockValid(const VocalBlock& b, size_t previousEnd, size_t numFrames)
{
    return b.beginFrame < b.endFrame && b.endFrame <= numFrames && b.beginFrame >= previousEnd
           && curveSizeOk(b.curveOffset.size(), b) && curveSizeOk(b.curveWarp.size(), b) && handlesOk(b);
}

bool headerOk(ByteReader& r, const EditFingerprint& expected)
{
    const auto m = r.u32();
    const auto version = r.u32();
    const auto fingerprint = readFingerprint(r);
    return r.ok && m == magic && version == formatVersion && fingerprint == expected;
}

bool readBlocks(ByteReader& r, size_t numFrames, std::vector<VocalBlock>& blocks)
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
        readEdits(r, b);
        if (! r.ok || ! blockValid(b, previousEnd, numFrames))
            return false;
        previousEnd = b.endFrame;
        blocks.push_back(std::move(b));
    }
    return true;
}
} // namespace

std::vector<uint8_t> encodeEditState(const std::vector<VocalBlock>& blocks, const EditFingerprint& fingerprint)
{
    ByteWriter w;
    w.u32(magic);
    w.u32(formatVersion);
    writeFingerprint(w, fingerprint);
    w.u32(static_cast<uint32_t>(blocks.size()));
    for (const auto& b : blocks)
    {
        writeStructure(w, b);
        writeEdits(w, b);
    }
    return std::move(w.bytes);
}

std::optional<std::vector<VocalBlock>> decodeEditState(const uint8_t* data, size_t size, const EditFingerprint& expected)
{
    ByteReader r(data, size);
    std::vector<VocalBlock> blocks;
    if (data == nullptr || ! headerOk(r, expected) || ! readBlocks(r, expected.numFrames, blocks) || ! r.atEnd())
        return std::nullopt;
    return blocks;
}
