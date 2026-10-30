#include "../Source/Analysis/BlockHistory.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

VocalBlock block(size_t begin, size_t end, double shift = 0.0)
{
    VocalBlock b;
    b.beginFrame = begin;
    b.endFrame = end;
    b.baseMidi = 60;
    b.pitchShift = shift;
    return b;
}

BlockSnapshot snapshot(std::vector<VocalBlock> blocks)
{
    return std::make_shared<const std::vector<VocalBlock>>(std::move(blocks));
}

void testEditsCompareAndCopy()
{
    auto a = block(0, 100, 1.0);
    auto b = block(0, 100, 1.0);
    check(sameEdits(a, b), "identical edits compare equal");
    b.gainDb = -3.0;
    check(! sameEdits(a, b), "a gain change is an edit");

    auto structure = block(0, 100);
    structure.pitchOffset = 0.3;
    const auto copied = withEditsOf(structure, b);
    check(copied.pitchOffset == 0.3 && copied.gainDb == -3.0 && copied.pitchShift == 1.0,
          "edits are copied, analysis values are kept");
}

void testFirstState()
{
    const std::vector<VocalBlock> original { block(0, 100), block(100, 200) };
    auto edited = block(100, 200, 2.0);
    edited.vibratoDepth = 0.4;
    const auto first = firstStateOf(edited, original, 0.05);
    check(first.pitchShift == 0.0 && first.vibratoDepth == 0.0 && std::abs(first.tidyAmount - 0.05) < 1e-12,
          "back to first: edits cleared, base correction kept");
    check(std::abs(firstStateOf(edited, original, 0.0).tidyAmount) < 1e-12, "base correction off stays off");

    const auto cutPiece = block(100, 150, 1.5);
    const auto fresh = firstStateOf(cutPiece, original, 0.05);
    check(fresh.pitchShift == 0.0 && fresh.beginFrame == 100 && fresh.endFrame == 150,
          "a piece made by cutting keeps its range and loses its edits");
}

void testPreviousState()
{
    // 기록: 처음(0) -> 이 블록 +1 -> 다른 블록만 편집 -> 지금 이 블록 +2
    const auto s0 = snapshot({ block(0, 100), block(100, 200) });
    const auto s1 = snapshot({ block(0, 100, 1.0), block(100, 200) });
    const auto s2 = snapshot({ block(0, 100, 1.0), block(100, 200, 5.0) });
    const std::vector<BlockSnapshot> undo { s0, s1, s2 };

    const auto now = block(0, 100, 2.0);
    const auto previous = previousStateOf(now, undo);
    check(previous && previous->pitchShift == 1.0, "one step back skips steps that did not touch this block");

    const auto stepBack = previousStateOf(block(0, 100, 1.0), undo);
    check(stepBack && stepBack->pitchShift == 0.0, "the step before that goes to the first state");

    check(! previousStateOf(block(100, 200), { s0, s1 }), "an untouched block has no earlier state");
    check(! previousStateOf(block(0, 50, 1.0), undo), "history stops where the block was created by a cut");
    check(! previousStateOf(now, {}), "empty history -> nothing to undo");
}
} // namespace

int main()
{
    testEditsCompareAndCopy();
    testFirstState();
    testPreviousState();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
