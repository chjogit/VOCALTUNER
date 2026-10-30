#include "../Source/Analysis/BlockEditing.h"
#include <cstdio>
#include <cstdlib>

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

// 200프레임: [0,100) 60.0 음, [100,200) 62.0 음. 모두 유성.
VocalFeatures makeFeatures()
{
    VocalFeatures f;
    for (size_t i = 0; i < 200; ++i)
    {
        f.timeSec.push_back(static_cast<double>(i) * 0.005);
        f.midiF0.push_back(i < 100 ? 60.0 : 62.0);
        f.rms.push_back(0.5);
    }
    return f;
}

VocalBlock make(const VocalFeatures& f, size_t begin, size_t end, bool consonant = false)
{
    VocalBlock b;
    b.beginFrame = begin;
    b.endFrame = end;
    b.startSec = f.timeSec[begin];
    b.endSec = f.timeSec[end - 1] + 0.005;
    b.isConsonant = consonant;
    b.baseMidi = 60;
    return b;
}
} // namespace

int main()
{
    const auto f = makeFeatures();

    {
        std::vector<VocalBlock> blocks { make(f, 0, 200) };
        blocks[0].lyric = "가";
        check(splitBlockAt(blocks, f, 100), "split at an interior frame succeeds");
        check(blocks.size() == 2 && blocks[0].endFrame == 100 && blocks[1].beginFrame == 100, "split yields two contiguous blocks");
        check(blocks[0].baseMidi == 60 && blocks[1].baseMidi == 62, "split recomputes pitch per piece");
        check(! blocks[0].isContinuation && blocks[1].isContinuation && blocks[1].lyric.empty(), "right piece is a continuation without lyric");
        check(blocks[0].lyric == "가" && blocks[0].id == 0 && blocks[1].id == 1, "left keeps lyric; ids renumbered");
        check(blocks[1].startSec == f.timeSec[100], "right piece start time follows its first frame");
    }
    {
        std::vector<VocalBlock> blocks { make(f, 0, 200) };
        check(! splitBlockAt(blocks, f, 3) && ! splitBlockAt(blocks, f, 197), "split refuses pieces shorter than the minimum");
        check(! splitBlockAt(blocks, f, 0) && ! splitBlockAt(blocks, f, 250), "split refuses edges and out-of-range frames");
        check(blocks.size() == 1, "failed split leaves blocks untouched");
    }
    {
        std::vector<VocalBlock> blocks { make(f, 0, 200, true) };
        check(! splitBlockAt(blocks, f, 100), "consonant blocks cannot be split");
    }
    {
        std::vector<VocalBlock> blocks { make(f, 0, 100), make(f, 100, 200) };
        blocks[1].isContinuation = true;
        check(findJointNear(blocks, 103, 5) == std::optional<size_t>(0), "joint found within tolerance");
        check(! findJointNear(blocks, 120, 5), "no joint outside tolerance");
        check(joinBlocksAt(blocks, f, 0), "join adjacent voiced blocks");
        check(blocks.size() == 1 && blocks[0].beginFrame == 0 && blocks[0].endFrame == 200, "joined block spans both");
        check(blocks[0].baseMidi == 61 && ! blocks[0].isContinuation, "joined pitch is the median of the union");
    }
    {
        std::vector<VocalBlock> blocks { make(f, 0, 80), make(f, 120, 200) };
        check(! findJointNear(blocks, 100, 50), "a gap wider than the maximum is not a joint");
        check(! joinBlocksAt(blocks, f, 0), "join refuses a wide gap");
    }
    {
        std::vector<VocalBlock> blocks { make(f, 0, 100), make(f, 100, 200) };
        check(editBlocksAt(blocks, f, 101, 4) == BlockEdit::Join, "edit near a joint joins");
        check(editBlocksAt(blocks, f, 100, 4) == BlockEdit::Split && blocks.size() == 2, "edit away from a joint splits (round trip)");
        check(editBlocksAt(blocks, f, 1000, 4) == BlockEdit::None, "edit outside any block does nothing");
    }
    {
        // 자음 + 모음: 모음을 자르면 뒤 자음은 오른쪽 조각의 음을 따른다
        std::vector<VocalBlock> blocks { make(f, 0, 20, true), make(f, 20, 140), make(f, 140, 160, true) };
        check(splitBlockAt(blocks, f, 100), "split a vowel that has consonants around it");
        check(blocks.size() == 4 && blocks[3].isConsonant && blocks[3].baseMidi == blocks[2].baseMidi, "trailing consonant follows the right piece");
        check(blocks[0].baseMidi == blocks[1].baseMidi, "leading consonant follows the left piece");
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
