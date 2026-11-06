#pragma once
#include <cstddef>
#include <vector>
#include "Psola.h"

// 위상 보코더 음정 이동 (실험용 대안 엔진, PSOLA와 비교하려고 둔다). JUCE 비의존. 기존 렌더 경로는 쓰지 않는다.
// STFT(Hann, N = 2048 @44.1k를 표본율에 맞춰 2의 거듭제곱으로, 홉 N/4)에서 프레임마다 빈을 k -> k*비율로 옮기고
// 위상을 이어 쌓는다(시간 늘임 + 리샘플 없음). 봉우리 위상만 전파하고 둘레 빈은 봉우리에 묶는다 (Laroche-Dolson 정체성 위상 고정).
// 포먼트: True Envelope로 원본 프레임의 포락선을 구해, 이동 뒤 포락선으로 나누고 2^((1-양)*이동/12)배 옮긴 포락선을 곱한다.
// 무성 프레임(sourceMidi == 0)과 이동 0 프레임은 스펙트럼을 그대로 두고, 그런 구간의 샘플은 원본 그대로다.
// 입력은 PsolaInput과 같다 (shiftSemitones, sourceMidi, formantAmount는 프레임당 1개, formantAmount nullptr = 1).
std::vector<float> vocoderShift(const PsolaInput&, size_t beginSample, size_t endSample);
