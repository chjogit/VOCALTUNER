#include "PhaseVocoderInternal.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace pvoc
{
FrameFft::FrameFft(size_t size)
    : n(size), timeIn(size), timeOut(size), freqOut(size + 2), freqIn(size + 2)
{
    const auto len = static_cast<int>(n);
    fwd = fft_plan_dft_r2c_1d(len, timeIn.data(), reinterpret_cast<fft_complex*>(freqOut.data()), FFT_ESTIMATE);
    bwd = fft_plan_dft_c2r_1d(len, reinterpret_cast<fft_complex*>(freqIn.data()), timeOut.data(), FFT_ESTIMATE);
}

FrameFft::~FrameFft() { fft_destroy_plan(fwd); fft_destroy_plan(bwd); }

void FrameFft::forward(const std::vector<double>& frame, Spec& out)
{
    std::copy(frame.begin(), frame.end(), timeIn.begin());
    fft_execute(fwd);
    out.re.resize(n / 2 + 1);
    out.im.resize(n / 2 + 1);
    for (size_t k = 0; k < out.re.size(); ++k) { out.re[k] = freqOut[2 * k]; out.im[k] = freqOut[2 * k + 1]; }
}

void FrameFft::inverse(const Spec& spec, std::vector<double>& frame)
{
    for (size_t k = 0; k < spec.re.size(); ++k) { freqIn[2 * k] = spec.re[k]; freqIn[2 * k + 1] = spec.im[k]; }
    freqIn[1] = 0.0;
    freqIn[n + 1] = 0.0;
    fft_execute(bwd);
    frame.resize(n);
    for (size_t i = 0; i < n; ++i) frame[i] = timeOut[i] / static_cast<double>(n);
}

namespace
{
constexpr double peakFloor = 1e-5;     // 최대 진폭 대비 이보다 작은 봉우리는 무시
constexpr double matchBins = 1.5;      // 앞 프레임 봉우리와 같은 정현파로 보는 빈 거리 (옮긴 뒤 기준)
constexpr double maxOffset = 1.0;      // 봉우리 빈에서 순간 주파수까지 믿는 최대 거리 (빈)

double wrap(double x) noexcept { return x - 2.0 * pi * std::round(x / (2.0 * pi)); }

// 프레임 중심을 시간 원점으로 옮긴다 (N/2 지연 = 홀수 빈 부호 반전). 이 영역에서 창 씌운 정현파의 둘레 빈은 위상이 같다 (+-pi)
void centre(Spec& s)
{
    for (size_t k = 1; k < s.re.size(); k += 2) { s.re[k] = -s.re[k]; s.im[k] = -s.im[k]; }
}

// 양쪽 두 빈보다 큰 빈 (Laroche-Dolson)
std::vector<long> findPeaks(const std::vector<double>& mag)
{
    const auto top = *std::max_element(mag.begin(), mag.end());
    std::vector<long> peaks;
    const auto last = static_cast<long>(mag.size()) - 1;
    for (long k = 0; k <= last; ++k)
    {
        if (mag[static_cast<size_t>(k)] <= peakFloor * top) continue;
        bool isPeak = true;
        for (long d = -2; d <= 2 && isPeak; ++d)
        {
            const auto j = k + d;
            if (d != 0 && j >= 0 && j <= last && mag[static_cast<size_t>(j)] >= mag[static_cast<size_t>(k)]) isPeak = false;
        }
        if (isPeak) peaks.push_back(k);
    }
    return peaks;
}

// 앞 프레임의 합성 봉우리 중 target(빈)에 가장 가까운 것 (없으면 nullptr)
const Peak* matchPrevious(const std::vector<Peak>& prev, double target)
{
    const Peak* best = nullptr;
    for (const auto& p : prev)
        if (std::abs(p.bin - target) <= matchBins && (! best || std::abs(p.bin - target) < std::abs(best->bin - target))) best = &p;
    return best;
}

// 봉우리 p의 순간 주파수 (rad/샘플). 앞 분석 프레임이 없으면 빈 중심. in은 중심 기준 위상
double instantFreq(const Spec& in, const ShiftState& st, long p, size_t hop, size_t n)
{
    const auto k = static_cast<size_t>(p);
    const auto centreFreq = 2.0 * pi * static_cast<double>(p) / static_cast<double>(n);
    if (st.prevPhase.empty()) return centreFreq;
    const auto phase = std::atan2(in.im[k], in.re[k]);
    const auto dev = wrap(phase - st.prevPhase[k] - centreFreq * static_cast<double>(hop)) / static_cast<double>(hop);
    const auto limit = maxOffset * 2.0 * pi / static_cast<double>(n);
    return centreFreq + std::clamp(dev, -limit, limit);
}

struct Region { long begin, end; };

Region regionOf(const std::vector<long>& peaks, size_t i, long last)
{
    const auto lo = i == 0 ? 0 : (peaks[i - 1] + peaks[i]) / 2 + 1;
    const auto hi = i + 1 == peaks.size() ? last : (peaks[i] + peaks[i + 1]) / 2;
    return { lo, hi };
}

// 복소 스펙트럼을 실수 위치 pos에서 4점 라그랑주 보간 (범위 밖은 0)
void interpolate(const Spec& s, double pos, double& re, double& im)
{
    const auto base = static_cast<long>(std::floor(pos));
    const auto f = pos - static_cast<double>(base);
    const double w[4] = { -f * (f - 1.0) * (f - 2.0) / 6.0, (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0,
                          -(f + 1.0) * f * (f - 2.0) / 2.0, (f + 1.0) * f * (f - 1.0) / 6.0 };
    re = im = 0.0;
    for (int d = 0; d < 4; ++d)
    {
        const auto j = base - 1 + d;
        if (j < 0 || j >= static_cast<long>(s.re.size())) continue;
        re += w[d] * s.re[static_cast<size_t>(j)];
        im += w[d] * s.im[static_cast<size_t>(j)];
    }
}

// 봉우리 둘레 영역을 shift빈(실수) 옮기고 위상을 peakPhase 기준으로 묶는다 (봉우리의 분석 위상 base). 겹치면 큰 진폭이 이긴다
void placeRegion(const Spec& in, Spec& out, std::vector<double>& outMag, Region r, double shift, double base, double peakPhase)
{
    const auto last = static_cast<long>(in.re.size()) - 1;
    const auto turn = std::polar(1.0, peakPhase - base);
    const auto from = std::max(0L, static_cast<long>(std::ceil(static_cast<double>(r.begin) + shift)));
    const auto to = std::min(last, static_cast<long>(std::floor(static_cast<double>(r.end) + shift)));
    for (long t = from; t <= to; ++t)
    {
        double re = 0.0, im = 0.0;
        interpolate(in, static_cast<double>(t) - shift, re, im);
        const auto o = static_cast<size_t>(t);
        const auto mag = std::hypot(re, im);
        if (mag <= outMag[o]) continue;
        const auto v = std::complex<double>(re, im) * turn;
        outMag[o] = mag;
        out.re[o] = v.real();
        out.im[o] = v.imag();
    }
}

// 봉우리 하나를 옮긴다: 앞 프레임의 같은 정현파가 있으면 위상을 이어 쌓고, 없으면 분석 위상으로 시작
Peak movePeak(const Spec& in, const ShiftState& st, const std::vector<long>& peaks, size_t i, double ratio, size_t hop,
              Spec& out, std::vector<double>& outMag)
{
    const auto p = peaks[i];
    const auto n = 2 * (in.re.size() - 1);
    const auto omega = instantFreq(in, st, p, hop, n);
    const auto source = omega * static_cast<double>(n) / (2.0 * pi);   // 원본 정현파의 빈 위치
    const auto before = matchPrevious(st.prevPeaks, source * ratio);
    const auto base = std::atan2(in.im[static_cast<size_t>(p)], in.re[static_cast<size_t>(p)]);
    const auto phase = before ? before->phase + ratio * omega * static_cast<double>(hop) : base;
    placeRegion(in, out, outMag, regionOf(peaks, i, static_cast<long>(in.re.size()) - 1), source * (ratio - 1.0), base, phase);
    return { source * ratio, std::remainder(phase, 2.0 * pi) };
}
} // namespace

void rememberAnalysis(const Spec& in, ShiftState& state)
{
    state.prevPhase.resize(in.re.size());
    for (size_t k = 0; k < in.re.size(); ++k)
        state.prevPhase[k] = k % 2 ? std::atan2(-in.im[k], -in.re[k]) : std::atan2(in.im[k], in.re[k]);   // 중심 기준
}

void shiftFrame(const Spec& analysis, ShiftState& state, double ratio, size_t hop, Spec& out)
{
    auto in = analysis;
    centre(in);
    const auto bins = in.re.size();
    std::vector<double> mag(bins), outMag(bins, 0.0);
    for (size_t k = 0; k < bins; ++k) mag[k] = std::hypot(in.re[k], in.im[k]);
    out.re.assign(bins, 0.0);
    out.im.assign(bins, 0.0);
    const auto peaks = findPeaks(mag);
    std::vector<Peak> fresh;
    for (size_t i = 0; i < peaks.size(); ++i) fresh.push_back(movePeak(in, state, peaks, i, ratio, hop, out, outMag));
    centre(out);
    out.im.front() = 0.0;
    out.im.back() = 0.0;
    state.prevPeaks = std::move(fresh);
    rememberAnalysis(analysis, state);
}
} // namespace pvoc
