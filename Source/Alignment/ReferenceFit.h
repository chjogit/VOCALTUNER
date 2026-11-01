#pragma once
#include <vector>
#include "BlockTransforms.h"
#include "GuidedMelody.h"
#include "TimeWarp.h"
#include "VocalFeatures.h"

// 레퍼런스 트랙에 맞춰 다른 트랙의 블롭을 보정한다. 역할(박자 / 음정)은 따로 켠다. JUCE 비의존.

enum class PitchRelation
{
    Unison,     // 더블링/코러스: 같은 음(옥타브 차이는 유지)에 맞춘다
    Harmony     // 화음: 음정 간격을 가장 가까운 반음 간격에 맞춘다 (레퍼런스와의 간격 유지)
};

// 이동 한도 (사용자 원칙: 2반음 넘으면 위험, 3반음 넘으면 재녹음 영역)
constexpr double defaultFitShiftSemitones = 2.0;
constexpr double maxFitShiftSemitones = 3.0;
// 빠른 런 가운데 스쳐 가는 음: 짧은 음(runPassingNoteSec 미만)이 앞뒤 유성 블롭과 모두 맞닿아(runNeighbourGapSec 미만) 있으면 옮기지 않는다.
// 사용자 청취 결정(11/01): 앞뒤가 긴 음인 단독 짧은 음까지 옮기면(음정 일치 +5점) 이웃과 이동량 차이로 새 글라이드가 생겨 어색해서,
// 맞닿은 짧은 음은 모두 건너뛰는 이 규칙으로 되돌렸다.
constexpr double runPassingNoteSec = 0.12;
constexpr double runNeighbourGapSec = 0.015;

struct TrackRoles
{
    bool timing = false;
    bool pitch = false;
    double timingAmount = 1.0;      // 0..1
    double pitchAmount = 1.0;       // 0..1
    double smoothAmount = 0.5;      // 음정 이음 정도 (pitch 역할이 켜진 트랙의 transitionAfter)
    double maxShiftSemitones = defaultFitShiftSemitones;   // 이동 한도: 이보다 크게 옮겨야 하는 음은 그대로 둔다
    PitchRelation relation = PitchRelation::Unison;
};

// 타깃 블록 목록을 레퍼런스에 맞춘 결과 (모든 블록, 인덱스 순서).
// baseline: 보정 전(기준) 블록들. 정도를 바꿔도 항상 기준에서 다시 계산하므로 슬라이더로 조절할 수 있다.
// melody: 레퍼런스가 반주 섞인 음원일 때 그 음원에서 뽑은 멜로디 (레퍼런스 소스 시간). 있으면 음정은 레퍼런스 블록 대신
// 이것을 따르고, 근거가 모자란 블록은 건드리지 않는다.
std::vector<BlockUpdate> fitToReference(const std::vector<VocalBlock>& baseline, double targetOffsetSec, const TimeMap& targetToReference,
                                        const std::vector<VocalBlock>& referenceBlocks, double referenceOffsetSec, const TrackRoles&,
                                        const MelodyTrack* melody = nullptr, const VocalFeatures* referenceCurve = nullptr);

// 개별 단계 (테스트/재사용)
VocalBlock withTimingFitted(const VocalBlock&, double targetOffsetSec, const TimeMap&, double amount);
// 레퍼런스 블록들 중 [startSong, endSong] 와 겹치는 유성 블록의 길이 가중 평균 음정. 없으면 음수.
// expectedMidi > 0 이면 그 음에서 한 옥타브 가까이(octaveErrorSemitones) 이상 벗어난 블록은 옥타브 오분석으로 보고
// 가장 가까운 옥타브로 옮겨서 평균한다 (레퍼런스의 음정 이동 흐름만 따른다).
constexpr double octaveErrorSemitones = 11.5;
double referencePitchOver(const std::vector<VocalBlock>& referenceBlocks, double referenceOffsetSec, double startSong, double endSong,
                          double expectedMidi = -1.0);
// 깨끗한(반주 없는) 레퍼런스의 음정 곡선에서 [startSec, endSec] (레퍼런스 소스 시간) 유성 프레임의 중앙값.
// 각 프레임은 expectedMidi ±6반음 안으로 접는다 (옥타브 튐 무시). 유성 프레임이 절반 미만이면 음수.
// 빠른 런 가운데 스쳐 가는 음인지 (유성 블롭만, 앞뒤 유성 이웃이 둘 다 바로 붙어 있는 짧은 음)
bool isRunPassingNote(const std::vector<VocalBlock>& blocks, size_t index);
double referenceCurvePitchOver(const VocalFeatures& curve, double startSec, double endSec, double expectedMidi);
double foldedToOctaveOf(double midi, double expectedMidi) noexcept;
double snappedPitchShift(double targetCentreMidi, double referenceMidi, PitchRelation, double amount);
