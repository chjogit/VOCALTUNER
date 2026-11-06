#pragma once
#include <cstddef>
#include <vector>
#include <world/fft.h>

// 위상 보코더 구현이 같이 쓰는 조각들 (PhaseVocoder.cpp, PhaseVocoderLock.cpp, PhaseVocoderFormant.cpp)
namespace pvoc
{
constexpr double pi = 3.14159265358979323846;

struct Spec { std::vector<double> re, im; };   // 반 스펙트럼 N/2+1빈

// 실수 프레임 FFT 한 쌍 (r2c / c2r). 역변환은 1/N로 정규화
class FrameFft
{
public:
    explicit FrameFft(size_t n);
    ~FrameFft();
    FrameFft(const FrameFft&) = delete;
    FrameFft& operator=(const FrameFft&) = delete;
    void forward(const std::vector<double>& frame, Spec& out);
    void inverse(const Spec& spec, std::vector<double>& frame);

private:
    size_t n;
    std::vector<double> timeIn, timeOut, freqOut, freqIn;
    fft_plan fwd, bwd;
};

struct Peak { double bin = 0.0; double phase = 0.0; };   // 옮긴 뒤의 빈 위치(실수)와 그 합성 위상 (프레임 중심 기준)

// 이동 상태: 앞 프레임의 분석 위상과 합성 봉우리 (비어 있으면 처음)
struct ShiftState
{
    std::vector<double> prevPhase;
    std::vector<Peak> prevPeaks;
};

// 분석 스펙트럼(창 시작 기준)의 위상을 프레임 중심 기준으로 기억. 이동하지 않는 프레임에서도 부른다
void rememberAnalysis(const Spec& in, ShiftState& state);
// in의 빈을 ratio배로 옮겨 out을 만든다. hop = 프레임 간격 (샘플)
void shiftFrame(const Spec& in, ShiftState& state, double ratio, size_t hop, Spec& out);

// 옮겨진 프레임에 곱할 이득. logEnv = 원본 프레임의 로그 진폭 포락선 (N/2+1빈).
// gain[k] = exp(logEnv(k/formantRatio) - logEnv(k/ratio)), +-60 dB로 제한
std::vector<double> formantGain(const std::vector<double>& logEnv, double ratio, double formantRatio);
} // namespace pvoc
