#include "../Source/Alignment/ReferenceFit.h"
#include <cmath>
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

// 0.5-1.4 s 노래, 1.4-1.5 s 숨(소리 있음), 1.5-2.0 s 노래, 2.0-2.2 s 무음, 2.2-2.8 s 노래
VocalFeatures features()
{
    VocalFeatures f;
    for (size_t i = 0; i < 600; ++i)
    {
        const auto t = static_cast<double>(i) * 0.005;
        f.timeSec.push_back(t);
        const bool sung = (t >= 0.5 && t < 1.4) || (t >= 1.5 && t < 2.0) || (t >= 2.2 && t < 2.8);
        const bool breath = t >= 1.4 && t < 1.5;
        f.midiF0.push_back(sung ? 64.0 : 0.0);
        f.rms.push_back(sung ? 0.3 : breath ? 0.05 : 0.0);
    }
    return f;
}

VocalBlock block(double start, double end, double pitch = 0.0)
{
    VocalBlock b;
    b.startSec = start;
    b.endSec = end;
    b.beginFrame = static_cast<size_t>(start / 0.005);
    b.endFrame = static_cast<size_t>(end / 0.005);
    b.baseMidi = 64;
    b.pitchShift = pitch;
    return b;
}

std::vector<VocalBlock> baseline()
{
    return { block(0.5, 0.9), block(0.9, 1.4), block(1.5, 2.0), block(2.2, 2.5), block(2.5, 2.8) };
}

std::vector<BlockUpdate> moved(const std::vector<double>& moves)
{
    std::vector<BlockUpdate> updates;
    const auto base = baseline();
    for (size_t i = 0; i < base.size(); ++i)
    {
        auto b = base[i];
        b.startOffsetSec = moves[i];
        b.endOffsetSec = moves[i] * 1.5;   // 맞추기가 블록을 늘였다
        b.pitchShift = 0.25 * static_cast<double>(i);
        updates.push_back({ i, b });
    }
    return updates;
}

void testPhrases()
{
    auto updates = moved({ 0.02, 0.05, 0.06, -0.03, -0.01 });
    movePhrasesRigidly(updates, baseline(), features());
    // 앞 세 블록은 숨이 든 틈으로만 나뉘어 한 프레이즈 (중앙값 0.05), 뒤 두 블록은 무음 뒤 다른 프레이즈 (두 값 중 뒤쪽 -0.01)
    bool rigid = true;
    for (const auto& u : updates) rigid = rigid && std::abs(u.block.startOffsetSec - u.block.endOffsetSec) < 1e-12;
    check(rigid, "every block keeps its length");
    check(std::abs(updates[0].block.startOffsetSec - 0.05) < 1e-12 && std::abs(updates[2].block.startOffsetSec - 0.05) < 1e-12,
          "blocks split only by a breath move together by the median");
    check(std::abs(updates[3].block.startOffsetSec - updates[4].block.startOffsetSec) < 1e-12 && std::abs(updates[3].block.startOffsetSec + 0.01) < 1e-12,
          "a phrase after silence moves on its own");
    check(std::abs(updates[4].block.pitchShift - 1.0) < 1e-12, "pitch edits from the fit are kept");
}

void testNoOverlap()
{
    auto updates = moved({ 0.25, 0.25, 0.25, -0.1, -0.1 });
    movePhrasesRigidly(updates, baseline(), features());
    bool ordered = true;
    for (size_t i = 1; i < updates.size(); ++i) ordered = ordered && updates[i].block.displayStartSec() >= updates[i - 1].block.displayEndSec() - 1e-12;
    check(ordered, "a later phrase is pushed back instead of overlapping");
}
} // namespace

int main()
{
    testPhrases();
    testNoOverlap();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
