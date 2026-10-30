#pragma once
#include <memory>
#include <optional>
#include <vector>
#include "BlockParser.h"

// 블록별 편집 기록. 따로 저장하지 않고, 첫 분석 결과와 되돌리기 스냅샷에서 같은 구간의 블록을 찾아 꺼낸다.
using BlockSnapshot = std::shared_ptr<const std::vector<VocalBlock>>;

// 사용자 편집 값이 모두 같은가
bool sameEdits(const VocalBlock& a, const VocalBlock& b);

// structure의 구간/분석 값은 그대로 두고 편집 값만 edits에서 가져온다
VocalBlock withEditsOf(const VocalBlock& structure, const VocalBlock& edits);

// 같은 블록 = 같은 원본 프레임 구간 [beginFrame, endFrame)
const VocalBlock* findSameBlock(const std::vector<VocalBlock>& blocks, const VocalBlock& block);

// 처음으로: 첫 분석 때의 편집 값 (기초 보정만 baseTidy로). 자르기로 구간이 바뀐 블록은 편집을 모두 지운 상태.
VocalBlock firstStateOf(const VocalBlock& current, const std::vector<VocalBlock>& original, double baseTidy);

// 한 단계 되돌리기: 기록을 최근부터 거슬러 이 블록의 편집이 지금과 다른 첫 상태. 없으면 nullopt
std::optional<VocalBlock> previousStateOf(const VocalBlock& current, const std::vector<BlockSnapshot>& undoStack);
