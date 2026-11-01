#pragma once
#include <cstdint>
#include <functional>
#include <vector>

// 반주·화음이 섞인 원곡에서 "따라 부른 보컬 근처의" 주 선율을 찾는다. JUCE 비의존.
// 오디오는 짧은 구간씩 읽어 처리하고 버린다 (프레임당 숫자 두 개만 남김).
// 1) 가운데 성분: 좌우가 같은 성분(메인 보컬)을 남기고 넓게 퍼진 소리를 줄인다
// 2) 배음 점수: 후보 음(가이드 ± searchSemitones)마다 배음의 크기를 더한다
// 3) 이어짐: 비터비로 음이 매끄럽게 이어지는 경로를 고른다
// 4) 근거: 고른 음이 부른 음보다 원곡에서 뚜렷할 때만 evidence가 높다. 맞추기는 근거가 충분한 블록만 옮긴다

struct GuidedMelodyOptions
{
    double hopSec = 0.01;
    int frameSize = 2048;           // 2의 거듭제곱
    double searchSemitones = 3.0;   // 가이드 음 근처만 후보로 (기타·화음이 다른 음을 내면 무시)
    double stepCents = 10.0;
    int harmonics = 10;
    double jumpCostPerSemitone = 0.3;
    std::function<bool()> shouldCancel;   // true면 남은 구간을 건너뛴다 (결과는 비어 있는 프레임으로 남음)
};

struct MelodyTrack
{
    double hopSec = 0.01;
    std::vector<float> midi;            // 0 = 없음 (가이드가 무성이거나 범위 밖)
    std::vector<float> confidence;      // 0..1
    std::vector<float> evidence;        // 0..1: 원곡에서 이 음이 부른 음보다 두드러진 정도 (부른 음과 다르다는 근거)
    size_t size() const noexcept { return midi.size(); }
};

// read(start, count, left, right): 출력 샘플 [start, start+count)를 채운다 (모노면 같은 값을 둘 다에). 범위 밖은 0.
using StereoReader = std::function<void(int64_t start, int count, float* left, float* right)>;
// guideMidiAt(sec): 그 시각(원곡 시간)에 따라 부른 보컬의 음 (0 = 무성)
MelodyTrack extractGuidedMelody(int64_t lengthSamples, int sampleRate, const StereoReader& read,
                                const std::function<double(double)>& guideMidiAt, const GuidedMelodyOptions& = {});

// 반주가 섞인 음원인지: 소리가 거의 끊기지 않으면(조용한 프레임이 드묾) 믹스로 본다. normalisedRms: VocalFeatures::rms
bool looksLikeMix(const std::vector<double>& normalisedRms);

// 원곡 멜로디로 블록 음정을 옮길 조건 (실제 원곡/보컬 한 쌍에서 뒤틀림 없는 비율이 가장 높게 나온 값)
struct MelodyGate
{
    float minEvidence = 0.2f;       // 프레임: 부른 음과 다르다는 근거
    double minEvidenceShare = 0.7;  // 블록 구간에서 근거 있는 프레임의 비율
    double minBlockSec = 0.2;       // 짧은 블록(꺾임·장식음)은 옮기지 않는다
    double maxShiftSemitones = 2.0; // 이보다 크게 다르면 다른 악기/화음으로 보고 옮기지 않는다
};

// [startSec, endSec] (원곡 시간)에서 근거 있는 프레임 음의 중앙값. 근거가 모자라면 음수
double melodyPitchOver(const MelodyTrack&, double startSec, double endSec, const MelodyGate& = {});
