#pragma once
#include <cstdint>
#include <functional>
#include "GuidedMelody.h"
#include "TimeWarp.h"
#include "VocalFeatures.h"

// 반주 섞인 원곡 음원을 기준으로 맞출 때의 보조. JUCE 비의존.
// 원곡에서는 보컬 블록을 믿을 수 없으므로, 따라 부른 보컬의 음을 가이드로 원곡의 멜로디만 뽑아 쓴다.

// 레퍼런스(원곡) 소스 시간 기준 멜로디. target: 따라 부른 보컬의 분석, targetToReference: 곡 시간 대응
MelodyTrack melodyForTarget(int64_t referenceLength, int referenceRate, const StereoReader& readReference,
                            const VocalFeatures& target, double targetOffsetSec, const TimeMap& targetToReference,
                            double referenceOffsetSec, const std::function<bool()>& shouldCancel = {});
