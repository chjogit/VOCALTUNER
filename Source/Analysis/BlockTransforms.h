#pragma once
#include <cstddef>
#include <optional>
#include <vector>
#include "BlockParser.h"

// 블롭 편집 연산 (순수 함수, JUCE 비의존). 입력 블록은 바꾸지 않고 편집된 사본을 돌려준다.

constexpr double minBlockSeconds = 0.03;
constexpr double pitchJointMaxGapSec = 0.12;        // 이보다 떨어진 유성 블롭끼리는 음정을 잇지 않음
constexpr double pitchTransitionMaxHalfSec = 0.12;  // 이음 정도 100%일 때 경계 양쪽으로 퍼지는 폭

struct BlockUpdate
{
    size_t index = 0;
    VocalBlock block;
};

// 블록의 원본 구간 <-> 편집된 표시 구간 (선형 매핑: F0 곡선이 블롭과 함께 늘어나고 움직임)
double sourceToDisplaySec(const VocalBlock&, double sourceSec) noexcept;
double displayToSourceSec(const VocalBlock&, double displaySec) noexcept;

// 편집(전체 이동 + 곡선 보정)을 반영한 프레임의 피치. 무성 프레임이거나 범위 밖이면 0.
double blockPitchAt(const VocalFeatures&, const VocalBlock&, size_t frame) noexcept;

// 편집된 곡선의 한 프레임: 블롭 이동(이웃과의 이음 포함) + 곡선 보정. 무성/범위 밖이면 0.
double editedPitchAt(const VocalFeatures&, const std::vector<VocalBlock>& blocks, size_t index, size_t frame) noexcept;

// 곡선의 한 프레임이 화면에서 놓이는 시각 (블롭 타이밍 + 곡선 시간 편집 반영)
double curveFrameDisplaySec(const VocalFeatures&, const VocalBlock&, size_t frame) noexcept;

// --- 비브라토: 깊이 > 0 이면 원래 곡선을 부드럽게 정리한 뒤 사인 비브라토를 얹는다. 시작/끝은 서서히 커지고 작아진다.
constexpr double maxVibratoDepth = 1.5;     // 반음
constexpr double minVibratoRate = 3.5;
constexpr double maxVibratoRate = 8.0;
// 프레임의 곡선에 더해지는 반음 보정 (유성 프레임이 아니면 0). 펴기 -> 비브라토 순으로 적용된다.
// 곡선 편집이 반영된 프레임 피치 (펴기/비브라토/정돈의 기준 곡선)
double workingMidiAt(const VocalFeatures&, const VocalBlock&, size_t frame) noexcept;
double flattenOffsetAt(const VocalFeatures&, const VocalBlock&, size_t frame) noexcept;
// 기본 자동 보정: 곡선을 이 녹음의 가장 가까운 음정(baseMidi)으로 아주 조금 끌어당김
double tidyOffsetAt(const VocalFeatures&, const VocalBlock&, size_t frame) noexcept;
double driftOffsetAt(const VocalFeatures&, const VocalBlock&, size_t frame) noexcept;
double vibratoOffsetAt(const VocalFeatures&, const VocalBlock&, size_t frame) noexcept;
// 두 보정을 합친 값
double shapeOffsetAt(const VocalFeatures&, const VocalBlock&, size_t frame) noexcept;
// 펴기/비브라토/정돈을 곡선 편집(curveOffset)에 굳히고 값은 0으로. 다음 보정은 이 결과를 기준으로 이어진다.
constexpr double defaultTidyAmount = 0.05;   // 기본 자동 보정 = 중심 음정 5%

// --- 음정 균형 (멜로다인 Correct Pitch). 조를 고르면 그 조의 음에만 맞춘다.
struct PitchScale
{
    int root = -1;          // 0 = C ... 11 = B, -1 = 반음(크로매틱)
    bool minor = false;
};
struct PitchBalance
{
    double center = defaultTidyAmount;     // 중심 음정 0..1
    double drift = 0.0;                         // 음정 흐름 0..1
    PitchScale scale;
    double toleranceCents = 0.0;                // 이미 이 안에 있는 블롭은 중심 음정을 건드리지 않음 (0 = 끔)
};
int nearestScaleNote(double midi, const PitchScale&) noexcept;
std::vector<BlockUpdate> withPitchBalance(const std::vector<VocalBlock>&, const std::vector<size_t>& indices, const PitchBalance&);

// 기초 자동 보정(tidy) 켜기/끄기. 값이 바뀌어야 하는 블록만 돌려준다 (없으면 빈 목록)
std::vector<BlockUpdate> withBaseCorrection(const std::vector<VocalBlock>&, bool on);

VocalBlock withShapeBaked(const VocalFeatures&, const VocalBlock&);

