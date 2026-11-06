// PSOLA vs 위상 보코더 비교 측정 (JUCE 비의존, 테스트 아님)
//   vt_formant_measure results.md [wavDir]
// 합성 모음(/a/, /i/ x f0 120, 220 Hz, 5.5 Hz +-30 cent 비브라토, 0.3초 무음 뒤 급한 시작)을 이동 x 포먼트 양 x 엔진으로 렌더하고,
// 출력 오디오만으로 포먼트(LPC 중앙값), 음정(Harvest), 프리에코, 4-8 kHz 에너지 변화, 시간을 잰다. 내부 목표값은 쓰지 않는다.
#include "../Tests/VowelSynth.h"
#include "../Source/Analysis/VocalFeatures.h"
#include "../Source/Render/PhaseVocoder.h"
#include "../Source/Render/Psola.h"
#include "WavIO.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <world/fft.h>

namespace
{
constexpr int sr = 44100;
constexpr double framePeriod = 0.005, onsetSec = 0.3, endSec = 1.9, totalSec = 2.0, pi = 3.14159265358979323846;
constexpr double measureBegin = 0.5, measureEnd = 1.8;

double midiOf(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

struct Source
{
    std::string name;
    std::vector<float> x;
    std::vector<double> midi;   // 프레임당 원본 음정 (0 = 무성)
    double f0 = 0.0;
    Formants formants;          // 원본에서 잰 중앙값
};

double vibratoRatio(double t) { return std::pow(2.0, 0.3 / 12.0 * std::sin(2.0 * pi * 5.5 * t)); }

// 비브라토 f0(t)의 성문 펄스열 (onset 이전은 0)
std::vector<double> pulseTrain(double f0)
{
    std::vector<double> x(static_cast<size_t>(totalSec * sr), 0.0);
    double phase = 1.0;   // 시작하자마자 첫 펄스
    for (auto i = static_cast<size_t>(onsetSec * sr); i < static_cast<size_t>(endSec * sr); ++i)
    {
        if (phase >= 1.0) { x[i] = 1.0; phase -= 1.0; }
        phase += f0 * vibratoRatio(static_cast<double>(i) / sr) / sr;
    }
    return x;
}

// 펄스열을 vowelsynth와 같은 필터에 통과시켜 최대 진폭 0.3으로 맞추고, 프레임별 원본 음정을 채운다
std::vector<float> synthVibrato(const vowelsynth::Vowel& v, double f0, std::vector<double>& midi)
{
    auto x = pulseTrain(f0);
    vowelsynth::onePole(x, 0.97);
    vowelsynth::onePole(x, 0.97);
    for (int i = 0; i < 4; ++i) vowelsynth::resonate(x, v.hz[i], v.bw[i], sr);
    double top = 1e-12;
    for (const auto s : x) top = std::max(top, std::abs(s));
    midi.assign(static_cast<size_t>(totalSec / framePeriod) + 1, 0.0);
    for (size_t f = 0; f < midi.size(); ++f)
    {
        const auto t = static_cast<double>(f) * framePeriod;
        if (t >= onsetSec && t < endSec) midi[f] = midiOf(f0 * vibratoRatio(t));
    }
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float>(0.3 * x[i] / top);
    return y;
}

double median(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// 0.5-1.8초에서 2048 창, 1024 간격의 LPC 포먼트 중앙값
Formants medianFormants(const std::vector<float>& y)
{
    std::vector<double> f[2];
    for (size_t s = static_cast<size_t>(measureBegin * sr); s + 2048 < static_cast<size_t>(measureEnd * sr); s += 1024)
    {
        const auto fm = vowelsynth::measure(y, s, 2048, sr);
        for (int i = 0; i < 2 && fm.count > i; ++i) f[i].push_back(fm.hz[i]);
    }
    Formants out;
    out.hz[0] = median(f[0]);
    out.hz[1] = median(f[1]);
    out.count = 2;
    return out;
}

// 출력 음정의 원본 x 2^(이동/12) 대비 오차 (cent, 절대값 중앙값)와 유성으로 잡힌 비율
void measureF0(const Source& src, const std::vector<float>& y, double shift, double& cents, double& voicedRatio)
{
    FeatureOptions opt;
    opt.f0FloorHz = 40.0;
    const auto f = *extractVocalFeatures(std::vector<double>(y.begin(), y.end()), sr, opt);
    std::vector<double> err;
    size_t wanted = 0;
    for (size_t i = 0; i < f.size() && i < src.midi.size(); ++i)
    {
        if (f.timeSec[i] < measureBegin || f.timeSec[i] > measureEnd || src.midi[i] <= 0.0) continue;
        ++wanted;
        if (f.midiF0[i] > 0.0) err.push_back(std::abs(f.midiF0[i] - (src.midi[i] + shift)) * 100.0);
    }
    cents = median(err);
    voicedRatio = wanted ? static_cast<double>(err.size()) / static_cast<double>(wanted) : 0.0;
}

double energy(const std::vector<float>& y, double fromSec, double toSec)
{
    double e = 0.0;
    for (auto i = static_cast<size_t>(fromSec * sr); i < static_cast<size_t>(toSec * sr); ++i) e += static_cast<double>(y[i]) * y[i];
    return e;
}

// 원본의 시작 직후 50 ms 에너지 대비, 시작 직전 50 ms 에너지 (dB)
double preEchoDb(const Source& src, const std::vector<float>& y)
{
    return 10.0 * std::log10((energy(y, onsetSec - 0.05, onsetSec) + 1e-12) / (energy(src.x, onsetSec, onsetSec + 0.05) + 1e-12));
}

// 0.5-1.8초 파워 합: 4-8 kHz와 전체
struct BandPower { double high = 0.0, total = 0.0; };

BandPower bandPower(const std::vector<float>& y)
{
    std::vector<double> in(2048), out(2050);
    auto plan = fft_plan_dft_r2c_1d(2048, in.data(), reinterpret_cast<fft_complex*>(out.data()), FFT_ESTIMATE);
    BandPower p;
    for (size_t s = static_cast<size_t>(measureBegin * sr); s + 2048 < static_cast<size_t>(measureEnd * sr); s += 1024)
    {
        for (size_t i = 0; i < 2048; ++i) in[i] = y[s + i] * (0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / 2048.0));
        fft_execute(plan);
        for (size_t k = 0; k <= 1024; ++k)
        {
            const auto power = out[2 * k] * out[2 * k] + out[2 * k + 1] * out[2 * k + 1];
            p.total += power;
            if (k >= 4000 * 2048 / sr && k <= 8000 * 2048 / sr) p.high += power;
        }
    }
    fft_destroy_plan(plan);
    return p;
}

struct Row { std::string engine, vowel; double f0, shift, amount, f1, f1Exp, f2, f2Exp, cents, voiced, preEcho, highBand, highShare, ms; };

std::vector<float> render(const std::string& engine, const Source& src, double shift, double amount, double& ms)
{
    std::vector<double> shiftCurve(src.midi.size(), 0.0), amountCurve(src.midi.size(), amount);
    for (size_t i = 0; i < src.midi.size(); ++i)
        if (src.midi[i] > 0.0) shiftCurve[i] = shift;
    const PsolaInput in{ src.x.data(), src.x.size(), sr, framePeriod, src.midi.data(), shiftCurve.data(), src.midi.size(), amountCurve.data() };
    const auto t0 = std::chrono::steady_clock::now();
    auto y = engine == "psola" ? psolaShift(in, 0, src.x.size()) : vocoderShift(in, 0, src.x.size());
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return y;
}

Row measureRow(const std::string& engine, const Source& src, double shift, double amount, const std::vector<float>& y, double ms)
{
    Row r{ engine, src.name, src.f0, shift, amount, 0, 0, 0, 0, 0, 0, 0, 0, 0, ms };
    const auto fm = medianFormants(y);
    const auto scale = std::pow(2.0, (1.0 - amount) * shift / 12.0);
    r.f1 = fm.hz[0]; r.f2 = fm.hz[1];
    r.f1Exp = src.formants.hz[0] * scale; r.f2Exp = src.formants.hz[1] * scale;
    measureF0(src, y, shift, r.cents, r.voiced);
    r.preEcho = preEchoDb(src, y);
    const auto out = bandPower(y), ref = bandPower(src.x);
    r.highBand = 10.0 * std::log10((out.high + 1e-12) / (ref.high + 1e-12));
    r.highShare = 10.0 * std::log10((out.high + 1e-12) / (out.total + 1e-12));
    return r;
}

std::string rowText(const Row& r)
{
    char line[400];
    std::snprintf(line, sizeof line, "| %s | %s | %.0f | %+.0f | %.1f | %.0f / %.0f (%+.0f) | %.0f / %.0f (%+.0f) | %.1f | %.0f%% | %.1f | %+.1f | %.1f | %.0f |\n",
                  r.engine.c_str(), r.vowel.c_str(), r.f0, r.shift, r.amount, r.f1, r.f1Exp, r.f1 - r.f1Exp, r.f2, r.f2Exp, r.f2 - r.f2Exp,
                  r.cents, r.voiced * 100.0, r.preEcho, r.highBand, r.highShare, r.ms);
    return line;
}

std::string summaryText(const std::string& engine, const std::vector<Row>& rows)
{
    double f1 = 0, f2 = 0, cents = 0, pre = 0, hb = 0, share = 0, ms = 0, voiced = 0;
    int n = 0;
    for (const auto& r : rows)
    {
        if (r.engine != engine) continue;
        f1 += std::abs(r.f1 / r.f1Exp - 1.0) * 100.0; f2 += std::abs(r.f2 / r.f2Exp - 1.0) * 100.0;
        cents += r.cents; pre += r.preEcho; hb += r.highBand; share += r.highShare; ms += r.ms; voiced += r.voiced; ++n;
    }
    char line[300];
    std::snprintf(line, sizeof line, "| %s | %.1f%% | %.1f%% | %.1f | %.0f%% | %.1f | %+.1f | %.1f | %.0f |\n", engine.c_str(), f1 / n, f2 / n,
                  cents / n, voiced / n * 100.0, pre / n, hb / n, share / n, ms / n);
    return line;
}

Source makeSource(const char* name, const vowelsynth::Vowel& v, double f0)
{
    Source s;
    s.name = name;
    s.f0 = f0;
    s.x = synthVibrato(v, f0, s.midi);
    s.formants = medianFormants(s.x);
    return s;
}
std::string wavName(const std::string& dir, const std::string& engine, const Source& src, double shift, double amount)
{
    char nm[200];
    std::snprintf(nm, sizeof nm, "%s/%s_%s_f%.0f_s%+.0f_a%.1f.wav", dir.c_str(), engine.c_str(), src.name.c_str(), src.f0, shift, amount);
    return nm;
}

// 한 원본에 대해 이동 x 양 x 엔진 전체를 렌더/측정
void runSource(const Source& src, const std::string& dir, std::vector<Row>& rows, std::string& table)
{
    for (const auto shift : { -12.0, -5.0, 7.0, 12.0 })
        for (const auto amount : { 0.0, 0.5, 1.0 })
            for (const std::string engine : { "psola", "vocoder" })
            {
                double ms = 0.0;
                const auto y = render(engine, src, shift, amount, ms);
                rows.push_back(measureRow(engine, src, shift, amount, y, ms));
                table += rowText(rows.back());
                if (! dir.empty()) writeWavMono16(wavName(dir, engine, src, shift, amount).c_str(), y, sr);
            }
}

bool writeReport(const char* path, const std::string& table, const std::vector<Row>& rows)
{
    std::FILE* out = std::fopen(path, "w");
    if (! out) return false;
    std::fprintf(out, "# PSOLA vs phase vocoder\n\nF1/F2: measured / expected (error Hz). F0 err: median |cents| vs source F0 x 2^(shift/12). voiced: share of voiced source frames Harvest finds voiced in the output.\n"
                      "pre-echo: dB of output energy in the 50 ms before onset relative to source energy in the 50 ms after onset. HF: change of 4-8 kHz energy vs source (dB; the synthetic source has almost no HF, so small absolute levels read as large changes). HF/total: output 4-8 kHz energy relative to the output's total energy (dB, absolute). ms: render time.\n\n"
                      "| engine | vowel | f0 | shift | amount | F1 | F2 | F0 err cents | voiced | pre-echo dB | HF dB | HF/total dB | ms |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n%s\n", table.c_str());
    std::fprintf(out, "## Mean over the grid\n\n| engine | F1 abs err | F2 abs err | F0 err cents | voiced | pre-echo dB | HF dB | HF/total dB | ms |\n|---|---|---|---|---|---|---|---|---|\n%s%s",
                 summaryText("psola", rows).c_str(), summaryText("vocoder", rows).c_str());
    std::fclose(out);
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: vt_formant_measure results.md [wavDir]\n"); return 2; }
    const std::string dir = argc > 2 ? argv[2] : "";
    if (! dir.empty()) std::filesystem::create_directories(dir);
    std::vector<Row> rows;
    std::string table;
    for (const auto& [vowelName, vowel] : { std::pair<const char*, vowelsynth::Vowel>{ "a", vowelsynth::vowelA() }, { "i", vowelsynth::vowelI() } })
        for (const auto f0 : { 120.0, 220.0 })
        {
            const auto src = makeSource(vowelName, vowel, f0);
            if (! dir.empty()) writeWavMono16((dir + "/source_" + vowelName + "_f" + std::to_string(static_cast<int>(f0)) + ".wav").c_str(), src.x, sr);
            runSource(src, dir, rows, table);
        }
    return writeReport(argv[1], table, rows) ? 0 : 1;
}
