#pragma once
#include <vector>
#include "BlockParser.h"
#include "VocalFeatures.h"

// 편집된 표시 시각 <-> 원본 소스 시각 (블록 경계를 마디로 하는 조각 선형 대응). JUCE 비의존.
// 블록 사이의 빈 구간(숨, 잔향 등)은 앞뒤 블록 경계 사이에서 늘이거나 줄인다.
class EditTimeMap
{
public:
    explicit EditTimeMap(const std::vector<VocalBlock>& blocks);
    // features가 있으면 블록 경계 사이의 늘임·줄임을 안정된 유성 프레임(모음)에만 몰아 준다.
    // 무성(숨·자음·잡음), 소리가 서는 첫 40 ms, 음량이 갑자기 뛰는 곳(클릭·어택)은 길이를 그대로 두고 옮기기만 한다 (늘이면 거슬리지 않던 잡음·어택이 들린다).
    // 모음 몫은 0.2~3배까지이고, 그 밖의 남는 차이만 고정 조각이 고르게 나눠 맡는다.
    EditTimeMap(const std::vector<VocalBlock>& blocks, const VocalFeatures& features);

    double toSource(double displaySec) const noexcept;
    double toDisplay(double sourceSec) const noexcept;
    bool isIdentity() const noexcept { return identity; }

private:
    std::vector<double> display, source;   // 같은 길이, 각각 비감소
    bool identity = true;
};
