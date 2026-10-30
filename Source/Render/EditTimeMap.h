#pragma once
#include <vector>
#include "BlockParser.h"

// 편집된 표시 시각 <-> 원본 소스 시각 (블록 경계를 마디로 하는 조각 선형 대응). JUCE 비의존.
// 블록 사이의 빈 구간(숨, 잔향 등)은 앞뒤 블록 경계 사이에서 늘이거나 줄인다.
class EditTimeMap
{
public:
    explicit EditTimeMap(const std::vector<VocalBlock>& blocks);

    double toSource(double displaySec) const noexcept;
    double toDisplay(double sourceSec) const noexcept;
    bool isIdentity() const noexcept { return identity; }

private:
    std::vector<double> display, source;   // 같은 길이, 각각 비감소
    bool identity = true;
};
