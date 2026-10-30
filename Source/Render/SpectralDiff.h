#pragma once
#include <vector>

// 원본과 렌더 결과의 스펙트럼 차이. JUCE 비의존.
// 렌더가 원본에 없던 고주파(지글거림/갈라짐)를 더한 구간을 찾는다. 원본의 치찰음은 양쪽에 다 있으므로 차이로 잡히지 않는다.
// 두 신호는 같은 타임라인이어야 한다(타이밍 편집 없는 렌더).

struct SpectralDiffOptions
{
    int fftSize = 2048;
    int hop = 1024;
    double highHz = 4000.0;     // 이 이상을 고주파 대역으로 본다
    double flagDb = 6.0;        // 고주파가 원본보다 이만큼 커지면 표시
    double floorDb = -70.0;     // 렌더 고주파가 이보다 작으면 무시 (무음 구간)
};

struct SpectralDiffFrame
{
    double timeSec = 0.0;           // 프레임 중심
    double lowDiffDb = 0.0;         // 렌더 - 원본, highHz 미만
    double highDiffDb = 0.0;        // 렌더 - 원본, highHz 이상
    double renderedHighDb = 0.0;    // 렌더 고주파 에너지 (dBFS)
    bool flagged = false;
};

struct DiffRegion
{
    double startSec = 0.0, endSec = 0.0;
    double peakDb = 0.0;            // 구간 안 최대 highDiffDb
};

std::vector<SpectralDiffFrame> spectralDiff(const std::vector<float>& original, const std::vector<float>& rendered,
                                            int sampleRate, const SpectralDiffOptions& = {});

// 표시된 프레임을 이어 붙인 구간 (gapSec 이하 끊김은 하나로)
std::vector<DiffRegion> flaggedRegions(const std::vector<SpectralDiffFrame>& frames, double frameSec, double gapSec = 0.1);
