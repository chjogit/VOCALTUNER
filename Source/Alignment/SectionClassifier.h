#pragma once
#include <vector>
#include "SpectralFrames.h"
#include "VocalFeatures.h"

// 프레임을 무음 / 호흡 / 유성(모음) / 무성(자음 등)으로 분류하고 구간으로 묶는다. 정렬의 닻으로 쓰인다.

enum class SectionKind { Silence = 0, Breath = 1, Voiced = 2, Unvoiced = 3 };

struct Section
{
    SectionKind kind = SectionKind::Silence;
    double startSec = 0.0;      // 소스 시간
    double endSec = 0.0;
};

struct ClassifierOptions
{
    double silenceAboveFloorDb = 10.0;   // 잡음 바닥보다 이만큼 커야 소리로 침
    double breathMinFlatness = 0.18;     // 호흡: 잡음처럼 평탄하고
    double breathMinHighRatio = 0.12;    // 고역이 있고
    double breathMaxBelowPeakDb = 14.0;  // 노래보다 한참 작고
    double breathMinSec = 0.12;          // 이 이상 이어질 때만 호흡
    double minSectionSec = 0.04;         // 이보다 짧은 구간은 이웃에 흡수
};

// frames와 같은 길이의 분류. features(F0)는 유성 판단에 쓴다.
std::vector<SectionKind> classifyFrames(const SpectralFrames& frames, const VocalFeatures& features,
                                        const ClassifierOptions& options = {});

std::vector<Section> toSections(const std::vector<SectionKind>& kinds, const SpectralFrames& frames);
