#pragma once
#include <vector>
#include "BlockTransforms.h"
#include "TimeWarp.h"

// 레퍼런스 트랙에 맞춰 다른 트랙의 블롭을 보정한다. 역할(박자 / 음정)은 따로 켠다. JUCE 비의존.

enum class PitchRelation
{
    Unison,     // 더블링/코러스: 같은 음(옥타브 차이는 유지)에 맞춘다
    Harmony     // 화음: 음정 간격을 가장 가까운 반음 간격에 맞춘다 (레퍼런스와의 간격 유지)
};

struct TrackRoles
{
    bool timing = false;
    bool pitch = false;
    double timingAmount = 1.0;      // 0..1
    double pitchAmount = 1.0;       // 0..1
    double smoothAmount = 0.5;      // 음정 이음 정도 (pitch 역할이 켜진 트랙의 transitionAfter)
    PitchRelation relation = PitchRelation::Unison;
};

// 타깃 블록 목록을 레퍼런스에 맞춘 결과 (모든 블록, 인덱스 순서).
// baseline: 보정 전(기준) 블록들. 정도를 바꿔도 항상 기준에서 다시 계산하므로 슬라이더로 조절할 수 있다.
std::vector<BlockUpdate> fitToReference(const std::vector<VocalBlock>& baseline, double targetOffsetSec, const TimeMap& targetToReference,
                                        const std::vector<VocalBlock>& referenceBlocks, double referenceOffsetSec, const TrackRoles&);

// 개별 단계 (테스트/재사용)
VocalBlock withTimingFitted(const VocalBlock&, double targetOffsetSec, const TimeMap&, double amount);
// 레퍼런스 블록들 중 [startSong, endSong] 와 겹치는 유성 블록의 길이 가중 평균 음정. 없으면 음수.
// expectedMidi > 0 이면 그 음에서 한 옥타브 가까이(octaveErrorSemitones) 이상 벗어난 블록은 옥타브 오분석으로 보고
// 가장 가까운 옥타브로 옮겨서 평균한다 (레퍼런스의 음정 이동 흐름만 따른다).
constexpr double octaveErrorSemitones = 11.5;
double referencePitchOver(const std::vector<VocalBlock>& referenceBlocks, double referenceOffsetSec, double startSong, double endSong,
                          double expectedMidi = -1.0);
double foldedToOctaveOf(double midi, double expectedMidi) noexcept;
double snappedPitchShift(double targetCentreMidi, double referenceMidi, PitchRelation, double amount);
