#pragma once
#include <functional>
#include <vector>
#include "SectionClassifier.h"
#include "SpectralFrames.h"

// 두 트랙의 시간 정렬 (DTW). JUCE 비의존.

// 단조 증가하는 조각 선형 시간 대응: 타깃 곡 시간 -> 레퍼런스 곡 시간
class TimeMap
{
public:
    TimeMap() = default;
    TimeMap(std::vector<double> from, std::vector<double> to) : fromTimes(std::move(from)), toTimes(std::move(to)) {}

    bool empty() const noexcept { return fromTimes.empty(); }
    double map(double t) const noexcept;          // 범위 밖은 끝점의 이동량을 그대로 유지
    const std::vector<double>& from() const noexcept { return fromTimes; }
    const std::vector<double>& to() const noexcept { return toTimes; }

private:
    std::vector<double> fromTimes, toTimes;
};

struct TrackFrames
{
    const SpectralFrames* frames = nullptr;
    const std::vector<SectionKind>* kinds = nullptr;
    double offsetSec = 0.0;     // 곡 시간 = 소스 시간 + offsetSec
    double songTimeAt(size_t frame) const noexcept { return frames->timeAt(frame) + offsetSec; }
};

struct AlignOptions
{
    double maxShiftSec = 1.0;       // 두 트랙이 이 이상 어긋나 있지 않다고 가정 (탐색 폭)
    double stepPenalty = 0.15;      // 대각선이 아닌 걸음의 벌점 (큰 왜곡 억제)
    double kindMismatchCost = 1.5;  // 구간 종류(무음/호흡/유성/무성)가 다를 때의 벌점
    double energyWeight = 0.5;
    double smoothSec = 0.03;        // 결과 대응의 이동량을 이 폭으로 고르게
    const TimeMap* guide = nullptr; // 레퍼런스 곡 시간 -> 타깃 곡 시간 (가사 앵커). 있으면 탐색 대역의 중심으로 쓴다
    std::function<bool(double)> progress;   // 진행률 0..1 보고. false를 돌려주면 중단 (결과 ok = false, cancelled = true)
};

struct AlignResult
{
    bool ok = false;
    TimeMap targetToReference;
    double meanCost = 0.0;          // 걸음당 평균 비용 (낮을수록 잘 맞음)
    bool cancelled = false;
};

AlignResult alignToReference(const TrackFrames& reference, const TrackFrames& target, const AlignOptions& = {});
