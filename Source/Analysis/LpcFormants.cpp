#include "SpectralEnvelope.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double targetRate = 11025.0;
constexpr int maxOrder = 18;
constexpr double preEmphasis = 0.97;
constexpr double minHz = 90.0, maxHz = 5000.0, maxBandwidthHz = 400.0;
using Cx = std::complex<double>;

struct Decimated { std::vector<double> x; double rate = 0.0; };

// i번째 샘플의 창 sinc 저역 통과 값 (차단 cut = 원 샘플 속도 기준 정규화 주파수)
double firAt(const double* x, size_t n, size_t i, int taps, double cut)
{
    double acc = 0.0;
    for (int k = -taps; k <= taps; ++k)
    {
        const auto at = static_cast<long>(i) + k;
        if (at < 0 || at >= static_cast<long>(n)) continue;
        const auto s = k == 0 ? 2.0 * cut : std::sin(2.0 * pi * cut * k) / (pi * k);
        acc += x[at] * s * (0.54 + 0.46 * std::cos(pi * k / taps));
    }
    return acc;
}

// 정수 배 D로 줄이기 전 저역 통과 (차단 = 새 나이퀴스트의 90%)
Decimated decimate(const double* frame, size_t n, int sampleRate)
{
    const auto d = std::max(1, static_cast<int>(sampleRate / targetRate));
    Decimated out;
    out.rate = static_cast<double>(sampleRate) / d;
    if (d == 1)
    {
        out.x.assign(frame, frame + n);
        return out;
    }
    for (size_t i = 0; i < n; i += static_cast<size_t>(d))
        out.x.push_back(firAt(frame, n, i, 8 * d, 0.45 / d));
    return out;
}

std::vector<double> windowedPreEmphasised(const std::vector<double>& x)
{
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto v = x[i] - (i > 0 ? preEmphasis * x[i - 1] : 0.0);
        y[i] = v * (0.54 - 0.46 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(x.size() - 1)));
    }
    return y;
}

std::vector<double> autocorrelation(const std::vector<double>& x, int order)
{
    std::vector<double> r(static_cast<size_t>(order) + 1, 0.0);
    for (size_t lag = 0; lag < r.size(); ++lag)
        for (size_t i = lag; i < x.size(); ++i)
            r[lag] += x[i] * x[i - lag];
    return r;
}

// A(z) = 1 + a1 z^-1 + ... 계수 (a[0] = 1). 불안정하면 빈 벡터
std::vector<double> levinson(const std::vector<double>& r)
{
    const auto p = r.size() - 1;
    std::vector<double> a(p + 1, 0.0), tmp;
    a[0] = 1.0;
    double err = r[0];
    for (size_t i = 1; i <= p; ++i)
    {
        double acc = r[i];
        for (size_t j = 1; j < i; ++j) acc += a[j] * r[i - j];
        const auto k = -acc / err;
        tmp = a;
        for (size_t j = 1; j < i; ++j) a[j] = tmp[j] + k * tmp[i - j];
        a[i] = k;
        err *= 1.0 - k * k;
        if (err <= 0.0) return {};
    }
    return a;
}

Cx polyAt(const std::vector<double>& a, Cx z)
{
    Cx v = 1.0;   // 최고차 계수 (z^p의 계수는 a[0] = 1)
    for (size_t k = 1; k < a.size(); ++k) v = v * z + a[k];
    return v;
}

// Durand-Kerner: z^p + a1 z^(p-1) + ... + ap 의 모든 근
std::vector<Cx> polyRoots(const std::vector<double>& a)
{
    const auto p = a.size() - 1;
    std::vector<Cx> z(p);
    for (size_t k = 0; k < p; ++k)
        z[k] = 0.8 * std::polar(1.0, 2.0 * pi * static_cast<double>(k) / static_cast<double>(p) + 0.4);
    for (int it = 0; it < 300; ++it)
    {
        double move = 0.0;
        for (size_t k = 0; k < p; ++k)
        {
            Cx den = 1.0;
            for (size_t j = 0; j < p; ++j) if (j != k) den *= z[k] - z[j];
            const auto delta = polyAt(a, z[k]) / den;
            z[k] -= delta;
            move = std::max(move, std::abs(delta));
        }
        if (move < 1e-11) break;
    }
    return z;
}

Formants pickFormants(const std::vector<Cx>& roots, double rate)
{
    std::vector<std::pair<double, double>> found;
    for (const auto& z : roots)
    {
        if (z.imag() <= 1e-9 || std::abs(z) >= 1.0) continue;
        const auto hz = std::atan2(z.imag(), z.real()) * rate / (2.0 * pi);
        const auto bw = -std::log(std::abs(z)) * rate / pi;
        if (hz >= minHz && hz <= maxHz && bw < maxBandwidthHz) found.emplace_back(hz, bw);
    }
    std::sort(found.begin(), found.end());
    Formants f;
    for (size_t i = 0; i < found.size() && i < 4; ++i, ++f.count)
    {
        f.hz[i] = found[i].first;
        f.bandwidth[i] = found[i].second;
    }
    return f;
}
} // namespace

Formants lpcFormants(const double* frame, size_t n, int sampleRate)
{
    if (frame == nullptr || sampleRate <= 0)
        return {};
    const auto d = decimate(frame, n, sampleRate);
    const auto order = std::min(maxOrder, 2 + static_cast<int>(std::lround(d.rate / 1000.0)));
    if (d.x.size() < static_cast<size_t>(4 * order))
        return {};
    const auto r = autocorrelation(windowedPreEmphasised(d.x), order);
    if (r[0] < 1e-12)
        return {};
    auto reg = r;
    reg[0] *= 1.0 + 1e-9;
    const auto a = levinson(reg);
    return a.empty() ? Formants{} : pickFormants(polyRoots(a), d.rate);
}
