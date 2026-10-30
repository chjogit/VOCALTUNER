#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "VocalFeatures.h"

// Python 프로토타입의 MelodyneBlock / split_by_pitch_steps 포팅 (Whisper 없이 DSP만으로 블록 생성).
// 곡선 데이터는 복사하지 않고 VocalFeatures의 프레임 인덱스 범위로 참조한다.
struct VocalBlock
{
    int id = 0;
    size_t beginFrame = 0;      // [beginFrame, endFrame)
    size_t endFrame = 0;
    double startSec = 0.0;
    double endSec = 0.0;        // 마지막 프레임의 끝 (배타적)
    int baseMidi = 0;           // 중심 반음 (자음 블록은 인접 모음 블록의 값)
    double pitchOffset = 0.0;   // 중앙값 - baseMidi (반음 단위, 자음은 0)
    bool isConsonant = false;   // true면 피치 고정
    bool isContinuation = false;// 같은 음절에서 음정이 꺾여 이어지는 하위 블록 ("─")
    std::string lyric;          // Whisper 연동 전까지 비어 있음

    // --- 사용자 편집. 파서는 건드리지 않고, 원본 구간/곡선은 그대로 둔 채 표시·렌더 때 적용한다.
    double pitchShift = 0.0;        // 블롭 전체를 옮긴 반음 수
    double gainDb = 0.0;            // 볼륨 (두께)
    double startOffsetSec = 0.0;    // 타이밍 편집: 표시 시작 = startSec + startOffsetSec
    double endOffsetSec = 0.0;
    std::vector<float> curveOffset; // F0 곡선 편집: 프레임별 반음 보정 (비어 있으면 0, 아니면 end-begin개)
    std::vector<float> curveWarp;   // F0 곡선 시간 편집: 프레임별 이동량 (프레임 단위, 양수 = 늦춤). 비어 있으면 0
    std::vector<size_t> userHandles;// 사용자가 추가한 곡선 핸들 (절대 프레임)
    double transitionAfter = 0.5;   // 다음 유성 블롭과 음정을 이어 주는 정도 (0 = 계단, 1 = 가장 부드럽게)
    double vibratoDepth = 0.0;      // 비브라토 진폭 (반음, 0 = 원래 곡선). 0보다 크면 곡선을 정리하고 사인 비브라토를 얹는다
    double vibratoRateHz = 5.5;     // 비브라토 속도
    double flattenAmount = 0.0;     // F0 곡선을 블롭의 중심 음으로 펴는 정도 (0 = 원래 곡선, 1 = 일자)
    // 음정 균형 (멜로다인 Correct Pitch 방식). 기본 자동 보정 = 중심 음정 5%.
    double tidyAmount = 0.05;       // 중심 음정(Pitch Center): 블롭의 평균 음정을 목표 음 쪽으로 옮기는 정도. 곡선 모양은 그대로
    double driftAmount = 0.0;       // 음정 흐름(Pitch Drift): 블롭 안의 느린 오르내림을 줄이는 정도 (-1..1, 음수 = 과장). 비브라토는 남는다
    int centerTargetMidi = -1;      // 중심 음정의 목표 음 (-1 = 가장 가까운 반음 baseMidi)

    double displayStartSec() const noexcept { return startSec + startOffsetSec; }
    double displayEndSec() const noexcept { return endSec + endOffsetSec; }
    double displayCenterMidi() const noexcept { return baseMidi + pitchOffset + pitchShift; }
};

struct BlockOptions
{
    // --- 유성 구간 묶기
    int minVoicedFrames = 12;       // 이보다 짧은 유성 구간은 버림 (60 ms)
    int maxBridgeFrames = 8;        // 이 이하의 무성 틈은 같은 구간으로 이어 붙임 (40 ms)

    // --- 음정 계단 분할 (프로토타입 값 그대로)
    // 중앙값 필터링한 피치의 3프레임 간격 기울기가 이 값을 넘는 봉우리에서 분할.
    // 계단이 선명할 때 약 1.3 반음 이상의 도약에 해당한다.
    double stepPeakHeight = 0.22;
    int stepPeakDistance = 12;      // 분할점 사이 최소 간격 (프레임)
    int minSubBlockFrames = 6;      // 이보다 짧은 조각은 버림 (30 ms)

    // --- 음정이 다른 두 부분 강제 분할: 계단 분할이 놓친 완만한 이동(글라이드)도 나눈다.
    // 고른 곡선의 앞/뒤 평균이 이만큼 다르고, 양쪽이 minPlateauFrames 이상이면 가장 크게 갈리는 곳에서 나눈다 (반복).
    double plateauSplitSemitones = 0.9;
    int minPlateauFrames = 20;      // 100 ms

    // --- 자음 (무성이지만 에너지가 있는 구간). 유성 구간 앞/뒤에 붙은 것만 블록으로 만든다.
    // RMS 창(2048 샘플)이 모음 에너지를 약 +-4프레임 번지게 하므로 minConsonantFrames는 그보다 커야 함.
    double consonantMinRms = 0.02;  // 정규화 RMS (-34 dB)
    int minConsonantFrames = 8;     // 40 ms
    int maxConsonantFrames = 30;    // 150 ms
    bool includeTrailingConsonants = true;
};

std::vector<VocalBlock> parseBlocks(const VocalFeatures& features, const BlockOptions& options = {});
