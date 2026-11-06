// 실제 목소리로 PSOLA 측정 (JUCE 비의존, 테스트 아님)
//   vt_voice_compare A.wav B.wav results.md [--level-match] [listenRawDir]
// --level-match: 같은 조건을 음량 보정(levelMatch) 켠 변형("levelmatch")으로도 렌더/측정. listenRawDir이 있으면 청취 조건의 원시 f32le를 쓴다
// A는 17.0-23.5 s, B는 24.0-30.5 s 구간을 쓴다. 이동 x 포먼트 양 x 엔진 x 구간을 렌더하고 출력 오디오만으로 측정한다.
#include "../Source/Render/Psola.h"
#include "VoiceMetrics.h"
#include "WavIO.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <cstdio>
#include <string>
#include <world/fft.h>

using namespace voicemetrics;

namespace
{
constexpr double pi = 3.14159265358979323846;

struct Excerpt
{
    std::string name;
    std::vector<float> x;
    std::vector<double> midi;   // 이 신호(excerpt)에서 직접 잰 Harvest (엔진 입력과 기준값이 같다)
    int sr = 48000;
};

struct Row
{
    std::string ex, engine;
    double shift, amount, peak, ms, hfDb, preDb, levelDb, pumpDb, hfSpike;
    size_t clips;
    F0Stat f0;
    FormantStat fm;
};

size_t countClips(const std::vector<float>& y)
{
    return static_cast<size_t>(std::count_if(y.begin(), y.end(), [](float v) { return std::abs(v) >= 0.999f; }));
}

double peakOf(const std::vector<float>& y)
{
    double p = 0.0;
    for (const auto v : y) p = std::max(p, static_cast<double>(std::abs(v)));
    return p;
}

double energy(const std::vector<float>& a, const std::vector<float>* b, size_t from, size_t to)
{
    double e = 0.0;
    for (auto i = from; i < to && i < a.size(); ++i) { const double d = a[i] - (b ? (*b)[i] : 0.0f); e += d * d; }
    return e;
}

// 2048 창 1024 간격 프레임마다 (4-8 kHz 파워, 전체 파워)
std::vector<std::pair<double, double>> framePowers(const std::vector<float>& y, int sr)
{
    std::vector<double> in(2048), out(2050);
    auto plan = fft_plan_dft_r2c_1d(2048, in.data(), reinterpret_cast<fft_complex*>(out.data()), FFT_ESTIMATE);
    std::vector<std::pair<double, double>> frames;
    for (size_t s = 0; s + 2048 < y.size(); s += 1024)
    {
        for (size_t i = 0; i < 2048; ++i) in[i] = y[s + i] * (0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / 2048.0));
        fft_execute(plan);
        double high = 0.0, total = 0.0;
        for (size_t k = 0; k <= 1024; ++k)
        {
            const auto p = out[2 * k] * out[2 * k] + out[2 * k + 1] * out[2 * k + 1];
            total += p;
            if (k >= static_cast<size_t>(4000 * 2048 / sr) && k <= static_cast<size_t>(8000 * 2048 / sr)) high += p;
        }
        frames.emplace_back(high, total);
    }
    fft_destroy_plan(plan);
    return frames;
}

// 4-8 kHz / 전체 파워 (dB), 전 구간
double highShareDb(const std::vector<float>& y, int sr)
{
    double high = 0.0, total = 0.0;
    for (const auto& [h, t] : framePowers(y, sr)) { high += h; total += t; }
    return 10.0 * std::log10((high + 1e-12) / (total + 1e-12));
}

// 새 고역 스파이크: 원본이 유성인 프레임마다 (출력 4-8 kHz 비중 - 원본 비중)의 95 백분위수 (dB). 이득만 바뀐 것은 비중에 영향이 없다
double hfSpikeDb(const Excerpt& e, const std::vector<float>& y)
{
    const auto a = framePowers(e.x, e.sr), b = framePowers(y, e.sr);
    std::vector<double> d;
    for (size_t f = 0; f < a.size() && f < b.size(); ++f)
        if (a[f].second > 1e-6 && e.midi[std::min(e.midi.size() - 1, (f * 1024 + 1024) / static_cast<size_t>(framePeriod * e.sr))] > 0.0)
            d.push_back(10.0 * std::log10((b[f].first + 1e-12) / (b[f].second + 1e-12) * (a[f].second + 1e-12) / (a[f].first + 1e-12)));
    return percentile95(d);
}

