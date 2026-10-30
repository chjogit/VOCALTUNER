#include "../Source/Analysis/BlockParser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

// 프레임 단위로 이어 붙여 VocalFeatures를 만드는 도우미 (Harvest 없이 파서만 검사)
class FeatureBuilder
{
public:
    FeatureBuilder& silence(size_t frames) { return add(frames, [](size_t) { return 0.0; }, 0.0); }

    // 무성이지만 에너지가 있음 (자음/숨소리)
    FeatureBuilder& noise(size_t frames, double rms = 0.1) { return add(frames, [](size_t) { return 0.0; }, rms); }

    FeatureBuilder& note(size_t frames, double pitch, double vibratoDepth = 0.0, double level = 0.8)
    {
        const auto offset = frameCount;
        return add(frames, [=](size_t i)
        {
            return pitch + vibratoDepth * std::sin(2.0 * 3.14159265358979 * 5.5 * static_cast<double>(offset + i) * 0.005);
        }, level);
    }

    // from에서 to까지 일정하게 미끄러지는 음 (계단 분할이 잡지 못하는 완만한 이동)
    FeatureBuilder& glide(size_t frames, double from, double to)
    {
        return add(frames, [=](size_t i) { return from + (to - from) * static_cast<double>(i) / static_cast<double>(frames); }, 0.8);
    }

    VocalFeatures build() const
    {
        VocalFeatures f;
        f.framePeriodMs = 5.0;
        f.midiF0 = midiFrames;
        f.rms = rmsFrames;
        for (size_t i = 0; i < midiFrames.size(); ++i)
            f.timeSec.push_back(static_cast<double>(i) * 0.005);
        return f;
    }

private:
    template <class Pitch>
    FeatureBuilder& add(size_t frames, Pitch pitch, double level)
    {
        for (size_t i = 0; i < frames; ++i)
        {
            midiFrames.push_back(pitch(i));
            rmsFrames.push_back(level);
        }
        frameCount += frames;
        return *this;
    }

    std::vector<double> midiFrames, rmsFrames;
    size_t frameCount = 0;
};

size_t countVoiced(const std::vector<VocalBlock>& blocks)
{
    return static_cast<size_t>(std::count_if(blocks.begin(), blocks.end(), [](const auto& b) { return ! b.isConsonant; }));
}

void testSteadyNoteWithVibratoIsOneBlock()
{
    const auto blocks = parseBlocks(FeatureBuilder().silence(10).note(200, 60.0, 0.4).silence(10).build());
    check(blocks.size() == 1 && blocks[0].baseMidi == 60, "steady note with vibrato -> one block at MIDI 60");
    check(blocks.size() == 1 && std::abs(blocks[0].pitchOffset) < 0.1, "vibrato does not shift the median much");
    check(blocks.size() == 1 && blocks[0].beginFrame == 10 && blocks[0].endFrame == 210, "block covers exactly the voiced frames");
}

void testPitchStepSplits()
{
    const auto blocks = parseBlocks(FeatureBuilder().note(80, 60.0).note(80, 63.0).build());
    check(blocks.size() == 2, "3-semitone step -> two blocks");
    if (blocks.size() == 2)
    {
        check(blocks[0].baseMidi == 60 && blocks[1].baseMidi == 63, "blocks carry their own base notes");
        check(! blocks[0].isContinuation && blocks[1].isContinuation, "second block is marked as continuation");
        check(blocks[1].beginFrame >= 78 && blocks[1].beginFrame <= 82, "split lands at the step (+-2 frames)");
    }
}

void testGlideAndMultipleSteps()
{
    const auto blocks = parseBlocks(FeatureBuilder().note(70, 60.0).note(70, 64.0).note(70, 67.0).build());
    check(blocks.size() == 3, "two steps -> three blocks");
}

void testSlowGlideBetweenPitchesSplits()
{
    // 60에서 62로 200 ms 동안 천천히 이동: 기울기가 작아 계단 분할은 못 잡지만 두 음이 분명히 다르다
    const auto blocks = parseBlocks(FeatureBuilder().note(100, 60.0, 0.2).glide(40, 60.0, 62.0).note(100, 62.0, 0.2).build());
    check(countVoiced(blocks) == 2, "slow glide between two pitches -> forced split into two blocks");
    if (countVoiced(blocks) == 2)
        check(blocks[0].baseMidi == 60 && blocks[1].baseMidi == 62, "split blocks keep their own pitches");
    const auto wide = parseBlocks(FeatureBuilder().note(300, 60.0, 0.5).build());
    check(countVoiced(wide) == 1, "wide vibrato on one pitch is not split");
}

void testFractionalOffset()
{
    const auto blocks = parseBlocks(FeatureBuilder().note(100, 60.3).build());
    check(blocks.size() == 1 && blocks[0].baseMidi == 60 && std::abs(blocks[0].pitchOffset - 0.3) < 1e-6,
          "60.3 -> base 60, offset +0.3");
}

