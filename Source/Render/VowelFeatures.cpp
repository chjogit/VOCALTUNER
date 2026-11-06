#include "VowelWeight.h"
#include <algorithm>
#include <cmath>
#include <world/fft.h>
#include "ConsonantShield.h"

namespace
{
constexpr double pi = 3.14159265358979323846;

size_t windowSizeFor(int sampleRate)
{
    size_t n = 64;
    while (static_cast<double>(n) < 0.02 * sampleRate)   // 약 20 ms 이상인 2의 거듭제곱
        n *= 2;
    return n;
}

std::vector<double> hannTable(size_t n)
{
    std::vector<double> w(n);
    for (size_t i = 0; i < n; ++i)
        w[i] = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(n));
    return w;
}

// 프레임 i의 중심 샘플 (ConsonantShield와 같은 프레임: i*hop 부터 2*hop)
long centerOf(size_t i, double hop) { return static_cast<long>((static_cast<double>(i) + 1.0) * hop); }

float at(const float* x, size_t length, long k) { return k >= 0 && static_cast<size_t>(k) < length ? x[k] : 0.0f; }

// Hann 창 FFT 파워 스펙트럼에서 저역 비중과 평탄도를 잰다
class Spectrum
{
public:
    explicit Spectrum(const std::vector<double>& hann) : n(hann.size()), window(hann), in(n), out(n + 2), power(n / 2 + 1)
    {
        plan = fft_plan_dft_r2c_1d(static_cast<int>(n), in.data(), reinterpret_cast<fft_complex*>(out.data()), FFT_ESTIMATE);
    }
    ~Spectrum() { fft_destroy_plan(plan); }
    Spectrum(const Spectrum&) = delete;
    Spectrum& operator=(const Spectrum&) = delete;

    void measure(const float* x, size_t length, long center, int sampleRate, double f0Hz, VowelFeatures& f)
    {
        const auto start = center - static_cast<long>(n / 2);
        for (size_t i = 0; i < n; ++i)
            in[i] = at(x, length, start + static_cast<long>(i)) * window[i];
        fft_execute(plan);
        for (size_t b = 0; b <= n / 2; ++b)
            power[b] = out[2 * b] * out[2 * b] + out[2 * b + 1] * out[2 * b + 1];
        const auto binHz = static_cast<double>(sampleRate) / static_cast<double>(n);
        f.lowShare = lowShare(binHz, std::max(vowelLowBandMinHz, vowelLowBandHarmonics * f0Hz));
        f.flatness = flatness(binHz);
    }

private:
    double lowShare(double binHz, double cutoffHz) const
    {
        double total = 0.0, low = 0.0;
        for (size_t b = 1; b < power.size(); ++b)
        {
            total += power[b];
            low += static_cast<double>(b) * binHz < cutoffHz ? power[b] : 0.0;
        }
        return total > 0.0 ? low / total : 0.0;
    }
    // 대역 평균 파워 대비 기하평균. 평균보다 60 dB 낮은 바닥을 둬 log(0)을 피한다
    double flatness(double binHz) const
    {
        const auto lo = static_cast<size_t>(std::ceil(vowelFlatBandLowHz / binHz)), hi = std::min(power.size() - 1, static_cast<size_t>(vowelFlatBandHighHz / binHz));
        if (hi <= lo)
            return 1.0;
        double sum = 0.0;
        for (auto b = lo; b <= hi; ++b)
            sum += power[b];
        const auto mean = sum / static_cast<double>(hi - lo + 1), floorP = 1e-6 * mean;
        if (! (mean > 0.0) || ! std::isfinite(mean))
            return 1.0;
        double logSum = 0.0;
        for (auto b = lo; b <= hi; ++b)
            logSum += std::log(std::max(power[b], floorP));
        const auto flat = std::exp(logSum / static_cast<double>(hi - lo + 1)) / mean;
        return std::isfinite(flat) ? flat : 1.0;
    }
    size_t n;
    std::vector<double> window, in, out, power;
    fft_plan plan{};
};

