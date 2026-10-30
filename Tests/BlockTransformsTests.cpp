#include "../Source/Analysis/BlockEditing.h"
#include "../Source/Analysis/BlockTransforms.h"
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

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

VocalFeatures flatFeatures(size_t frames, double pitch = 60.0)
{
    VocalFeatures f;
    for (size_t i = 0; i < frames; ++i)
    {
        f.timeSec.push_back(static_cast<double>(i) * 0.005);
        f.midiF0.push_back(pitch);
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
    b.baseMidi = static_cast<int>(f.midiF0[begin]);
    b.tidyAmount = 0.0;   // 정확한 값 비교를 위해 기본 정돈은 끈다 (기본값 자체는 아래에서 따로 검사)
    return b;
}
} // namespace

int main()
{
    const auto f = flatFeatures(400);

    // --- 시간 매핑
    {
        auto b = make(f, 100, 200);                       // 0.5 .. 1.0 s
        b.startOffsetSec = 0.1;
        b.endOffsetSec = 0.3;                             // 표시 0.6 .. 1.3 s
        check(near(sourceToDisplaySec(b, 0.5), 0.6) && near(sourceToDisplaySec(b, 1.0), 1.3), "source->display maps both ends");
        check(near(displayToSourceSec(b, sourceToDisplaySec(b, 0.77)), 0.77), "display->source is the inverse");
        check(near(b.displayCenterMidi(), 60.0), "display centre without shift");
        b.pitchShift = 2.0;
        check(near(blockPitchAt(f, b, 150), 62.0), "pitch at frame includes the block shift");
        b.curveOffset.assign(100, 0.5f);
        check(near(blockPitchAt(f, b, 150), 62.5) && blockPitchAt(f, b, 50) == 0.0, "pitch includes curve offset; outside frames are 0");
    }

    // --- 볼륨 이음새
    {
        std::vector<VocalBlock> blocks { make(f, 0, 100), make(f, 100, 200) };
        blocks[1].gainDb = 12.0;
        const auto joint = 0.5;
        check(near(effectiveGainDb(blocks, 0, 0.1), 0.0) && near(effectiveGainDb(blocks, 1, 0.9), 12.0), "gain is untouched far from the joint");
        check(near(effectiveGainDb(blocks, 0, joint), 6.0) && near(effectiveGainDb(blocks, 1, joint), 6.0), "both sides agree at the joint (midpoint)");
        const auto before = effectiveGainDb(blocks, 0, joint - 0.01);
        const auto after = effectiveGainDb(blocks, 1, joint + 0.01);
        check(before > 0.0 && before < 6.0 && after > 6.0 && after < 12.0, "gain ramps smoothly across the joint");
        check(near(effectiveGainDb(blocks, 0, joint - 0.01), effectiveGainDb(blocks, 0, joint - 0.01)) && effectiveGainDb(blocks, 0, joint - 0.039) < 0.1, "transition is limited to 40 ms");
        std::vector<VocalBlock> apart { make(f, 0, 80), make(f, 120, 200) };
        apart[1].gainDb = 12.0;
        check(near(effectiveGainDb(apart, 0, 0.39), 0.0), "blocks that do not touch are not blended");
    }

    // --- 타이밍
    {
        std::vector<VocalBlock> blocks { make(f, 0, 100), make(f, 120, 200), make(f, 220, 300) };   // 0..0.5, 0.6..1.0, 1.1..1.5
        const auto moved = movedInTime(blocks, 1, 0.05);
        check(near(moved.displayStartSec(), 0.65) && near(moved.displayEndSec(), 1.05), "move shifts both ends");
        check(near(movedInTime(blocks, 1, 0.5).displayEndSec(), 1.1), "move stops at the next block");
        check(near(movedInTime(blocks, 1, -0.5).displayStartSec(), 0.5), "move stops at the previous block");
        check(near(movedInTime(blocks, 0, -1.0).displayStartSec(), 0.0), "move stops at time zero");

        const auto front = resizedInTime(blocks, 1, BlockEdge::Front, -0.05);
        check(near(front.displayStartSec(), 0.55) && near(front.displayEndSec(), 1.0), "front handle extends the start only");
        check(near(resizedInTime(blocks, 1, BlockEdge::Front, -9.0).displayStartSec(), 0.5), "front handle stops at the previous block");
        check(near(resizedInTime(blocks, 1, BlockEdge::Front, 9.0).displayStartSec(), 1.0 - minBlockSeconds), "front handle keeps the minimum length");
        check(near(resizedInTime(blocks, 1, BlockEdge::Back, 9.0).displayEndSec(), 1.1), "back handle stops at the next block");
        check(near(resizedInTime(blocks, 1, BlockEdge::Back, -9.0).displayEndSec(), 0.6 + minBlockSeconds), "back handle keeps the minimum length");
    }

    // --- F0 곡선 핸들
    {
        VocalFeatures g;
        const double points[] = { 60, 64, 60, 64, 60 };                 // 극값 사이 30프레임
        for (size_t i = 0; i < 121; ++i)
        {
            const auto seg = std::min<size_t>(i / 30, 3);
            const auto t = static_cast<double>(i - seg * 30) / 30.0;
            g.timeSec.push_back(static_cast<double>(i) * 0.005);
            g.midiF0.push_back(points[seg] + (points[seg + 1] - points[seg]) * t);
            g.rms.push_back(0.5);
        }
        const auto b = make(g, 0, 121);
        const auto handles = findCurveHandles(g, b);
        check(handles.size() == 3, "zigzag finds the three interior turning points");
        check(handles.size() == 3 && std::abs(static_cast<int>(handles[0]) - 30) <= 3 && std::abs(static_cast<int>(handles[1]) - 60) <= 3, "handles sit at the turning frames");

        const auto flat = flatFeatures(100);
        check(findCurveHandles(flat, make(flat, 0, 100)).empty(), "a flat curve has no handles");

        if (handles.size() == 3)
        {
            const auto dragged = withCurveHandleDragged(b, handles, 1, 1.0);
            check(near(dragged.curveOffset[handles[1] - b.beginFrame], 1.0), "dragged handle moves by the full delta");
            check(near(dragged.curveOffset[handles[0] - b.beginFrame], 0.0) && near(dragged.curveOffset[handles[2] - b.beginFrame], 0.0), "neighbouring handles stay put");
            check(near(dragged.curveOffset[0], 0.0) && near(dragged.curveOffset[120], 0.0), "block ends stay put");
            const auto mid = (handles[0] + handles[1]) / 2 - b.beginFrame;
            check(dragged.curveOffset[mid] > 0.4 && dragged.curveOffset[mid] < 0.6, "offset eases between handles");
            const auto again = withCurveHandleDragged(dragged, handles, 1, 0.5);
            check(near(again.curveOffset[handles[1] - b.beginFrame], 1.5), "repeated drags accumulate");
        }
    }

    // --- 자르기/붙이기가 편집을 이어 받는지
    {
        auto b = make(f, 0, 200);                          // 0..1.0 s
        b.pitchShift = 2.0;
        b.gainDb = -6.0;
        b.startOffsetSec = 0.2;
        b.endOffsetSec = 0.6;                              // 표시 0.2 .. 1.6
        b.curveOffset.assign(200, 0.0f);
        b.curveOffset[10] = 1.0f;
        b.curveOffset[150] = 2.0f;
        std::vector<VocalBlock> blocks { b };

        check(editBlocksAtDisplayTime(blocks, f, 0.9, 0.01) == BlockEdit::Split, "display-time cut splits the stretched block");
        check(blocks.size() == 2 && blocks[0].endFrame == 100 && blocks[1].beginFrame == 100, "cut lands on the mapped source frame (display 0.9 -> source 0.5)");
        check(near(blocks[0].displayEndSec(), 0.9, 1e-6) && near(blocks[1].displayStartSec(), 0.9, 1e-6), "timing edit continues across the cut");
        check(near(blocks[0].displayStartSec(), 0.2) && near(blocks[1].displayEndSec(), 1.6), "outer ends keep their timing");
        check(blocks[0].pitchShift == 2.0 && blocks[1].gainDb == -6.0, "pitch shift and gain are inherited");
        check(blocks[0].curveOffset.size() == 100 && blocks[1].curveOffset.size() == 100 && blocks[1].curveOffset[50] == 2.0f, "curve offsets are sliced");

        check(editBlocksAtDisplayTime(blocks, f, 0.905, 0.02) == BlockEdit::Join, "display-time edit near the joint joins");
        check(blocks.size() == 1 && blocks[0].curveOffset.size() == 200 && blocks[0].curveOffset[10] == 1.0f && blocks[0].curveOffset[150] == 2.0f, "join concatenates curve offsets");
        check(near(blocks[0].displayStartSec(), 0.2) && near(blocks[0].displayEndSec(), 1.6) && near(blocks[0].pitchShift, 2.0), "join restores timing and pitch shift");
        check(editBlocksAtDisplayTime(blocks, f, 5.0, 0.01) == BlockEdit::None, "display-time edit outside any block does nothing");
    }

    // --- 음정 정렬 / 이음 / 평균 음정
    {
        auto b = make(f, 0, 100);
        b.pitchOffset = 0.3;                                   // 60.3 -> 60
        check(near(alignedToNoteCenter(b).displayCenterMidi(), 60.0), "align moves the blob to the nearest semitone centre");
        b.pitchOffset = -0.4;
        b.pitchShift = 5.0;                                    // 이전 이동은 무시하고 원래 음 기준으로 정렬
        check(near(alignedToNoteCenter(b).displayCenterMidi(), 60.0), "align ignores earlier shifts");
        auto c = make(f, 0, 100, true);
        c.pitchOffset = 0.3;
        check(near(alignedToNoteCenter(c).pitchShift, 0.0), "align leaves consonants alone");

        std::vector<VocalBlock> blocks { make(f, 0, 100), make(f, 100, 200) };      // 0..0.5, 0.5..1.0 맞닿음
        blocks[1].pitchShift = 4.0;
        check(findPitchJoints(blocks).size() == 1, "touching voiced blocks form a pitch joint");
        blocks[0].transitionAfter = 0.0;
        check(near(effectivePitchShift(blocks, 0, 0.49), 0.0) && near(effectivePitchShift(blocks, 1, 0.51), 4.0), "0% transition is a hard step at the joint");
        blocks[0].transitionAfter = 1.0;
        check(near(effectivePitchShift(blocks, 0, 0.5), 2.0) && near(effectivePitchShift(blocks, 1, 0.5), 2.0), "both sides agree at the joint");
        const auto narrow = [&](double amount)
        {
            blocks[0].transitionAfter = amount;
            return effectivePitchShift(blocks, 0, 0.45);
        };
        check(narrow(1.0) > narrow(0.5) && narrow(0.5) >= narrow(0.0), "a larger amount reaches further into the block");
        check(near(effectivePitchShift(blocks, 0, 0.1), 0.0) && near(effectivePitchShift(blocks, 1, 0.9), 4.0), "far from the joint the shift is unchanged");

        std::vector<VocalBlock> apart { make(f, 0, 60), make(f, 140, 200) };       // 틈 0.4 s
        apart[1].pitchShift = 4.0;
        check(findPitchJoints(apart).empty() && near(effectivePitchShift(apart, 0, 0.29), 0.0), "distant blocks are not connected");

        std::vector<VocalBlock> withConsonant { make(f, 0, 100), make(f, 100, 110, true), make(f, 110, 200) };
        withConsonant[2].pitchShift = 3.0;
        const auto joints = findPitchJoints(withConsonant);
        check(joints.size() == 1 && joints[0].left == 0 && joints[0].right == 2, "a consonant between two vowels is skipped");

        check(near(averagePitchMidi({ make(f, 0, 100), make(f, 100, 300) }), 60.0), "average pitch of a flat song");
        VocalFeatures g = flatFeatures(300, 62.0);
        check(near(averagePitchMidi({ make(f, 0, 100), make(g, 100, 300) }), (60.0 * 0.5 + 62.0 * 1.0) / 1.5), "average pitch is duration weighted");

        std::vector<VocalBlock> edited { make(f, 0, 100) };
        edited[0].pitchShift = 1.0;
        edited[0].curveOffset.assign(100, 0.5f);
        check(near(editedPitchAt(f, edited, 0, 50), 61.5) && editedPitchAt(f, edited, 0, 150) == 0.0, "editedPitchAt combines shift and curve offset");
    }

    // --- 여러 블롭을 한 덩어리로 (이웃은 밀려남)
    {
        std::vector<VocalBlock> blocks { make(f, 0, 50), make(f, 60, 110), make(f, 120, 170), make(f, 180, 230) };   // 각 0.25 s, 틈 0.05
        const auto result = [](const std::vector<BlockUpdate>& u, size_t index) -> const VocalBlock*
        {
            for (const auto& x : u)
                if (x.index == index)
                    return &x.block;
            return nullptr;
        };
        const auto moved = groupMovedInTime(blocks, { 1, 2 }, 0.02);
        check(moved.size() == 2 && near(result(moved, 1)->displayStartSec(), 0.32) && near(result(moved, 2)->displayStartSec(), 0.62), "group move shifts every selected block");

        const auto pushed = groupMovedInTime(blocks, { 1, 2 }, 0.2);   // 틈 0.05를 넘어선 만큼 마지막 블록이 밀림
        check(near(result(pushed, 3)->displayStartSec(), 0.9 + 0.15, 1e-5), "group move pushes the next block");
        check(near(result(pushed, 3)->displayEndSec() - result(pushed, 3)->displayStartSec(), 0.25, 1e-5), "pushed block keeps its length");
        check(result(pushed, 0) == nullptr, "untouched neighbours are not reported");

        const auto left = groupMovedInTime(blocks, { 2, 3 }, -0.2);
        check(near(result(left, 1)->displayEndSec(), 0.6 + 0.0 - 0.15 + 0.0, 1e-5) || result(left, 1)->displayEndSec() <= result(left, 2)->displayStartSec() + 1e-6, "group move pushes the previous block");
        check(near(groupMovedInTime(blocks, { 0, 1 }, -1.0).front().block.displayStartSec(), 0.0, 1e-5), "group move stops at time zero");
        const auto chain = groupMovedInTime(blocks, { 2, 3 }, -2.0);
        check(near(result(chain, 2)->displayStartSec(), 0.5, 1e-4) && result(chain, 1)->displayEndSec() <= result(chain, 2)->displayStartSec() + 1e-6,
              "pushing left stops when the first block reaches time zero");

        const auto stretched = groupScaledInTime(blocks, { 1, 2 }, BlockEdge::Back, 0.04);   // 0.3..0.85 -> 0.3..0.89
        const auto span = 0.85 - 0.30;
        const auto factor = (span + 0.04) / span;
        check(near(result(stretched, 1)->displayStartSec(), 0.30, 1e-5) && near(result(stretched, 2)->displayEndSec(), 0.89, 1e-5), "group scale moves only the dragged edge");
        check(near(result(stretched, 2)->displayStartSec(), 0.30 + (0.60 - 0.30) * factor, 1e-5), "group scale keeps the ratios inside the group");
        const auto shrunk = groupScaledInTime(blocks, { 1, 2 }, BlockEdge::Front, 0.1);    // 앞 가장자리 0.3 -> 0.4
        check(near(result(shrunk, 1)->displayStartSec(), 0.40, 1e-5) && near(result(shrunk, 2)->displayEndSec(), 0.85, 1e-5), "front handle scales from the far end");
        const auto crushed = groupScaledInTime(blocks, { 1, 2 }, BlockEdge::Back, -0.5);
        check(result(crushed, 1)->displayEndSec() - result(crushed, 1)->displayStartSec() >= minBlockSeconds - 1e-6, "group scale never makes a block shorter than the minimum");
        const auto into = groupScaledInTime(blocks, { 1, 2 }, BlockEdge::Back, 0.5);
        check(near(result(into, 2)->displayEndSec(), 0.85 + 0.5, 1e-5) && result(into, 3)->displayStartSec() >= result(into, 2)->displayEndSec() - 1e-6, "group scale pushes the next block");
    }

    // --- 비브라토
    {
        auto b = make(f, 0, 200);   // 1 s, 평탄한 60
        check(vibratoOffsetAt(f, b, 100) == 0.0, "no vibrato offset at depth 0");
        b.vibratoDepth = 0.8;
        b.vibratoRateHz = 5.0;
        double lo = 0.0, hi = 0.0;
        for (size_t i = 0; i < 200; ++i)
        {
            const auto o = vibratoOffsetAt(f, b, i);
            lo = std::min(lo, o);
            hi = std::max(hi, o);
        }
        check(hi > 0.6 && hi <= 0.8 + 1e-9 && lo < -0.6 && lo >= -0.8 - 1e-9, "vibrato swings about the requested depth");
        check(std::abs(vibratoOffsetAt(f, b, 0)) < 0.02 && std::abs(vibratoOffsetAt(f, b, 199)) < 0.1, "vibrato fades in and out at the edges");
        std::vector<VocalBlock> blocks { b };
        check(near(editedPitchAt(f, blocks, 0, 50), 60.0 + vibratoOffsetAt(f, b, 50)), "editedPitchAt includes the vibrato");
        check(near(blockPitchAt(f, b, 50), 60.0 + vibratoOffsetAt(f, b, 50)), "blockPitchAt includes the vibrato");

        auto wobbly = flatFeatures(200);
        for (size_t i = 0; i < 200; ++i)
            wobbly.midiF0[i] = 60.0 + 0.5 * std::sin(0.9 * static_cast<double>(i));   // 거친 떨림
        auto w = make(wobbly, 0, 200);
        w.vibratoDepth = 0.6;
        double maxDev = 0.0;
        for (size_t i = 60; i < 140; ++i)
            maxDev = std::max(maxDev, std::abs(wobbly.midiF0[i] + vibratoOffsetAt(wobbly, w, i) - 60.0));
        check(maxDev <= 0.68, "vibrato replaces the original jitter with a clean wave");

        // 이미 비브라토가 있는 곡선(±1.0 반음, 위쪽으로 치우친 비대칭)은 모양을 살리고 위/아래 극값의 가운데에 맞춘다
        auto natural = flatFeatures(300);
        for (size_t i = 0; i < 300; ++i)
        {
            const auto s = std::sin(2.0 * 3.14159265358979 * 5.5 * static_cast<double>(i) * 0.005);
            natural.midiF0[i] = 60.0 + (s > 0 ? 1.2 * s : 0.8 * s);
        }
        auto n = make(natural, 0, 300);
        n.vibratoDepth = 0.5;
        double lowest = 1e9, highest = -1e9;
        for (size_t i = 100; i < 200; ++i)
        {
            const auto v = natural.midiF0[i] + vibratoOffsetAt(natural, n, i);
            lowest = std::min(lowest, v);
            highest = std::max(highest, v);
        }
        check(std::abs(0.5 * (highest + lowest) - 60.2) < 0.15, "existing vibrato is centred on the middle of its range");
        check(0.5 * (highest - lowest) > 0.35 && 0.5 * (highest - lowest) < 0.7, "existing vibrato is scaled to the requested depth");
        n.vibratoDepth = 1.5;
        double widest = 0.0;
        for (size_t i = 100; i < 200; ++i)
            widest = std::max(widest, std::abs(natural.midiF0[i] + vibratoOffsetAt(natural, n, i) - 60.2));
        check(widest <= 1.5 * 1.0 + 0.1, "existing vibrato never overshoots far beyond its own range");
    }

    // --- 여러 트랙 동시 편집
    {
        std::vector<VocalBlock> self { make(f, 0, 50), make(f, 60, 110) };            // 0..0.25, 0.3..0.55
        std::vector<VocalBlock> other { make(f, 10, 60), make(f, 70, 120), make(f, 200, 250) };   // 0.05.., 0.35.., 1.0..
        auto moved = self[1];
        moved.pitchShift = 2.0;
        moved.gainDb = -3.0;
        moved.startOffsetSec = 0.1;
        moved.endOffsetSec = 0.1;
        const auto edit = makeLinkedEdit({ { self[1], moved } });
        check(near(edit.oldStart, 0.3) && near(edit.newStart, 0.4) && near(edit.pitchShift, 2.0), "linked edit captures span and deltas");
        const auto updates = applyLinkedEdit(other, edit);
        check(updates.size() >= 1 && updates[0].index == 1 && near(updates[0].block.pitchShift, 2.0) && near(updates[0].block.gainDb, -3.0), "overlapping block of another track gets the same edit");
        check(near(updates[0].block.displayStartSec(), 0.35 + 0.1, 1e-6), "linked timing shifts the same amount");
        bool untouchedFar = true;
        for (const auto& u : updates)
            untouchedFar = untouchedFar && u.index != 2;
        check(untouchedFar, "non-overlapping blocks are not touched");
        VocalBlock same = self[1];
        check(applyLinkedEdit(other, makeLinkedEdit({ { same, same } })).empty(), "an edit that changes nothing is not mirrored");
    }

    // --- F0 펴기
    {
        auto wobbly = flatFeatures(100);
        for (size_t i = 0; i < 100; ++i)
            wobbly.midiF0[i] = 60.0 + (i % 2 == 0 ? 0.4 : -0.4);
        auto b = make(wobbly, 0, 100);
        b.pitchOffset = 0.0;
        check(near(VocalBlock().flattenAmount, 0.0) && near(VocalBlock().tidyAmount, defaultTidyAmount) && near(defaultTidyAmount, 0.05), "default auto-correction is a 5% tidy, flatten is off");
        b.tidyAmount = 0.01;
        b.pitchOffset = 0.3;
        check(near(tidyOffsetAt(wobbly, b, 0), -0.003) && near(tidyOffsetAt(wobbly, b, 1), -0.003),
              "1% pitch center moves the whole blob slightly toward the nearest note (shape kept)");
        b.pitchOffset = 0.0;
        {
            PitchScale cMajor { 0, false };
            check(nearestScaleNote(60.8, cMajor) == 60 && nearestScaleNote(61.2, cMajor) == 62 && nearestScaleNote(61.6, {}) == 62
                  && nearestScaleNote(65.6, cMajor) == 65 && nearestScaleNote(70.0, PitchScale { 9, true }) == 69,
                  "scale snapping picks the nearest note of the key");
            auto off = b;
            off.baseMidi = 61;
            off.pitchOffset = -0.2;    // 60.8 (가장 가까운 반음은 61) -> C장조에서는 60
            PitchBalance balance;
            balance.center = 1.0;
            balance.scale = cMajor;
            const auto updates = withPitchBalance({ off }, { 0 }, balance);
            check(updates.size() == 1 && updates[0].block.centerTargetMidi == 60 && near(tidyOffsetAt(wobbly, updates[0].block, 0), -0.8),
                  "100% pitch center in C major moves 60.8 to 60, not C#");
            balance.scale = {};
            balance.toleranceCents = 25.0;
            off.pitchOffset = 0.2;
            check(near(withPitchBalance({ off }, { 0 }, balance)[0].block.tidyAmount, 0.0), "blobs within the tolerance are left alone");

            // 느린 흐름(0.5 반음 기울기) + 빠른 비브라토: 흐름만 줄고 비브라토 폭은 남는다
            auto drifting = flatFeatures(200);
            for (size_t i = 0; i < 200; ++i)
                drifting.midiF0[i] = 60.0 + 0.5 * (static_cast<double>(i) / 199.0 - 0.5) + 0.3 * std::sin(2.0 * 3.14159265358979 * 5.5 * drifting.timeSec[i]);
            auto d = make(drifting, 0, 200);
            d.tidyAmount = 0.0;
            d.baseMidi = 60;
            d.pitchOffset = 0.0;
            d.driftAmount = 1.0;
            const auto endLift = blockPitchAt(drifting, d, 180) - blockPitchAt(drifting, d, 20);
            const auto original = drifting.midiF0[180] - drifting.midiF0[20];
            std::printf("  drift: original span %.3f -> corrected %.3f\n", original, endLift);
            double swing = 0.0;
            for (size_t i = 90; i < 110; ++i)
                swing = std::max(swing, std::abs(blockPitchAt(drifting, d, i) - 60.0));
            check(std::abs(endLift) < 0.6 * std::abs(original) + 0.1 && swing > 0.2, "pitch drift removes the slow slope but keeps vibrato");
        }
        {
            std::vector<VocalBlock> two { b, b };
            two[1].tidyAmount = 0.0;
            const auto off = withBaseCorrection(two, false);
            check(off.size() == 1 && off[0].index == 0 && near(off[0].block.tidyAmount, 0.0), "base correction off clears only the tidy blocks");
            const auto on = withBaseCorrection(two, true);
            check(on.size() == 1 && on[0].index == 1 && near(on[0].block.tidyAmount, defaultTidyAmount), "base correction on restores the default");
        }
        b.tidyAmount = 0.0;
        b.flattenAmount = 1.0;
        check(near(blockPitchAt(wobbly, b, 0), 60.0) && near(blockPitchAt(wobbly, b, 1), 60.0), "100% flatten is a straight line");
        b.flattenAmount = 0.0;
        check(near(blockPitchAt(wobbly, b, 0), 60.4), "0% keeps the original curve");
        b.flattenAmount = 1.0;
        b.vibratoDepth = 0.5;
        double peak = 0.0;
        for (size_t i = 30; i < 70; ++i)
            peak = std::max(peak, std::abs(blockPitchAt(wobbly, b, i) - 60.0));
        check(peak > 0.35 && peak < 0.55, "vibrato rides on the flattened curve");
    }

    // --- 굳히기: 펴기 -> 비브라토를 따로 굳히면 같은 결과가 이어진다
    {
        auto wobbly = flatFeatures(200);
        for (size_t i = 0; i < 200; ++i)
            wobbly.midiF0[i] = 60.0 + 0.5 * std::sin(0.4 * static_cast<double>(i));
        auto b = make(wobbly, 0, 200);
        b.flattenAmount = 0.5;
        const auto before = wobbly.midiF0[50] + flattenOffsetAt(wobbly, b, 50);
        const auto baked = withShapeBaked(wobbly, b);
        check(near(baked.flattenAmount, 0.0) && baked.curveOffset.size() == 200, "baking clears the flatten amount and stores a curve edit");
        const std::vector<VocalBlock> list { baked };
        check(near(editedPitchAt(wobbly, list, 0, 50), before), "baking keeps the sound identical");
        // 굳힌 곡선 위에서 다시 50% 펴면 남은 흔들림의 절반이 더 줄어든다 (원래 곡선이 아니라 굳힌 곡선이 기준)
        auto again = baked;
        again.flattenAmount = 0.5;
        const auto centre = again.baseMidi + again.pitchOffset;
        const auto working = workingMidiAt(wobbly, again, 50);
        check(near(editedPitchAt(wobbly, std::vector<VocalBlock>{ again }, 0, 50), working + 0.5 * (centre - working)), "next tool works on the baked curve");
        auto both = b;
        both.vibratoDepth = 0.4;
        const auto stepwise = withShapeBaked(wobbly, withShapeBaked(wobbly, b));
        check(near(stepwise.flattenAmount, 0.0) && near(stepwise.vibratoDepth, 0.0), "baking twice is stable");
        (void) both;
        {
            auto tidyOnly = make(wobbly, 0, 200);
            tidyOnly.tidyAmount = defaultTidyAmount;
            check(withShapeBaked(wobbly, tidyOnly).curveOffset.empty(), "blocks with only the default tidy are left alone");
        }
    }

    // --- 음정 맞춤은 트랙마다 자기 음정으로 (다른 트랙의 보정량을 따라가지 않음)
    {
        auto lead = make(f, 0, 100);
        lead.pitchOffset = 0.3;
        auto harmony = make(f, 0, 100);
        harmony.pitchOffset = -0.2;
        const auto snapped = alignedToNoteCenter(lead);
        const auto edit = makeLinkedEdit({ { lead, snapped } });
        check(edit.snapToNote, "a note-centre snap is recognised");
        const auto updates = applyLinkedEdit({ harmony }, edit);
        check(! updates.empty() && near(updates[0].block.pitchShift, alignedToNoteCenter(harmony).pitchShift), "the other track snaps to its own nearest note");
        auto dragged = lead;
        dragged.pitchShift = 0.77;
        check(! makeLinkedEdit({ { lead, dragged } }).snapToNote, "a plain pitch drag is still mirrored as a shift");
    }

    // --- 짧은 블롭에도 핸들 추가
    {
        auto tiny = make(f, 0, 10);   // 10프레임 (50 ms)
        const auto added = withUserHandleToggled(tiny, 5);
        check(added.userHandles.size() == 1 && added.userHandles[0] == 5, "handle can be added in a very short blob");
        check(withUserHandleToggled(tiny, 0).userHandles.empty(), "handle is not added at the very edge");
        check(withUserHandleToggled(make(f, 0, 2), 1).userHandles.empty(), "blobs under 3 frames take no handle");
    }

    // --- 곡선 핸들 추가 / 시간 이동
    {
        auto b = make(f, 0, 100);
        check(allCurveHandles(f, b) == std::vector<size_t>{ 0, 99 }, "a flat curve has only the start and end handles");
        const auto added = withUserHandleToggled(b, 40);
        check(added.userHandles == std::vector<size_t>{ 40 } && allCurveHandles(f, added) == std::vector<size_t>{ 0, 40, 99 }, "double-click adds a user handle");
        {
            const auto handles = allCurveHandles(f, b);
            const auto raised = withCurveHandleDragged(b, handles, 0, 1.0, 5.0);
            check(near(raised.curveOffset[0], 1.0) && near(raised.curveOffset[99], 0.0), "start handle moves only the start pitch");
            check(raised.curveWarp[0] > 0.0, "start handle can also move in time");
            const auto lowered = withCurveHandleDragged(b, handles, 1, -1.0, -5.0);
            check(near(lowered.curveOffset[99], -1.0) && lowered.curveWarp[99] < 0.0, "end handle moves pitch and time");
        }
        check(withUserHandleToggled(added, 42).userHandles.empty(), "double-click near a user handle removes it");
        check(withUserHandleToggled(b, 2).userHandles.empty() && withUserHandleToggled(b, 98).userHandles.empty(), "no handles right at the block ends");

        const std::vector<size_t> handles { 30, 60 };
        const auto moved = withCurveHandleDragged(b, handles, 0, 0.0, 10.0);
        check(near(moved.curveWarp[30], 10.0), "horizontal drag warps the handle frame");
        check(near(moved.curveWarp[0], 0.0) && near(moved.curveWarp[60], 0.0), "warp is zero at the neighbours");
        const auto limited = withCurveHandleDragged(b, handles, 0, 0.0, 500.0);
        check(limited.curveWarp[30] <= 0.6 * 30.0 + 1e-6, "horizontal drag is limited so the curve cannot fold over");
        check(near(curveFrameDisplaySec(f, moved, 30), f.timeSec[30] + 10.0 * 0.005), "curve frame display time includes the warp");
        const auto both = withCurveHandleDragged(b, handles, 0, 1.0, 5.0);
        check(near(both.curveOffset[30], 1.0) && near(both.curveWarp[30], 5.0), "vertical and horizontal edits combine");
    }

    // --- 자르기/붙이기가 새 편집도 이어 받는지
    {
        auto b = make(f, 0, 200);
        b.transitionAfter = 0.8;
        b.userHandles = { 30, 150 };
        b.curveWarp.assign(200, 0.0f);
        b.curveWarp[150] = 3.0f;
        std::vector<VocalBlock> blocks { b };
        check(editBlocksAtDisplayTime(blocks, f, 0.5, 0.001) == BlockEdit::Split, "split a block that carries the new edits");
        check(blocks[0].userHandles == std::vector<size_t>{ 30 } && blocks[1].userHandles == std::vector<size_t>{ 150 }, "user handles go to the piece that contains them");
        check(blocks[0].transitionAfter == 0.5 && blocks[1].transitionAfter == 0.8, "the new joint is 50%; the old joint amount moves to the right piece");
        check(blocks[1].curveWarp.size() == 100 && near(blocks[1].curveWarp[50], 3.0), "warp is sliced");
        check(editBlocksAtDisplayTime(blocks, f, 0.5, 0.01) == BlockEdit::Join, "rejoin");
        check(blocks[0].userHandles == std::vector<size_t>{ 30, 150 } && near(blocks[0].transitionAfter, 0.8) && near(blocks[0].curveWarp[150], 3.0), "join restores handles, transition and warp");
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
