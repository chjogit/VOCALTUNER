#include "MixSpectrum.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr double pi = 3.14159265358979323846;

void fft(std::vector<std::complex<double>>& a)
{
    const auto n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> w(std::cos(-2.0 * pi / len), std::sin(-2.0 * pi / len));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> wn(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k, wn *= w)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * wn;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// 주변 평균을 빼서 뾰족한 봉우리(배음)만 남긴다
std::vector<double> whitened(const std::vector<double>& spec)
{
    constexpr long half = 8;
    const auto n = static_cast<long>(spec.size());
    std::vector<double> prefix(spec.size() + 1, 0.0);
    for (long k = 0; k < n; ++k)
        prefix[static_cast<size_t>(k + 1)] = prefix[static_cast<size_t>(k)] + spec[static_cast<size_t>(k)];
    std::vector<double> out(spec.size());
    for (long k = 0; k < n; ++k)
    {
        const auto a = std::max(0L, k - half), b = std::min(n, k + half + 1);
        const auto local = (prefix[static_cast<size_t>(b)] - prefix[static_cast<size_t>(a)]) / static_cast<double>(b - a);
        out[static_cast<size_t>(k)] = std::max(0.0, spec[static_cast<size_t>(k)] - local);
    }
    return out;
}

} // namespace

MixSpectrum::MixSpectrum(size_t frameSize)
{
    size_t n = 256;
    while (n < frameSize)
        n <<= 1;
    window.resize(n);
    for (size_t i = 0; i < n; ++i)
        window[i] = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(n));
    work.resize(n);
}

// 가운데 성분의 크기 스펙트럼 (압축). 좌우를 한 번의 복소 FFT로 (z = L + iR)
std::vector<double> MixSpectrum::centre(const float* left, const float* right)
{
    const auto n = window.size();
    for (size_t i = 0; i < n; ++i)
        work[i] = std::complex<double>(left[i] * window[i], right[i] * window[i]);
    fft(work);
    std::vector<double> out(n / 2);
    double sum = 0.0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const auto z = work[k], zc = std::conj(work[n - k]);
        const auto l = (z + zc) * 0.5, r = (z - zc) * std::complex<double>(0.0, -0.5);
        const auto m = std::abs(l + r) * 0.5, s = std::abs(l - r) * 0.5;
        out[k] = m * (m * m / (m * m + s * s + 1e-20));     // 좌우가 다를수록 줄인다
        sum += out[k];
    }
    const auto mean = sum / static_cast<double>(n / 2) + 1e-12;
    for (auto& v : out)
        v = std::log1p(v / mean);
    return whitened(out);
}

double hzOf(double midi) noexcept { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

double valueAt(const std::vector<double>& spec, double bin) noexcept
{
    const auto k = static_cast<size_t>(bin);
    if (k + 1 >= spec.size())
        return 0.0;
    const auto f = bin - static_cast<double>(k);
    return spec[k] * (1.0 - f) + spec[k + 1] * f;
}

double harmonicSalience(const std::vector<double>& spec, double hz, double binHz, int harmonics) noexcept
{
    double s = 0.0, weight = 1.0;
    for (int h = 1; h <= harmonics && h * hz < 5000.0; ++h, weight *= 0.8)
        s += weight * valueAt(spec, h * hz / binHz);
    return s;
}