// 강한 시작: 무성 80 ms(16프레임) 뒤에 유성이 시작하고 이후 50 ms RMS가 -40 dBFS 이상인 프레임
std::vector<size_t> strongOnsets(const Excerpt& e)
{
    std::vector<size_t> on;
    const auto hop = static_cast<size_t>(framePeriod * e.sr), win = static_cast<size_t>(0.05 * e.sr);
    for (size_t i = 16; i < e.midi.size(); ++i)
    {
        if (e.midi[i] <= 0.0 || std::any_of(e.midi.begin() + static_cast<long>(i) - 16, e.midi.begin() + static_cast<long>(i), [](double m) { return m > 0.0; })) continue;
        if (i * hop + win < e.x.size() && std::sqrt(energy(e.x, nullptr, i * hop, i * hop + win) / static_cast<double>(win)) >= 0.01) on.push_back(i);
    }
    return on;
}

// 시작 직전 30 ms에서 (출력 - 원본) 에너지를 시작 직후 30 ms 원본 에너지에 견준 dB의 평균. 시작이 없으면 NaN
double preEchoDb(const Excerpt& e, const std::vector<float>& y)
{
    const auto hop = static_cast<size_t>(framePeriod * e.sr), w = static_cast<size_t>(0.03 * e.sr);
    double sum = 0.0;
    const auto on = strongOnsets(e);
    for (const auto i : on)
        sum += 10.0 * std::log10((energy(y, &e.x, i * hop - w, i * hop) + 1e-12) / (energy(e.x, nullptr, i * hop, i * hop + w) + 1e-12));
    return on.empty() ? std::nan("") : sum / static_cast<double>(on.size());
}

std::vector<float> render(const std::string& engine, const Excerpt& e, double shift, double amount, double& ms)
{
    std::vector<double> shiftCurve(e.midi.size(), 0.0), amountCurve(e.midi.size(), amount);
    for (size_t i = 0; i < e.midi.size(); ++i)
        if (e.midi[i] > 0.0) shiftCurve[i] = shift;
    const PsolaInput in{ e.x.data(), e.x.size(), e.sr, framePeriod, e.midi.data(), shiftCurve.data(), e.midi.size(), amountCurve.data(), engine == "levelmatch" };
    const auto t0 = std::chrono::steady_clock::now();
    auto y = psolaShift(in, 0, e.x.size());
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return y;
}

Row measure(const Excerpt& e, const std::string& engine, double shift, double amount, const std::vector<float>& y, double ms, double srcHf)
{
    const auto mo = harvestMidi(y, e.sr, 40.0);
    return { e.name, engine, shift, amount, peakOf(y), ms, highShareDb(y, e.sr) - srcHf, preEchoDb(e, y), voicedLevelDb(e.x, y, e.midi, e.sr), pumpingStdDb(e.x, y, e.midi, e.sr), hfSpikeDb(e, y), countClips(y),
             f0Stats(e.midi, mo, shift), formantStats(e.x, y, e.midi, mo, shift, amount, e.sr) };
}

bool loadExcerpt(const char* path, const char* name, double beginSec, double endSec, Excerpt& e)
{
    std::vector<float> all;
    if (! readWavMono(path, all, e.sr)) return false;
    e.name = name;
    e.x.assign(all.begin() + static_cast<long>(beginSec * e.sr), all.begin() + static_cast<long>(std::min<double>(endSec * e.sr, static_cast<double>(all.size()))));
    e.midi = harvestMidi(e.x, e.sr, 71.0);
    return true;
}
} // namespace

#include "VoiceCompareReport.h"

int main(int argc, char** argv)
{
    if (argc < 4) { std::fprintf(stderr, "usage: vt_voice_compare A.wav B.wav results.md [--level-match] [listenRawDir]\n"); return 2; }
    const bool both = argc > 4 && std::string(argv[4]) == "--level-match";
    const std::string listen = argc > (both ? 5 : 4) ? argv[both ? 5 : 4] : "";
    if (! listen.empty()) std::filesystem::create_directories(listen);
    Excerpt a, b;
    if (! loadExcerpt(argv[1], "A", 17.0, 23.5, a) || ! loadExcerpt(argv[2], "B", 24.0, 30.5, b)) { std::fprintf(stderr, "cannot read wav\n"); return 1; }
    std::vector<Row> rows;
    for (const auto* e : { &a, &b }) runExcerpt(*e, both, listen, rows);
    return writeReport(argv[3], rows, { &a, &b }) ? 0 : 1;
}
