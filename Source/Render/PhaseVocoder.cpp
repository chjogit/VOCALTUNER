#include "PhaseVocoder.h"
#include "PhaseVocoderInternal.h"
#include "SpectralEnvelope.h"
#include <algorithm>
#include <cmath>

namespace
{
using namespace pvoc;
constexpr double activeShift = 0.01;   // 이보다 작은 이동은 옮기지 않는 것으로 본다 (PSOLA와 같다)

size_t frameSizeFor(int sampleRate)
{
    const auto scaled = std::log2(2048.0 * sampleRate / 44100.0);
    return static_cast<size_t>(1) << static_cast<int>(std::lround(scaled));
}

struct Params { bool active = false; double ratio = 1.0, formantRatio = 1.0, f0Hz = 0.0; };

Params paramsAt(const PsolaInput& in, long centre)
{
    const auto f = std::min<size_t>(in.frames - 1, centre <= 0 ? 0 : static_cast<size_t>(std::lround(centre / (in.framePeriodSec * in.sampleRate))));
    Params p;
    const auto shift = in.shiftSemitones[f], midi = in.sourceMidi[f];
    if (midi <= 0.0 || std::abs(shift) < activeShift) return p;
    const auto amount = in.formantAmount ? std::clamp(in.formantAmount[f], 0.0, 1.0) : 1.0;
    p.active = true;
    p.ratio = std::pow(2.0, shift / 12.0);
    p.formantRatio = std::pow(2.0, (1.0 - amount) * shift / 12.0);
    p.f0Hz = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
    return p;
}

// 프레임 m(중심 m*hop)을 [first, last) 구간의 누적기에 더한다
class Engine
{
public:
    Engine(const PsolaInput& input, long firstSample, long lastSample)
        : in(input), n(frameSizeFor(input.sampleRate)), hop(n / 4), first(firstSample), fft(n),
          window(n), frame(n), raw(n), acc(static_cast<size_t>(lastSample - firstSample), 0.0), norm(acc.size(), 0.0)
    {
        for (size_t i = 0; i < n; ++i) window[i] = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(n));
    }

    void run(long m)
    {
        const auto centre = m * static_cast<long>(hop);
        loadFrame(centre - static_cast<long>(n / 2));
        fft.forward(frame, spec);
        const auto p = paramsAt(in, centre);
        if (p.active) transform(p);
        else passThrough();
        fft.inverse(spec, frame);
        overlapAdd(centre - static_cast<long>(n / 2));
    }
    float result(long sample, float fallback) const
    {
        const auto i = static_cast<size_t>(sample - first);
        return norm[i] > 1e-9 ? static_cast<float>(acc[i] / norm[i]) : fallback;
    }

private:
    void loadFrame(long start)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const auto s = start + static_cast<long>(i);
            raw[i] = s >= 0 && s < static_cast<long>(in.length) ? static_cast<double>(in.samples[s]) : 0.0;
            frame[i] = raw[i] * window[i];
        }
    }
    void passThrough()
    {
        state.prevPeaks.clear();
        rememberAnalysis(spec, state);
    }
    void transform(const Params& p)
    {
        const auto env = trueEnvelope(raw.data(), n, in.sampleRate, p.f0Hz);
        shiftFrame(spec, state, p.ratio, hop, shifted);
        const auto gain = formantGain(env, p.ratio, p.formantRatio);
        for (size_t k = 0; k < gain.size(); ++k) { shifted.re[k] *= gain[k]; shifted.im[k] *= gain[k]; }
        spec = shifted;
    }
    void overlapAdd(long start)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const auto s = start + static_cast<long>(i) - first;
            if (s < 0 || s >= static_cast<long>(acc.size())) continue;
            acc[static_cast<size_t>(s)] += frame[i] * window[i];
            norm[static_cast<size_t>(s)] += window[i] * window[i];
        }
    }

    const PsolaInput& in;
    size_t n, hop;
    long first;
    FrameFft fft;
    std::vector<double> window, frame, raw, acc, norm;
    Spec spec, shifted;
    ShiftState state;
};

long floorDiv(long a, long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

} // namespace

std::vector<float> vocoderShift(const PsolaInput& in, size_t beginSample, size_t endSample)
{
    endSample = std::min(endSample, in.length);
    if (beginSample >= endSample || ! in.samples || in.frames == 0) return {};
    std::vector<float> out(in.samples + beginSample, in.samples + endSample);
    const auto b = static_cast<long>(beginSample), e = static_cast<long>(endSample);
    const auto n = static_cast<long>(frameSizeFor(in.sampleRate)), hop = n / 4;
    const auto m0 = floorDiv(b - n / 2, hop), m1 = floorDiv(e + n / 2, hop) + 1;
    std::vector<long> active;
    for (long m = m0; m <= m1; ++m)
        if (paramsAt(in, m * hop).active) active.push_back(m);
    if (active.empty()) return out;
    const auto from = active.front() - 3, to = active.back() + 3;   // 앞뒤 3프레임: 모든 표본이 4프레임으로 덮인다
    Engine engine(in, from * hop - n / 2, to * hop + n / 2);
    for (long m = from; m <= to; ++m) engine.run(m);
    std::vector<char> touched(out.size(), 0);
    for (const auto m : active)
        for (long s = std::max(b, m * hop - n / 2); s < std::min(e, m * hop + n / 2); ++s) touched[static_cast<size_t>(s - b)] = 1;
    for (size_t i = 0; i < out.size(); ++i)
        if (touched[i]) out[i] = engine.result(b + static_cast<long>(i), out[i]);
    return out;
}
