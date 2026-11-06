#pragma once
#include <memory>
#include <vector>
#include "BlockParser.h"
#include "EditTimeMap.h"
#include "VocalFeatures.h"

// 표시 시간 격자(프레임 i = i * 주기) 위에서 본 편집 결과
struct EditGrid
{
    std::vector<int> sourceFrame;       // 프레임 i가 가져오는 원본 프레임
    std::vector<double> targetMidi;     // 0 = 무성
    std::vector<double> originalMidi;   // 같은 프레임에서 원본이 가진 음정
    std::vector<double> gainDb;
    std::vector<double> formantAmount;  // 프레임마다 포먼트를 원본 자리에 두는 정도 (기본 1)
    bool formantEdited = false;         // 어느 프레임이든 1이 아님
    int lastSourceFrame = 0;
    bool timingChanged = false;         // 문서 어딘가에 박자 편집이 있음 (렌더 방식은 구간이 아니라 문서 전체로 정한다)
    std::shared_ptr<const EditTimeMap> timeMap;
    size_t size() const noexcept { return targetMidi.size(); }
};

// 블록 목록을 표시 시간 격자 위의 편집 결과로 (박자 지도, 목표 음정, 볼륨)
EditGrid buildGrid(size_t samples, int sampleRate, const VocalFeatures&, const std::vector<VocalBlock>&);

// 프레임 i(구간)가 원본과 다른 소리를 만드는지 (박자가 다르거나 음정이 tolerance 반음 넘게 다름)
bool frameChanges(const EditGrid&, size_t i, double tolerance) noexcept;
bool rangeChanges(const EditGrid&, size_t begin, size_t end, double tolerance) noexcept;
