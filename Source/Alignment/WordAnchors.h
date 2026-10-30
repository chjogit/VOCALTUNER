#pragma once
#include <string>
#include <vector>
#include "TimeWarp.h"

// Whisper 단어 타임스탬프로 두 트랙의 같은 가사 위치를 짝지어 DTW의 길잡이로 쓴다. JUCE 비의존.
struct TimedWord
{
    std::string text;       // UTF-8
    double startSec = 0.0;  // 곡 시간
    double endSec = 0.0;
};

struct WordAnchor
{
    double referenceSec = 0.0;
    double targetSec = 0.0;
};

// 글자 단위로 두 가사를 맞추고(LCS), 같은 글자의 중심 시각을 짝으로 돌려준다.
// 너무 긴 입력(maxChars 초과)은 빈 결과. 시간 순서가 어긋난 이상치는 제거한다.
std::vector<WordAnchor> matchWords(const std::vector<TimedWord>& reference, const std::vector<TimedWord>& target,
                                   size_t maxChars = 4000, double outlierSec = 0.6);

// 레퍼런스 곡 시간 -> 타깃 곡 시간. 앵커가 minAnchors보다 적으면 empty.
TimeMap guideFromAnchors(const std::vector<WordAnchor>&, size_t minAnchors = 8);
