#include "../Source/Alignment/PhraseTiming.h"
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

// 음절마다 음량이 솟고 음이 바뀌는 가짜 노래. 음절 길이가 조금씩 달라 리듬이 되풀이되지 않는다
struct Note { double start, end, midi; };

std::vector<Note> melody()
{
    std::vector<Note> notes;
    double t = 1.0;
    for (int phrase = 0; phrase < 4; ++phrase)
    {
        for (int s = 0; s < 9; ++s)
        {
            const auto length = 0.18 + 0.07 * ((s * 5 + phrase * 3) % 4);
            notes.push_back({ t, t + length - 0.03, 60.0 + ((s * 7 + phrase) % 5) * 2.0 });
            t += length;
        }
        t += 1.0;   // 프레이즈 사이 쉼
    }
    return notes;
}

VocalFeatures render(const std::vector<Note>& notes, double seconds, double transpose = 0.0, double delay = 0.0)
{
    VocalFeatures f;
    const auto n = static_cast<size_t>(seconds / 0.005);
    f.timeSec.resize(n);
    f.midiF0.assign(n, 0.0);
    f.rms.assign(n, 0.0);
    for (size_t i = 0; i < n; ++i)
    {
        const auto t = static_cast<double>(i) * 0.005;
        f.timeSec[i] = t;
        for (const auto& note : notes)
        {
            const auto local = t - delay - note.start;
            if (local < 0.0 || t - delay > note.end)
                continue;
            f.midiF0[i] = note.midi + transpose;
            f.rms[i] = 0.3 * std::exp(-local / 0.15) + 0.05;   // 음절 시작에서 솟는다
        }
    }
    return f;
}

double shiftAt(const TimeMap& map, double t) { return map.map(t) - t; }

void testIdentical()
{
    const auto ref = render(melody(), 30.0);
    const auto map = phraseTimeMap(ref, 0.0, ref, 0.0);
    check(! map.empty() && std::abs(shiftAt(map, 5.0)) < 0.011, "identical tracks: no shift");
}

// 문장 중간(다섯째 음절)부터 4반음 위 화음으로 들어오고 0.12초 늦었다 -> 타깃 -> 기준 이동량 -0.12
void testMidPhraseHarmony()
{
    const auto notes = melody();
    const auto ref = render(notes, 30.0);
    std::vector<Note> late(notes.begin() + 13, notes.begin() + 18);   // 둘째 프레이즈 가운데부터 끝까지
    const auto tgt = render(late, 30.0, 4.0, 0.12);
    const auto map = phraseTimeMap(ref, 0.0, tgt, 0.0);
    const auto middle = 0.5 * (late.front().start + late.back().end) + 0.12;
    check(! map.empty() && std::abs(shiftAt(map, middle) + 0.12) < 0.02, "mid-phrase harmony (+4 st, 0.12 s late) is pulled back");
}

void testOffsets()
{
    const auto notes = melody();
    const auto ref = render(notes, 30.0);
    std::vector<Note> late(notes.begin() + 22, notes.begin() + 27);
    for (auto& n : late) { n.start -= 2.0; n.end -= 2.0; }   // 타깃 클립은 곡 2초에서 시작한다
    const auto tgt = render(late, 30.0, 7.0, -0.08);
    const auto map = phraseTimeMap(ref, 0.0, tgt, 2.0);
    const auto middle = 0.5 * (late.front().start + late.back().end) - 0.08 + 2.0;
    check(! map.empty() && std::abs(shiftAt(map, middle) - 0.08) < 0.02, "song offsets: early harmony (+7 st) is pushed later");
}

void testNothingToMatch()
{
    const auto ref = render(melody(), 30.0);
    VocalFeatures silent = render({}, 30.0);
    check(phraseTimeMap(ref, 0.0, silent, 0.0).empty(), "silent target gives an empty map");
    check(phraseTimeMap(ref, 0.0, VocalFeatures {}, 0.0).empty(), "empty target gives an empty map");
}
} // namespace

int main()
{
    testIdentical();
    testMidPhraseHarmony();
    testOffsets();
    testNothingToMatch();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
