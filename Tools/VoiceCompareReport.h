// vt_voice_compare의 격자 실행과 보고서 (voice_compare.cpp 안에서만 포함)
#pragma once

namespace
{
bool isListenCase(double shift, double amount)
{
    constexpr double cases[][2] = { { -2, 1 }, { 2, 1 }, { -5, 1 }, { 7, 1 }, { -12, 1 }, { 12, 1 }, { -5, 0 }, { 7, 0 }, { 7, 0.5 } };
    return std::any_of(std::begin(cases), std::end(cases), [&](const auto& c) { return c[0] == shift && c[1] == amount; });
}

// 유성 프레임 RMS를 원본에 맞추는 배율 (처리 결과가 무음이면 1)
double matchGain(const Excerpt& e, const std::vector<float>& y)
{
    const auto have = voicedEnergy(y, e.midi, e.sr);
    return have > 1e-20 ? std::sqrt(voicedEnergy(e.x, e.midi, e.sr) / have) : 1.0;
}

std::vector<float> scaled(const std::vector<float>& y, double g)
{
    std::vector<float> out(y.size());
    for (size_t i = 0; i < y.size(); ++i) out[i] = static_cast<float>(y[i] * g);
    return out;
}

// 청취 조건이면 세 부분의 원시 파일(처리 부분은 레벨을 원본 유성 RMS에 맞춘 것)과 manifest(이름, 피크 3개, 배율 2개)를 쓴다
void exportListen(const Excerpt& e, const std::string& dir, double shift, double amount, const std::vector<float>& ps, const std::vector<float>& vc)
{
    char base[200];
    std::snprintf(base, sizeof base, "%s_%+.0fst_f%g", e.name.c_str(), shift, amount);
    const auto gp = matchGain(e, ps), gv = matchGain(e, vc);
    const auto psOut = scaled(ps, gp), vcOut = scaled(vc, gv);
    for (const auto& [tag, y] : { std::pair<const char*, const std::vector<float>*>{ "src", &e.x }, { "psola", &psOut }, { "vocoder", &vcOut } })
        writeRaw(dir + "/" + base + "_" + tag + ".f32", *y);
    std::FILE* m = std::fopen((dir + "/manifest.txt").c_str(), "a");
    std::fprintf(m, "%s %.6f %.6f %.6f %.6f %.6f\n", base, peakOf(e.x), peakOf(psOut), peakOf(vcOut), gp, gv);
    std::fclose(m);
}

void runExcerpt(const Excerpt& e, const std::string& listen, std::vector<Row>& rows)
{
    const auto srcHf = highShareDb(e.x, e.sr);
    for (const auto shift : { -12.0, -5.0, -2.0, 2.0, 7.0, 12.0 })
        for (const auto amount : { 0.0, 0.5, 1.0 })
        {
            double msP = 0.0, msV = 0.0;
            const auto ps = render("psola", e, shift, amount, msP), vc = render("vocoder", e, shift, amount, msV);
            rows.push_back(measure(e, "psola", shift, amount, ps, msP, srcHf));
            rows.push_back(measure(e, "vocoder", shift, amount, vc, msV, srcHf));
            if (! listen.empty() && isListenCase(shift, amount)) exportListen(e, listen, shift, amount, ps, vc);
            std::fprintf(stderr, "%s %+.0f %.1f done\n", e.name.c_str(), shift, amount);
        }
}

std::string rowText(const Row& r)
{
    char line[400];
    std::snprintf(line, sizeof line, "| %s | %s | %+.0f | %.1f | %.0f | %.0f%% | %.1f%% | %.0f%% | %.1f | %.1f | %.0f%% | %+.1f | %+.1f | %+.2f | %.3f | %zu | %.0f |\n",
                  r.ex.c_str(), r.engine.c_str(), r.shift, r.amount, r.f0.medCents, r.f0.within20, r.f0.over50, r.f0.kept, r.fm.f1, r.fm.f2, r.fm.measurable,
                  r.hfDb, r.preDb, r.levelDb, r.peak, r.clips, r.ms);
    return line;
}

struct Acc { double v[12] = {}; int n[12] = {}; };

void add(Acc& a, int k, double x) { if (! std::isnan(x)) { a.v[k] += x; ++a.n[k]; } }

std::string meanLine(const std::string& label, const std::vector<Row>& rows, const std::function<bool(const Row&)>& pick)
{
    Acc a;
    for (const auto& r : rows)
    {
        if (! pick(r)) continue;
        const double x[] = { r.f0.medCents, r.f0.within20, r.f0.kept, r.fm.f1, r.fm.f2, r.fm.measurable, r.hfDb, r.preDb, r.ms, static_cast<double>(r.clips), r.f0.over50, r.levelDb };
        for (int k = 0; k < 12; ++k) add(a, k, x[k]);
    }
    char line[300];
    const auto m = [&](int k) { return a.n[k] ? a.v[k] / a.n[k] : std::nan(""); };
    std::snprintf(line, sizeof line, "| %s | %.0f | %.0f%% | %.1f%% | %.0f%% | %.1f | %.1f | %.0f%% | %+.1f | %+.1f | %+.2f | %.0f | %.1f |\n", label.c_str(), m(0), m(1), m(10), m(2),
                  m(3), m(4), m(5), m(6), m(7), m(11), m(8), m(9));
    return line;
}

std::string meanTables(const std::vector<Row>& rows)
{
    const std::string head = "| group | F0 med cents | within 20c | >50c | voiced kept | F1 err % | F2 err % | formant measurable | HF/total dB chg | pre-echo dB | voiced RMS dB | ms | clipped samples |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    std::string t = "## Mean per engine and amount (mean of per-row medians over shifts and both excerpts)\n\n" + head;
    for (const std::string eng : { "psola", "vocoder" })
        for (const auto am : { 0.0, 0.5, 1.0 })
            t += meanLine(eng + " amount " + std::to_string(am).substr(0, 3), rows, [&](const Row& r) { return r.engine == eng && r.amount == am; });
    t += "\n## Mean per engine and excerpt\n\n" + head;
    for (const std::string eng : { "psola", "vocoder" })
        for (const std::string ex : { "A", "B" })
            t += meanLine(eng + " excerpt " + ex, rows, [&](const Row& r) { return r.engine == eng && r.ex == ex; });
    return t;
}

bool writeReport(const char* path, const std::vector<Row>& rows, const std::vector<const Excerpt*>& ex)
{
    std::FILE* out = std::fopen(path, "w");
    if (! out) return false;
    std::fprintf(out, "# Real-voice comparison: PSOLA vs phase vocoder\n\nExpected formant ratio is the design formula 2^((1-a)s/12) (a = formant amount, s = shift in semitones), not a measured truth. F0 >50c: share of frames voiced in both whose F0 is more than 50 cents from source x 2^(s/12). voiced RMS dB: RMS change over source-voiced frames.\n\n");
    for (const auto* e : ex)
        std::fprintf(out, "Input excerpt %s: %.2f s, peak %.4f, samples >= 0.999: %zu\n\n", e->name.c_str(), static_cast<double>(e->x.size()) / e->sr, peakOf(e->x), countClips(e->x));
    for (const auto* e : ex)
        std::fprintf(out, "Strong onsets used for pre-echo in %s: %zu (pre-echo is n/a with none)\n\n", e->name.c_str(), strongOnsets(*e).size());
    std::fprintf(out, "| ex | engine | shift | amount | F0 med abs cents | F0 within 20c | F0 >50c | voiced kept | F1 err %% | F2 err %% | formant measurable | HF/total dB chg | pre-echo dB | voiced RMS dB | peak | clipped | ms |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const auto& r : rows) std::fputs(rowText(r).c_str(), out);
    std::fprintf(out, "\n%s", meanTables(rows).c_str());
    std::fclose(out);
    return true;
}
} // namespace