// F0 주기 T 만큼 떨어진 두 조각의 정규화 상관 (lag T-2..T+2 중 최대: Harvest F0가 약간 어긋나도 된다)
double periodicityAt(const float* x, size_t length, long center, double period)
{
    const auto t = static_cast<long>(std::lround(period));
    double best = 0.0;
    for (long lag = t - 2; lag <= t + 2; ++lag)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (long k = center - t; k < center; ++k)
        {
            const double a = at(x, length, k), b = at(x, length, k + lag);
            ab += a * b; aa += a * a; bb += b * b;
        }
        best = std::max(best, aa > 0.0 && bb > 0.0 ? ab / std::sqrt(aa * bb) : 0.0);
    }
    return best;
}

// 프레임 창(Hann 가중) 평균 제곱. 상대 레벨 기준용
std::vector<double> frameEnergies(const float* x, size_t length, const std::vector<double>& hann, double hop, size_t from, size_t to)
{
    const auto n = hann.size();
    std::vector<double> e(to - from, 0.0);
    for (size_t i = from; i < to; ++i)
    {
        const auto start = centerOf(i, hop) - static_cast<long>(n / 2);
        double s = 0.0;
        for (size_t k = 0; k < n; ++k)
        {
            const auto v = static_cast<double>(at(x, length, start + static_cast<long>(k)));
            s += hann[k] * v * v;
        }
        e[i - from] = s / (0.375 * static_cast<double>(n));
    }
    return e;
}

double db(double energy) { return 10.0 * std::log10(energy + 1e-12); }

// 프레임 i 둘레의 유성 프레임 중 최대 에너지
double localVoicedMax(const std::vector<double>& e, size_t base, const double* midi, size_t frames, size_t i, size_t radius)
{
    const auto from = i > radius ? i - radius : 0, to = std::min(frames, i + radius + 1);
    double top = 0.0;
    for (auto k = from; k < to; ++k)
        top = midi[k] > 0.0 ? std::max(top, e[k - base]) : top;
    return top;
}
// 한 호출이 공유하는 입력과 미리 구한 프레임 에너지
struct Input
{
    const float* x;
    size_t length;
    int sampleRate;
    double hop;
    const double* midi;
    size_t frames, eFrom, levelRadius;
    std::vector<double> energy;
};

VowelFeatures frameFeatures(const Input& in, Spectrum& spectrum, size_t i, bool fricative)
{
    VowelFeatures f;
    f.voiced = in.midi[i] > 0.0;
    if (! f.voiced)
        return f;
    const auto c = centerOf(i, in.hop);
    const auto hz = 440.0 * std::pow(2.0, (in.midi[i] - 69.0) / 12.0);
    f.fricative = fricative;
    f.periodicity = periodicityAt(in.x, in.length, c, in.sampleRate / hz);
    f.relLevelDb = db(in.energy[i - in.eFrom]) - db(localVoicedMax(in.energy, in.eFrom, in.midi, in.frames, i, in.levelRadius));
    spectrum.measure(in.x, in.length, c, in.sampleRate, hz, f);
    return f;
}
} // namespace

std::vector<VowelFeatures> vowelFeatures(const float* x, size_t length, int sampleRate, double framePeriodSec, const double* midi,
                                         size_t frames, size_t firstFrame, size_t lastFrame)
{
    lastFrame = std::min(lastFrame, frames);
    if (firstFrame >= lastFrame)
        return {};
    if (x == nullptr || midi == nullptr)
        return std::vector<VowelFeatures>(lastFrame - firstFrame);   // 입력이 없으면 전부 무성 취급
    const double hop = framePeriodSec * sampleRate;
    const auto hann = hannTable(windowSizeFor(sampleRate));
    const auto radius = vowelRadiusFrames(vowelLevelRadiusSec, framePeriodSec);
    const auto eFrom = firstFrame > radius ? firstFrame - radius : 0;
    const Input in { x, length, sampleRate, hop, midi, frames, eFrom, radius,
                     frameEnergies(x, length, hann, hop, eFrom, std::min(frames, lastFrame + radius)) };
    const auto fric = fricativeFrames(x, length, sampleRate, framePeriodSec, lastFrame - firstFrame, firstFrame);
    std::vector<VowelFeatures> out;
    Spectrum spectrum(hann);
    for (auto i = firstFrame; i < lastFrame; ++i)
        out.push_back(frameFeatures(in, spectrum, i, fric[i - firstFrame] != 0));
    return out;
}
