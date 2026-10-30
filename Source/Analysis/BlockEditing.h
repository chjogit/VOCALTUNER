#pragma once
#include <cstddef>
#include <optional>
#include <vector>
#include "BlockParser.h"

// 사용자의 블롭 편집(자르기 / 붙이기). JUCE 비의존(단위 테스트용).
// 자음 블록은 편집 대상이 아니다. 편집 후에는 id를 다시 매기고 피치/자음 정보를 갱신한다.
struct EditOptions
{
    size_t minPieceFrames = 6;      // 자른 조각의 최소 길이 (30 ms)
    size_t maxJoinGapFrames = 8;    // 붙일 수 있는 두 블록 사이 최대 틈 (40 ms)
};

// frame 위치에서 유성 블록을 둘로 자른다. 자를 수 없으면 false (blocks 불변).
bool splitBlockAt(std::vector<VocalBlock>& blocks, const VocalFeatures& features, size_t frame,
                  const EditOptions& options = {});

// frame에서 tolerance 이내에 있는 두 유성 블록의 경계를 찾아 왼쪽 블록 인덱스를 반환.
std::optional<size_t> findJointNear(const std::vector<VocalBlock>& blocks, size_t frame, size_t toleranceFrames,
                                    const EditOptions& options = {});

// blocks[leftIndex]와 blocks[leftIndex + 1]을 하나로 합친다. 합칠 수 없으면 false.
bool joinBlocksAt(std::vector<VocalBlock>& blocks, const VocalFeatures& features, size_t leftIndex,
                  const EditOptions& options = {});

enum class BlockEdit { None, Split, Join };

// 표시 시간 기준으로 displaySec에서 toleranceSec 이내인 두 유성 블록의 경계를 찾아 왼쪽 블록 인덱스를 반환.
std::optional<size_t> findJointNearDisplayTime(const std::vector<VocalBlock>& blocks, double displaySec, double toleranceSec,
                                               const EditOptions& options = {});

// 화면(표시 시간) 기준 버전: 시간 편집이 적용된 블롭 위치에서 자르거나 붙인다.
BlockEdit editBlocksAtDisplayTime(std::vector<VocalBlock>& blocks, const VocalFeatures& features, double displaySec,
                                  double toleranceSec, const EditOptions& options = {});

// 경계 근처면 붙이고, 아니면 그 위치에서 자른다.
BlockEdit editBlocksAt(std::vector<VocalBlock>& blocks, const VocalFeatures& features, size_t frame,
                       size_t toleranceFrames, const EditOptions& options = {});