// --- 음정 이음: 가까운 두 유성 블롭(사이의 자음 블록은 건너뜀)의 이동량이 경계에서 부드럽게 이어진다
struct PitchJoint
{
    size_t left = 0;
    size_t right = 0;
    double timeSec = 0.0;       // 경계 (두 블롭 사이의 중간)
    double halfWidthSec = 0.0;  // 전이 구간의 절반 폭 = left.transitionAfter에 비례
};
std::vector<PitchJoint> findPitchJoints(const std::vector<VocalBlock>& blocks);
double effectivePitchShift(const std::vector<VocalBlock>& blocks, size_t index, double displaySec) noexcept;

// 음정의 가운데(가장 가까운 반음)로 정렬한 사본. 자음 블록은 그대로.
VocalBlock alignedToNoteCenter(const VocalBlock&) noexcept;

// 노래 전체의 평균 음정 (원본 기준, 길이 가중). 유성 블록이 없으면 60.
double averagePitchMidi(const std::vector<VocalBlock>& blocks) noexcept;

// 표시 시각 t에서의 볼륨(dB). 맞닿은 앞뒤 블록과의 경계에서는 부드럽게 이어진다.
double effectiveGainDb(const std::vector<VocalBlock>& blocks, size_t index, double displaySec) noexcept;

// --- 타이밍 (앞뒤 블록을 넘지 않도록 제한)
enum class BlockEdge { Front, Back };
VocalBlock movedInTime(const std::vector<VocalBlock>& blocks, size_t index, double deltaSec);
VocalBlock resizedInTime(const std::vector<VocalBlock>& blocks, size_t index, BlockEdge edge, double deltaSec);

// 여러 블롭을 한 덩어리로: indices(오름차순)의 블록을 deltaSec만큼 옮기거나, 맨 앞/뒤 가장자리를 deltaSec만큼 끌어
// 덩어리 전체를 비례해서 늘이고 줄인다. 선택하지 않은 이웃은 길이를 유지한 채 밀려난다.
// 시간 0 앞으로 밀리게 되거나 너무 짧아지면 가능한 만큼만 적용. 반환은 바뀐 모든 블록(선택 + 밀린 이웃).
std::vector<BlockUpdate> groupMovedInTime(const std::vector<VocalBlock>& blocks, const std::vector<size_t>& indices, double deltaSec);
std::vector<BlockUpdate> groupScaledInTime(const std::vector<VocalBlock>& blocks, const std::vector<size_t>& indices,
                                           BlockEdge edge, double deltaSec);

// --- 여러 트랙 동시 편집: 한 트랙에서 한 편집을 같은 시간대의 다른 트랙 블롭에도 똑같이 적용한다.
struct LinkedEdit
{
    // 편집된 선택 구간의 편집 전/후 (표시 시각). 타이밍 편집이 아니면 old == new.
    double oldStart = 0.0, oldEnd = 0.0, newStart = 0.0, newEnd = 0.0;
    // 편집 전후 값의 차이
    double pitchShift = 0.0, gainDb = 0.0, vibratoDepth = 0.0, vibratoRateHz = 0.0, flattenAmount = 0.0;

    bool snapToNote = false;    // 음정 맞춤 편집: 받는 트랙은 이동량 대신 자기 블롭을 가장 가까운 음정으로 맞춘다

    bool isEmpty() const noexcept;
};
// 기준 편집(편집 전 블록 -> 편집 후 블록들)에서 LinkedEdit를 만든다. changed: 선택된 블록들의 (전, 후) 쌍.
LinkedEdit makeLinkedEdit(const std::vector<std::pair<VocalBlock, VocalBlock>>& changed);
// blocks 중 [oldStart, oldEnd]와 겹치는 블록에 적용. 타이밍은 구간 전체의 선형 변환이고, 이웃은 밀려난다.
std::vector<BlockUpdate> applyLinkedEdit(const std::vector<VocalBlock>& blocks, const LinkedEdit&);

// --- F0 곡선: 피치 기울기의 방향이 바뀌는 지점(극대/극소)의 프레임 목록
std::vector<size_t> findCurveHandles(const VocalFeatures&, const VocalBlock&, double thresholdSemitones = 0.3,
                                     size_t minSpacingFrames = 6);

// 감지한 핸들 + 사용자가 추가한 핸들 (정렬, 사용자 핸들 우선)
std::vector<size_t> allCurveHandles(const VocalFeatures&, const VocalBlock&, size_t minSpacingFrames = 6);

// frame 근처(removeRadiusFrames 이내)에 사용자 핸들이 있으면 지우고, 없으면 추가한 사본. 양 끝 가까이에는 추가하지 않음.
VocalBlock withUserHandleToggled(const VocalBlock&, size_t frame, size_t removeRadiusFrames = 4, size_t minSpacingFrames = 6);

// origin 블록에서 handles[handleIndex]를 deltaSemitones만큼 올린 사본. 이웃 핸들/양 끝에서 0으로 부드럽게 줄어든다.
// deltaFrames > 0 이면 곡선의 그 지점이 시간상 늦춰진다 (이웃 핸들을 넘지 않도록 제한).
VocalBlock withCurveHandleDragged(const VocalBlock& origin, const std::vector<size_t>& handles, size_t handleIndex,
                                  double deltaSemitones, double deltaFrames = 0.0);