void testShortVoicedBlipIsDropped()
{
    const auto blocks = parseBlocks(FeatureBuilder().silence(20).note(8, 60.0).silence(20).build());
    check(blocks.empty(), "voiced blip shorter than 12 frames is dropped");
}

void testSilenceOnly()
{
    check(parseBlocks(FeatureBuilder().silence(500).build()).empty(), "silence -> no blocks");
    check(parseBlocks(VocalFeatures {}).empty(), "empty features -> no blocks");
    check(parseBlocks(FeatureBuilder().noise(300, 0.001).build()).empty(), "faint noise without voicing -> no blocks");
}

void testShortGapIsBridged()
{
    const auto blocks = parseBlocks(FeatureBuilder().note(60, 60.0).silence(5).note(60, 60.0).build());
    check(blocks.size() == 1 && blocks[0].beginFrame == 0 && blocks[0].endFrame == 125,
          "5-frame unvoiced gap inside a note is bridged");
}

void testLongGapSeparates()
{
    const auto blocks = parseBlocks(FeatureBuilder().note(60, 60.0).silence(20).note(60, 60.0).build());
    check(blocks.size() == 2, "20-frame gap separates two notes");
}

void testLeadingConsonant()
{
    const auto blocks = parseBlocks(FeatureBuilder().silence(30).noise(20).note(100, 62.0).silence(30).build());
    check(blocks.size() == 2, "leading noise -> consonant + vowel");
    if (blocks.size() == 2)
    {
        check(blocks[0].isConsonant && blocks[0].beginFrame == 30 && blocks[0].endFrame == 50, "consonant covers the 20 noisy frames");
        check(blocks[0].baseMidi == 62 && blocks[0].pitchOffset == 0.0, "consonant takes the following vowel's pitch, offset 0");
        check(! blocks[1].isConsonant && blocks[1].beginFrame == 50, "vowel starts right after the consonant");
    }
}

void testTrailingConsonant()
{
    const auto blocks = parseBlocks(FeatureBuilder().silence(30).note(100, 62.0).noise(15).silence(30).build());
    check(blocks.size() == 2 && blocks[1].isConsonant && blocks[1].endFrame - blocks[1].beginFrame == 15,
          "trailing noise -> trailing consonant");

    BlockOptions off;
    off.includeTrailingConsonants = false;
    check(parseBlocks(FeatureBuilder().silence(30).note(100, 62.0).noise(15).silence(30).build(), off).size() == 1,
          "trailing consonants can be disabled");
}

void testConsonantLimits()
{
    check(parseBlocks(FeatureBuilder().silence(30).noise(4).note(100, 62.0).build()).size() == 1,
          "noise shorter than 8 frames is not a consonant (RMS leakage guard)");

    const auto longNoise = parseBlocks(FeatureBuilder().silence(30).noise(80).note(100, 62.0).build());
    check(longNoise.size() == 2 && longNoise[0].endFrame - longNoise[0].beginFrame == 30,
          "consonant is capped at 30 frames; older noise is left out");
}

void testGapBetweenNotesIsSharedWithoutOverlap()
{
    const auto blocks = parseBlocks(FeatureBuilder().note(80, 60.0).noise(50).note(80, 64.0).build());
    check(blocks.size() == 4, "note + trailing + leading + note");

    bool ordered = true;
    for (size_t i = 1; i < blocks.size(); ++i)
        ordered = ordered && blocks[i].beginFrame >= blocks[i - 1].endFrame;
    check(ordered, "blocks never overlap and stay in time order");
    check(blocks.size() == 4 && blocks[2].isConsonant && blocks[2].endFrame - blocks[2].beginFrame == 30,
          "leading consonant of the next note has priority over the trailing one");
}

void testIdsAndTimes()
{
    const auto blocks = parseBlocks(FeatureBuilder().silence(10).noise(20).note(100, 60.0).build());
    bool idsOk = true;
    for (size_t i = 0; i < blocks.size(); ++i)
        idsOk = idsOk && blocks[i].id == static_cast<int>(i);
    check(idsOk, "ids are sequential from 0");
    check(blocks.size() == 2 && std::abs(blocks[1].startSec - 0.150) < 1e-9 && std::abs(blocks[1].endSec - 0.650) < 1e-9,
          "start/end seconds follow the frame grid (end exclusive)");
    check(countVoiced(blocks) == 1, "one voiced block");
}
} // namespace

int main()
{
    testSteadyNoteWithVibratoIsOneBlock();
    testPitchStepSplits();
    testGlideAndMultipleSteps();
    testSlowGlideBetweenPitchesSplits();
    testFractionalOffset();
    testShortVoicedBlipIsDropped();
    testSilenceOnly();
    testShortGapIsBridged();
    testLongGapSeparates();
    testLeadingConsonant();
    testTrailingConsonant();
    testConsonantLimits();
    testGapBetweenNotesIsSharedWithoutOverlap();
    testIdsAndTimes();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
