#include "SpectralDiff.h"
#include <algorithm>
#include <cmath>
#include <world/fft.h>

namespace
{
struct BandEnergy { double low = 0.0, high = 0.0; };

class BandAnalyzer
{
public:
    BandAnalyzer(int size, int sampleRate, double highHz)
        : n(size), split(static_cast<int>(highHz / sampleRate * size)), window(static_cast<size_t>(size)),
          in(static_cast<size_t>(size)), out(static_cast<size_t>(size + 2))
    {
        for (int i = 0; i < n; ++i)
            window[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / n);
        plan = fft_plan_dft_r2c_1d(n, in.data(), reinterpret_cast<fft_complex*>(out.data()), FFT_ESTIMATE);
    }
    ~BandAnalyzer() { fft_destroy_plan(plan); }
    BandAnalyzer(const BandAnalyzer&) = delete;
    BandAnalyzer& operator=(const BandAnalyzer&) = delete;

    BandEnergy measure(const std::vector<float>& x, size_t begin)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto k = begin + static_cast<size_t>(i);
            in[static_cast<size_t>(i)] = (k < x.size() ? x[k] : 0.0f) * window[static_cast<size_t>(i)];
        }
        fft_execute(plan);
        BandEnergy e;
        for (int b = 1; b <= n / 2; ++b)
        {
            const auto re = out[2 * static_cast<size_t>(b)], im = out[2 * static_cast<size_t>(b) + 1];
            (b < split ? e.low : e.high) += re * re + im * im;
        }
        const auto norm = 1.0 / (0.375 * n * n);   // Hann 창 전력 보정, 풀스케일 사인 ~ 0 dB 근처
        e.low *= norm;
        e.high *= norm;
        return e;
    }

private:
    int n, split;
    std::vector<double> window, in, out;   // out: 복소수 (re, im) 쌍
    fft_plan plan;
};

double toDb(double energy) noexcept { return 10.0 * std::log10(energy + 1e-12); }
} // namespace

std::vector<SpectralDiffFrame> spectralDiff(const std::vector<float>& original, const std::vector<float>& rendered,
                                            int sampleRate, const SpectralDiffOptions& o)
{
    BandAnalyzer analyzer(o.fftSize, sampleRate, o.highHz);
    std::vector<SpectralDiffFrame> frames;
    const auto length = std::min(original.size(), rendered.size());
    for (size_t begin = 0; begin + static_cast<size_t>(o.fftSize) <= length; begin += static_cast<size_t>(o.hop))
    {
        const auto a = analyzer.measure(original, begin), b = analyzer.measure(rendered, begin);
        SpectralDiffFrame f;
        f.timeSec = (static_cast<double>(begin) + 0.5 * o.fftSize) / sampleRate;
        f.lowDiffDb = toDb(b.low) - toDb(a.low);
        f.highDiffDb = toDb(b.high) - toDb(a.high);
        f.renderedHighDb = toDb(b.high);
        f.flagged = f.highDiffDb > o.flagDb && f.renderedHighDb > o.floorDb;
        frames.push_back(f);
    }
    return frames;
}

std::vector<DiffRegion> flaggedRegions(const std::vector<SpectralDiffFrame>& frames, double frameSec, double gapSec)
{
    std::vector<DiffRegion> regions;
    for (const auto& f : frames)
    {
        if (! f.flagged)
            continue;
        const auto start = f.timeSec - 0.5 * frameSec, end = f.timeSec + 0.5 * frameSec;
        if (! regions.empty() && start - regions.back().endSec <= gapSec)
        {
            regions.back().endSec = end;
            regions.back().peakDb = std::max(regions.back().peakDb, f.highDiffDb);
        }
        else
            regions.push_back({ start, end, f.highDiffDb });
    }
    return regions;
}
