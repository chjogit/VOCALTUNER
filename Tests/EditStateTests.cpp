#include "../Source/Analysis/BlockHistory.h"
#include "../Source/Analysis/EditState.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

const EditFingerprint fp { 44100.0, 441000, 2000 };

VocalBlock block(size_t begin, size_t end)
{
    VocalBlock b;
    b.beginFrame = begin;
    b.endFrame = end;
    b.baseMidi = 60;
    return b;
}

void fillStructure(VocalBlock& b, size_t begin, size_t end, int id)
{
    b.id = id;
    b.startSec = begin * 0.005 + 0.001;
    b.endSec = end * 0.005 - 0.002;
    b.pitchOffset = -0.37;
    b.isConsonant = id % 2 == 0;
    b.isContinuation = id % 3 == 0;
    b.lyric = "\xEA\xB0\x80 la";
}

void fillScalarEdits(VocalBlock& b)
{
    b.pitchShift = 1.25;
    b.gainDb = -4.5;
    b.startOffsetSec = 0.011;
    b.endOffsetSec = -0.013;
    b.transitionAfter = 0.8;
    b.vibratoDepth = 0.4;
    b.vibratoRateHz = 6.1;
    b.flattenAmount = 0.3;
    b.tidyAmount = 0.6;
    b.driftAmount = -0.2;
    b.centerTargetMidi = 62;
    b.formantAmount = 0.35;
}

void fillCurves(VocalBlock& b, size_t begin, size_t end)
{
    for (size_t i = begin; i < end; ++i)
    {
        b.curveOffset.push_back(0.1f * static_cast<float>(i - begin));
        b.curveWarp.push_back(-0.25f * static_cast<float>(i - begin));
    }
    b.userHandles = { begin + 1, end - 1 };
}

// 구조와 편집 필드를 모두 기본값이 아닌 값으로 채운 블록
VocalBlock editedBlock(size_t begin, size_t end, int id)
{
    auto b = block(begin, end);
    fillStructure(b, begin, end, id);
    fillScalarEdits(b);
    fillCurves(b, begin, end);
    return b;
}

// 14개 편집 필드를 하나씩 비교 (sameEdits에 의존하지 않음)
bool sameEditFields(const VocalBlock& a, const VocalBlock& b)
{
    return a.pitchShift == b.pitchShift && a.gainDb == b.gainDb && a.startOffsetSec == b.startOffsetSec
           && a.endOffsetSec == b.endOffsetSec && a.curveOffset == b.curveOffset && a.curveWarp == b.curveWarp
           && a.userHandles == b.userHandles && a.transitionAfter == b.transitionAfter
           && a.vibratoDepth == b.vibratoDepth && a.vibratoRateHz == b.vibratoRateHz
           && a.flattenAmount == b.flattenAmount && a.tidyAmount == b.tidyAmount && a.driftAmount == b.driftAmount
           && a.centerTargetMidi == b.centerTargetMidi;
}

bool sameStructure(const VocalBlock& a, const VocalBlock& b)
{
    return a.id == b.id && a.beginFrame == b.beginFrame && a.endFrame == b.endFrame && a.startSec == b.startSec
           && a.endSec == b.endSec && a.baseMidi == b.baseMidi && a.pitchOffset == b.pitchOffset
           && a.isConsonant == b.isConsonant && a.isContinuation == b.isContinuation && a.lyric == b.lyric;
}

bool sameList(const std::vector<VocalBlock>& a, const std::vector<VocalBlock>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (! sameStructure(a[i], b[i]) || ! sameEditFields(a[i], b[i]))
            return false;
    return true;
}

bool decodes(const std::vector<uint8_t>& bytes, const EditFingerprint& expected = fp)
{
    return decodeEditState(bytes.data(), bytes.size(), expected).has_value();
}

std::vector<VocalBlock> sample() { return { editedBlock(10, 30, 1), editedBlock(30, 50, 2), editedBlock(60, 70, 3) }; }

void testRoundTrip()
{
    const auto blocks = sample();
    const auto bytes = encodeEditState(blocks, fp);
    const auto back = decodeEditState(bytes.data(), bytes.size(), fp);
    check(back && sameList(*back, blocks), "round trip keeps every structure and edit field");
    check(back && back->at(0).lyric == blocks[0].lyric && back->at(0).userHandles.size() == 2, "lyric and handles survive");

    const auto empty = encodeEditState({}, fp);
    const auto emptyBack = decodeEditState(empty.data(), empty.size(), fp);
    check(emptyBack && emptyBack->empty(), "empty list round-trips");
}

void testAfterCut()
{
    auto left = editedBlock(10, 25, 1);
    auto right = editedBlock(25, 50, 2);
    right.id = 7;
    const std::vector<VocalBlock> cut { left, right, editedBlock(60, 70, 3) };
    const auto bytes = encodeEditState(cut, fp);
    const auto back = decodeEditState(bytes.data(), bytes.size(), fp);
    check(back && sameList(*back, cut), "a list that differs structurally from fresh analysis round-trips");
}

void testTruncation()
{
    const auto bytes = encodeEditState(sample(), fp);
    bool allRejected = true;
    for (size_t n = 0; n < bytes.size(); ++n)
        allRejected = allRejected && ! decodeEditState(bytes.data(), n, fp).has_value();
    check(allRejected, "every truncation length is rejected");
    check(! decodeEditState(nullptr, 0, fp).has_value(), "null input is rejected");
}

