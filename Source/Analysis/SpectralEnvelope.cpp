#include "SpectralEnvelope.h"
#include <algorithm>
#include <cmath>
#include <world/fft.h>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double toleranceNat = 2.0 / 8.685889638;   // 2 dB (자연로그 진폭)
constexpr int maxIterations = 50;
constexpr double fallbackF0Hz = 200.0;
constexpr double floorAmp = 1e-12;

// 실수 대칭열의 FFT는 실수다: 로그 스펙트럼 <-> 켑스트럼을 같은 정방향 FFT 하나로 오간다
class SymmetricFft
{
public:
    explicit SymmetricFft(size_t size) : n(size), in(size), out(size + 2)
    {
        plan = fft_plan_dft_r2c_1d(static_cast<int>(n), in.data(), reinterpret_cast<fft_complex*>(out.data()), FFT_ESTIMATE);
    }
    ~SymmetricFft() { if (ok()) fft_destroy_plan(plan); }
    SymmetricFft(const SymmetricFft&) = delete;
    SymmetricFft& operator=(const SymmetricFft&) = delete;

    // 플랜을 만들지 못했으면 false
    bool ok() const noexcept { return plan.w != nullptr && plan.ip != nullptr && plan.input != nullptr; }

    // frame[0..len)에 Hann 창을 씌우고 0을 채워 변환한 로그 진폭 (n/2+1개). 플랜은 한 번만 만들어 재사용한다
    std::vector<double> logSpectrum(const double* frame, size_t len)
    {
        std::fill(in.begin(), in.end(), 0.0);
        for (size_t i = 0; i < len; ++i)
            in[i] = frame[i] * (0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(std::max<size_t>(1, len - 1))));
        fft_execute(plan);
        std::vector<double> logAmp(n / 2 + 1);
        for (size_t m = 0; m < logAmp.size(); ++m)
            logAmp[m] = std::log(std::hypot(out[2 * m], out[2 * m + 1]) + floorAmp);
        return logAmp;
    }

    // half[0..n/2]를 짝대칭으로 펼쳐 변환한 실수부 (n/2+1개)
    std::vector<double> transform(const std::vector<double>& half)
    {
        std::fill(in.begin(), in.end(), 0.0);
        for (size_t m = 0; m < half.size(); ++m)
        {
            in[m] = half[m];
            if (m > 0) in[n - m] = half[m];
        }
        fft_execute(plan);
        std::vector<double> re(n / 2 + 1);
        for (size_t m = 0; m < re.size(); ++m) re[m] = out[2 * m];
        return re;
    }

private:
    size_t n;
    std::vector<double> in, out;
    fft_plan plan{};
};

// 켑스트럼 차수 order 이하만 남긴 평활
std::vector<double> cepstralSmooth(SymmetricFft& fft, const std::vector<double>& logHalf, size_t order)
{
    const auto nfft = 2 * (logHalf.size() - 1);
    auto cep = fft.transform(logHalf);
    for (size_t k = 0; k < cep.size(); ++k)
        cep[k] = k <= order ? cep[k] / static_cast<double>(nfft) : 0.0;
    return fft.transform(cep);
}

size_t cepstralOrder(size_t nfft, int sampleRate, double f0Hz)
{
    const auto f0 = f0Hz > 0.0 ? f0Hz : fallbackF0Hz;
    const auto k = static_cast<size_t>(std::lround(0.5 * sampleRate / f0));
    return std::clamp<size_t>(k, 4, nfft / 2 - 1);
}

// A = max(목표, 평활)를 되풀이해 평활이 목표 위로 올라갈 때까지 (최대 오차 < 2 dB 또는 50회)
std::vector<double> refineEnvelope(SymmetricFft& fft, const std::vector<double>& target, size_t order)
{
    auto a = target;
    auto env = cepstralSmooth(fft, a, order);
    for (int it = 0; it < maxIterations; ++it)
    {
        double worst = 0.0;
        for (size_t m = 0; m < a.size(); ++m)
        {
            worst = std::max(worst, target[m] - env[m]);
            a[m] = std::max(target[m], env[m]);
        }
        if (worst < toleranceNat)
            break;
        env = cepstralSmooth(fft, a, order);
    }
    return env;
}
} // namespace

size_t trueEnvelopeFftSize(size_t n) noexcept
{
    size_t nfft = 64;
    while (nfft < n) nfft *= 2;
    return nfft;
}

std::vector<double> trueEnvelope(const double* frame, size_t n, int sampleRate, double f0Hz)
{
    if (frame == nullptr || n < 2 || sampleRate <= 0 || ! std::isfinite(f0Hz))
        return {};
    const auto nfft = trueEnvelopeFftSize(n);
    SymmetricFft fft(nfft);
    if (! fft.ok())
        return {};
    return refineEnvelope(fft, fft.logSpectrum(frame, n), cepstralOrder(nfft, sampleRate, f0Hz));
}
