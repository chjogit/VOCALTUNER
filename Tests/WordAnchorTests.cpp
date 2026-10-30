#include "../Source/Alignment/WordAnchors.h"
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

std::vector<TimedWord> lyrics(const std::vector<std::string>& words, double start, double step, double scale = 1.0)
{
    std::vector<TimedWord> out;
    for (size_t i = 0; i < words.size(); ++i)
        out.push_back({ words[i], start + static_cast<double>(i) * step * scale, start + (static_cast<double>(i) + 0.8) * step * scale });
    return out;
}

const std::vector<std::string> song = { "안녕", "하세요", "오늘은", "날씨가", "정말", "좋네요", "우리", "함께", "노래해요", "Hello,", "World!", "라라라" };

void testMatchesShiftedLyrics()
{
    const auto ref = lyrics(song, 1.0, 0.5);
    const auto tgt = lyrics(song, 1.2, 0.5);
    const auto anchors = matchWords(ref, tgt);
    check(anchors.size() > 20, "같은 가사: 글자 앵커가 많이 나옴");
    bool allShifted = ! anchors.empty();
    for (const auto& a : anchors)
        allShifted = allShifted && std::abs(a.targetSec - a.referenceSec - 0.2) < 1e-6;
    check(allShifted, "모든 앵커가 +0.2초 이동을 가리킴");
    const auto guide = guideFromAnchors(anchors);
    check(! guide.empty() && std::abs(guide.map(3.0) - 3.2) < 1e-6, "길잡이 맵: 3.0 -> 3.2");
}

void testSurvivesRecognitionErrors()
{
    auto wrong = song;
    wrong[3] = "날씨는";
    wrong[7] = "";
    const auto anchors = matchWords(lyrics(song, 0.0, 0.5), lyrics(wrong, 0.4, 0.5));
    check(anchors.size() > 15, "인식 오류가 있어도 나머지는 짝지어짐");
    bool ok = true;
    for (const auto& a : anchors)
        ok = ok && std::abs(a.targetSec - a.referenceSec - 0.4) < 0.3;
    check(ok, "오류 구간이 이동량을 망치지 않음");
}

void testEdgeCases()
{
    check(matchWords({}, lyrics(song, 0, 0.5)).empty(), "빈 입력은 빈 결과");
    check(matchWords(lyrics({ "abc" }, 0, 1), lyrics({ "xyz" }, 0, 1)).empty(), "겹치는 글자 없음");
    check(guideFromAnchors({ { 1, 1 }, { 2, 2 } }).empty(), "앵커가 적으면 길잡이 없음");
    check(matchWords(lyrics(song, 0, 0.5), lyrics(song, 0, 0.5), 10).empty(), "너무 긴 입력은 건너뜀");
}
} // namespace

int main()
{
    testMatchesShiftedLyrics();
    testSurvivesRecognitionErrors();
    testEdgeCases();
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