void testHeader()
{
    auto bytes = encodeEditState(sample(), fp);
    auto flipped = bytes;
    flipped[0] ^= 0xFF;
    check(! decodes(flipped), "flipped magic is rejected");

    auto v3 = bytes;
    v3[4] = 3;
    check(! decodes(v3), "unknown version is rejected");
    auto v0 = bytes;
    v0[4] = 0;
    check(! decodes(v0), "version 0 is rejected");

    check(! decodes(bytes, { 48000.0, fp.numSamples, fp.numFrames }), "sample rate mismatch is rejected");
    check(! decodes(bytes, { fp.sampleRate, fp.numSamples + 1, fp.numFrames }), "sample count mismatch is rejected");
    check(! decodes(bytes, { fp.sampleRate, fp.numSamples, fp.numFrames + 1 }), "frame count mismatch is rejected");

    bytes.push_back(0);
    check(! decodes(bytes), "trailing bytes are rejected");
}

void testInvalidBlocks()
{
    check(! decodes(encodeEditState({ block(20, 20) }, fp)), "empty range is rejected");
    check(! decodes(encodeEditState({ block(30, 20) }, fp)), "reversed range is rejected");
    check(! decodes(encodeEditState({ block(1990, 2001) }, fp)), "range past the last frame is rejected");
    check(decodes(encodeEditState({ block(1990, 2000) }, fp)), "range ending at the last frame is accepted");
    check(! decodes(encodeEditState({ block(30, 40), block(10, 20) }, fp)), "unsorted ranges are rejected");
    check(! decodes(encodeEditState({ block(10, 30), block(25, 40) }, fp)), "overlapping ranges are rejected");

    auto badOffset = block(10, 30);
    badOffset.curveOffset.assign(5, 0.f);
    check(! decodes(encodeEditState({ badOffset }, fp)), "wrong curveOffset size is rejected");
    auto badWarp = block(10, 30);
    badWarp.curveWarp.assign(21, 0.f);
    check(! decodes(encodeEditState({ badWarp }, fp)), "wrong curveWarp size is rejected");

    auto lowHandle = block(10, 30);
    lowHandle.userHandles = { 9 };
    check(! decodes(encodeEditState({ lowHandle }, fp)), "handle before the block is rejected");
    auto highHandle = block(10, 30);
    highHandle.userHandles = { 31 };
    check(! decodes(encodeEditState({ highHandle }, fp)), "handle after the block is rejected");
}

void testNonFinite()
{
    auto nan = block(10, 30);
    nan.gainDb = std::nan("");
    check(! decodes(encodeEditState({ nan }, fp)), "NaN edit value is rejected");
    auto inf = block(10, 30);
    inf.startSec = std::numeric_limits<double>::infinity();
    check(! decodes(encodeEditState({ inf }, fp)), "infinite structure value is rejected");
    auto nanCurve = block(10, 12);
    nanCurve.curveOffset = { 0.f, std::nanf("") };
    check(! decodes(encodeEditState({ nanCurve }, fp)), "NaN curve value is rejected");
    auto nanWarp = block(10, 12);
    nanWarp.curveWarp = { std::nanf(""), 0.f };
    check(! decodes(encodeEditState({ nanWarp }, fp)), "NaN curveWarp value is rejected");
}

void testHasEdits()
{
    const auto original = sample();
    check(! hasEdits(original, original), "identical lists have no edits");

    auto changed = original;
    changed[1].gainDb += 1.0;
    check(hasEdits(changed, original), "one changed field is an edit");

    auto cut = original;
    cut[0].endFrame = 20;
    cut.insert(cut.begin() + 1, editedBlock(20, 30, 9));
    check(hasEdits(cut, original), "a cut block is an edit");

    auto restored = changed;
    restored[1].gainDb = original[1].gainDb;
    check(! hasEdits(restored, original), "changed then restored values are no edit");
}

void testHugeLengths()
{
    auto bytes = encodeEditState({ block(10, 30) }, fp);
    // 블록 개수 필드(헤더 8 + 지문 24 = 오프셋 32)를 거대한 값으로
    for (int i = 0; i < 4; ++i)
        bytes[32 + i] = 0xFF;
    check(! decodes(bytes), "huge block count is rejected without allocating");

    // 첫 블록: 구조 54바이트(lyric 길이 필드는 오프셋 36+54-4) 뒤에 편집 필드가 이어짐
    constexpr size_t blockStart = 36;
    constexpr size_t lyricLen = blockStart + 50;   // id4 begin8 end8 start8 end8 midi4 off8 flags2 -> 50
    auto lyric = encodeEditState({ block(10, 30) }, fp);
    for (int i = 0; i < 4; ++i)
        lyric[lyricLen + i] = 0xFF;
    check(! decodes(lyric), "huge lyric length is rejected");

    // curveOffset 길이 필드: 구조 54 + 스칼라 4개(32) 뒤
    auto curve = encodeEditState({ block(10, 30) }, fp);
    for (int i = 0; i < 4; ++i)
        curve[blockStart + 54 + 32 + i] = 0xFF;
    check(! decodes(curve), "huge curve count is rejected");
}
} // namespace

int main()
{
    testRoundTrip();
    testAfterCut();
    testTruncation();
    testHeader();
    testInvalidBlocks();
    testNonFinite();
    testHasEdits();
    testHugeLengths();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
