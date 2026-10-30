#include "SpectralFrames.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr size_t fftSize = 1024;
constexpr int melBands = 26;

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
        const auto angle = -2.0 * pi / static_cast<double>(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

double hzToMel(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }
double melToHz(double mel) { return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0); }

struct FilterBank
{
    std::vector<std::vector<std::pair<size_t, double>>> bands;   // 대역별 (bin, 가중치)
    std::vector<double> window;
    std::vector<std::vector<double>> dct;                        // mfccCount x melBands
    size_t highBin = 0, lowBin = 0;
};

FilterBank makeFilterBank(int sampleRate)
{
    FilterBank bank;
    const auto nyquist = 0.5 * sampleRate;
    const auto top = std::min(8000.0, 0.95 * nyquist);
    const auto lowMel = hzToMel(100.0), highMel = hzToMel(top);
    const auto binHz = static_cast<double>(sampleRate) / static_cast<double>(fftSize);

    std::vector<double> edges;
    for (int i = 0; i < melBands + 2; ++i)
        edges.push_back(melToHz(lowMel + (highMel - lowMel) * i / (melBands + 1)));

    bank.bands.resize(melBands);
    for (int b = 0; b < melBands; ++b)
        for (size_t bin = 1; bin < fftSize / 2; ++bin)
        {
            const auto hz = static_cast<double>(bin) * binHz;
            double weight = 0.0;
            if (hz > edges[b] && hz <= edges[b + 1])
                weight = (hz - edges[b]) / (edges[b + 1] - edges[b]);
            else if (hz > edges[b + 1] && hz < edges[b + 2])
                weight = (edges[b + 2] - hz) / (edges[b + 2] - edges[b + 1]);
            if (weight > 0.0)
                bank.bands[b].push_back({ bin, weight });
        }

    bank.window.resize(fftSize);
    for (size_t i = 0; i < fftSize; ++i)
        bank.window[i] = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(fftSize));

    bank.dct.assign(mfccCount, std::vector<double>(melBands));
    for (int c = 0; c < mfccCount; ++c)
        for (int b = 0; b < melBands; ++b)
            bank.dct[c][b] = std::cos(pi * (c + 1) * (b + 0.5) / melBands);   // c1..c12 (c0는 에너지로 따로)

    bank.lowBin = static_cast<size_t>(500.0 / binHz);
    bank.highBin = static_cast<size_t>(3000.0 / binHz);
    return bank;
}

struct FrameResult
{
    float energyDb = -120.0f, flatness = 0.0f, highRatio = 0.0f;
    float mfcc[mfccCount] = {};
};

FrameResult analyseFrame(const std::vector<double>& mono, long long centreSample, const FilterBank& bank, int sampleRate)
{
    std::vector<std::complex<double>> spectrum(fftSize);
    const auto start = centreSample - static_cast<long long>(fftSize / 2);
    double energy = 0.0;
    for (size_t i = 0; i < fftSize; ++i)
    {
        const auto idx = start + static_cast<long long>(i);
        const auto v = (idx >= 0 && idx < static_cast<long long>(mono.size())) ? mono[static_cast<size_t>(idx)] : 0.0;
        energy += v * v;
        spectrum[i] = v * bank.window[i];
    }
    fft(spectrum);

    std::vector<double> power(fftSize / 2);
    for (size_t k = 0; k < power.size(); ++k)
        power[k] = std::norm(spectrum[k]);

    FrameResult out;
    out.energyDb = static_cast<float>(10.0 * std::log10(energy / static_cast<double>(fftSize) + 1e-10));

    std::vector<double> logMel(melBands);
    for (int b = 0; b < melBands; ++b)
    {
        double sum = 0.0;
        for (const auto& [bin, weight] : bank.bands[b])
            sum += weight * power[bin];
        logMel[b] = std::log(sum + 1e-9);
    }
    for (int c = 0; c < mfccCount; ++c)
    {
        double sum = 0.0;
        for (int b = 0; b < melBands; ++b)
            sum += bank.dct[c][b] * logMel[b];
        out.mfcc[c] = static_cast<float>(sum / melBands);
    }

    // 평탄도: 0.5 kHz ~ 상한의 기하평균 / 산술평균
    const auto hiLimit = std::min(power.size(), static_cast<size_t>(0.95 * 0.5 * sampleRate / (static_cast<double>(sampleRate) / static_cast<double>(fftSize))));
    double logSum = 0.0, linSum = 0.0, high = 0.0, total = 0.0;
    size_t count = 0;
    for (size_t k = bank.lowBin; k < hiLimit; ++k)
    {
        logSum += std::log(power[k] + 1e-12);
        linSum += power[k];
        if (k >= bank.highBin)
            high += power[k];
        ++count;
    }
    for (size_t k = 1; k < hiLimit; ++k)
        total += power[k];
    if (count > 0 && linSum > 0.0)
        out.flatness = static_cast<float>(std::exp(logSum / static_cast<double>(count)) / (linSum / static_cast<double>(count)));
    out.highRatio = total > 0.0 ? static_cast<float>(high / total) : 0.0f;
    return out;
}

void normalise(std::vector<float>& mfcc, size_t frames)
{
    if (frames == 0)
        return;
    for (int c = 0; c < mfccCount; ++c)
    {
        double mean = 0.0;
        for (size_t i = 0; i < frames; ++i)
            mean += mfcc[i * mfccCount + c];
        mean /= static_cast<double>(frames);
        double var = 0.0;
        for (size_t i = 0; i < frames; ++i)
        {
            const auto d = mfcc[i * mfccCount + c] - mean;
            var += d * d;
        }
        const auto sd = std::sqrt(var / static_cast<double>(frames)) + 1e-6;
        for (size_t i = 0; i < frames; ++i)
            mfcc[i * mfccCount + c] = static_cast<float>((mfcc[i * mfccCount + c] - mean) / sd);
    }
}
} // namespace

SpectralFrames computeSpectralFrames(const std::vector<double>& mono, int sampleRate, double hopSec, const ParallelFor& parallelFor)
{
    SpectralFrames frames;
    frames.hopSec = hopSec;
    const auto hopSamples = std::max<long long>(1, std::llround(hopSec * sampleRate));
    frames.hopSec = static_cast<double>(hopSamples) / sampleRate;
    const auto count = static_cast<size_t>(mono.size() / static_cast<size_t>(hopSamples));
    if (count == 0 || sampleRate < 16000)
        return frames;

    frames.mfcc.assign(count * mfccCount, 0.0f);
    frames.energyDb.assign(count, -120.0f);
    frames.flatness.assign(count, 0.0f);
    frames.highRatio.assign(count, 0.0f);

    const auto bank = makeFilterBank(sampleRate);
    const auto range = [&](size_t begin, size_t end)
    {
        for (auto i = begin; i < end; ++i)
        {
            const auto r = analyseFrame(mono, static_cast<long long>(i) * hopSamples, bank, sampleRate);
            std::copy(r.mfcc, r.mfcc + mfccCount, frames.mfcc.begin() + static_cast<long>(i * mfccCount));
            frames.energyDb[i] = r.energyDb;
            frames.flatness[i] = r.flatness;
            frames.highRatio[i] = r.highRatio;
        }
    };

    constexpr size_t chunk = 256;
    const auto chunks = static_cast<int>((count + chunk - 1) / chunk);
    if (parallelFor && chunks > 1)
        parallelFor(chunks, [&](int c) { range(static_cast<size_t>(c) * chunk, std::min(count, static_cast<size_t>(c + 1) * chunk)); });
    else
        range(0, count);

    normalise(frames.mfcc, count);
    return frames;
}
