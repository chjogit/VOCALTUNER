#pragma once
#include <vector>
#include "SectionClassifier.h"
#include "SpectralFrames.h"
#include "WordAnchors.h"

// 한 트랙의 정렬용 분석 결과 (정렬 기능을 켠 뒤에만 만들어진다)
struct AlignmentData
{
    SpectralFrames frames;
    std::vector<SectionKind> kinds;
    std::vector<TimedWord> words;   // Whisper 단어 (소스 시간). Whisper가 없거나 실패하면 비어 있음
    bool whisperUsed = false;
};
