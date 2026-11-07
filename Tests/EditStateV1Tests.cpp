#include "../Source/Analysis/EditState.h"
#include "EditStateV1Fixture.h"
#include <iterator>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
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

std::vector<uint8_t> fixtureBytes() { return { std::begin(v1fixture::bytes), std::end(v1fixture::bytes) }; }

void testV1Decodes()
{
    const auto v1 = fixtureBytes();
    const auto back = decodeEditState(v1.data(), v1.size(), fp);
    check(back && back->size() == 2, "version 1 fixture (real 7a0ea0b encoder output) decodes");
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
    check(bytes[4] == 2 && bytes.size() > 100, "version 2 header");
}

// 모든 블록이 formantAmount 1이면 정확히 버전 1 바이트, 하나라도 다르면 버전 2. 둘 다 읽힌다
void testVersionChoice()
{
    const std::vector<VocalBlock> plain { block(10, 30, 1), block(40, 60, 2) };
    check(encodeEditState(plain, fp) == fixtureBytes(), "formant-free blocks encode byte for byte like the 7a0ea0b encoder");
    auto edited = plain;
    edited[1].formantAmount = 0.5;
    const auto v2 = encodeEditState(edited, fp);
    check(v2[4] == 2 && v2 != fixtureBytes(), "any formant edit encodes version 2");
    const auto back = decodeEditState(v2.data(), v2.size(), fp);
    check(back && back->at(0).formantAmount == 1.0 && back->at(1).formantAmount == 0.5, "version 2 mixed blocks decode");
    const auto empty = encodeEditState({}, fp);
    check(empty[4] == 1 && decodeEditState(empty.data(), empty.size(), fp).has_value(), "empty list encodes version 1");
}

// 디코더가 거부하는 값은 쓰지 않는다: 범위 밖은 [0,1]로, 유한하지 않으면 1로 쓴다
void testWriteSanitizes()
{
    auto b = block(10, 30, 1);
    for (const auto bad : { -0.5, 1.7, std::nan(""), std::numeric_limits<double>::infinity() })
    {
        b.formantAmount = bad;
        const auto bytes = encodeEditState({ b }, fp);
        const auto back = decodeEditState(bytes.data(), bytes.size(), fp);
        const auto want = std::isnan(bad) || std::isinf(bad) ? 1.0 : std::clamp(bad, 0.0, 1.0);
        check(back && back->at(0).formantAmount == want, "encode clamps formantAmount so the decoder accepts it");
    }
    b.formantAmount = std::nan("");
    check(encodeEditState({ b }, fp) == encodeEditState({ block(10, 30, 1) }, fp), "non-finite formantAmount encodes like 1.0");
}
} // namespace

int main()
{
    testV1Decodes();
    testV2RoundTrip();
    testVersionChoice();
    testWriteSanitizes();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
