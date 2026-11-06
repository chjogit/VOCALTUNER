#include "../Source/Analysis/EditState.h"
#include "../Source/Analysis/EditStateIO.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

// 버전 1 형식(formantAmount 없음) 호환 시험
namespace
{
int failures = 0;
const EditFingerprint fp { 44100.0, 441000, 2000 };

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

VocalBlock block(size_t begin, size_t end, int id)
{
    VocalBlock b;
    b.id = id;
    b.beginFrame = begin;
    b.endFrame = end;
    b.baseMidi = 60;
    b.lyric = "la";
    b.pitchShift = 1.5;
    b.gainDb = -2.0;
    b.curveOffset.assign(end - begin, 0.25f);
    b.userHandles = { begin + 1 };
    b.centerTargetMidi = 62;
    return b;
}

void writeV1(ByteWriter& w, const VocalBlock& b)
{
    w.i32(b.id); w.u64(b.beginFrame); w.u64(b.endFrame); w.f64(b.startSec); w.f64(b.endSec);
    w.i32(b.baseMidi); w.f64(b.pitchOffset); w.boolean(b.isConsonant); w.boolean(b.isContinuation); w.str(b.lyric);
    w.f64(b.pitchShift); w.f64(b.gainDb); w.f64(b.startOffsetSec); w.f64(b.endOffsetSec);
    w.u32(static_cast<uint32_t>(b.curveOffset.size()));
    for (const auto v : b.curveOffset) w.f32(v);
    w.u32(static_cast<uint32_t>(b.curveWarp.size()));
    for (const auto v : b.curveWarp) w.f32(v);
    w.u32(static_cast<uint32_t>(b.userHandles.size()));
    for (const auto v : b.userHandles) w.u64(v);
    for (const auto v : { b.transitionAfter, b.vibratoDepth, b.vibratoRateHz, b.flattenAmount, b.tidyAmount, b.driftAmount }) w.f64(v);
    w.i32(b.centerTargetMidi);
}

std::vector<uint8_t> encodeV1(const std::vector<VocalBlock>& blocks)
{
    ByteWriter w;
    w.u32('V' | ('T' << 8) | ('E' << 16) | (static_cast<uint32_t>('D') << 24));
    w.u32(1);
    w.f64(fp.sampleRate);
    w.u64(static_cast<uint64_t>(fp.numSamples));
    w.u64(fp.numFrames);
    w.u32(static_cast<uint32_t>(blocks.size()));
    for (const auto& b : blocks) writeV1(w, b);
    return w.bytes;
}

void testV1Decodes()
{
    const std::vector<VocalBlock> blocks { block(10, 30, 1), block(40, 60, 2) };
    const auto v1 = encodeV1(blocks);
    const auto back = decodeEditState(v1.data(), v1.size(), fp);
    check(back && back->size() == 2, "version 1 buffer decodes");
    check(back && back->at(0).formantAmount == 1.0 && back->at(1).formantAmount == 1.0, "version 1 gives formantAmount = 1");
    check(back && back->at(1).pitchShift == 1.5 && back->at(1).centerTargetMidi == 62 && back->at(1).userHandles.size() == 1,
          "version 1 keeps the other edit fields");
    auto cut = v1;
    cut.pop_back();
    check(! decodeEditState(cut.data(), cut.size(), fp).has_value(), "truncated version 1 buffer is rejected");
    auto longer = v1;
    longer.push_back(0);
    check(! decodeEditState(longer.data(), longer.size(), fp).has_value(), "version 1 with trailing bytes is rejected");
}

void testV2RoundTrip()
{
    auto b = block(10, 30, 1);
    b.formantAmount = 0.35;
    const auto bytes = encodeEditState({ b }, fp);
    const auto back = decodeEditState(bytes.data(), bytes.size(), fp);
    check(back && back->at(0).formantAmount == 0.35, "version 2 round-trips formantAmount");
    check(bytes.size() > encodeV1({ b }).size(), "version 2 is larger than version 1");
}
} // namespace

int main()
{
    testV1Decodes();
    testV2RoundTrip();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
