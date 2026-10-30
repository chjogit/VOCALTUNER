#pragma once
#include <cstddef>
#include <vector>
#include "VocalFeatures.h"

// 레퍼런스 정렬용 스펙트럼 특징 (JUCE 비의존). 사용자가 정렬 기능을 켰을 때만 계산한다.
// 프레임은 hopSec 간격, 각 프레임은 mfccCount개의 정규화된 MFCC(발음 단서)와 에너지/평탄도/고역 비율(호흡·자음 단서).

constexpr int mfccCount = 12;

struct SpectralFrames
{
    double hopSec = 0.01;
    double originSec = 0.0;             // 첫 프레임 중심의 소스 시간
    std::vector<float> mfcc;            // size() * mfccCount, 평균/분산 정규화 (CMVN)
    std::vector<float> energyDb;        // 프레임 에너지 (dB)
    std::vector<float> flatness;        // 0..1, 0.5~8 kHz 스펙트럼 평탄도 (잡음 = 1에 가까움)
    std::vector<float> highRatio;       // 0..1, 3 kHz 이상 에너지 비율

    size_t size() const noexcept { return energyDb.size(); }
    double timeAt(size_t frame) const noexcept { return originSec + static_cast<double>(frame) * hopSec; }
    const float* coefficients(size_t frame) const noexcept { return mfcc.data() + frame * mfccCount; }
};

// mono 오디오 -> 스펙트럼 프레임. parallelFor가 있으면 프레임 구간을 병렬 처리.
SpectralFrames computeSpectralFrames(const std::vector<double>& mono, int sampleRate, double hopSec = 0.01,
                                     const ParallelFor& parallelFor = {});
