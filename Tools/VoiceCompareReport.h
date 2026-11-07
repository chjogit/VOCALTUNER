// vt_voice_compare의 격자 실행과 보고서 (voice_compare.cpp 안에서만 포함)
#pragma once

namespace
{
void writeRaw(const std::string& path, const std::vector<float>& y)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (! f) return;
    std::fwrite(y.data(), sizeof(float), y.size(), f);
    std::fclose(f);
}

// 청취 조건 (이동, 포먼트 양 1): 원본 / 현재 PSOLA / 음량 보정 PSOLA 원시 파일과 manifest(이름, 피크 3개)
void exportListen(const Excerpt& e, const std::string& dir, double shift, const std::vector<float>& cur, const std::vector<float>& lm)
{
    char base[100];
    std::snprintf(base, sizeof base, "%s_%+.0fst", e.name.c_str(), shift);
    for (const auto& [tag, y] : { std::pair<const char*, const std::vector<float>*>{ "src", &e.x }, { "cur", &cur }, { "lm", &lm } })
        writeRaw(dir + "/" + base + "_" + tag + ".f32", *y);
    std::FILE* m = std::fopen((dir + "/manifest.txt").c_str(), "a");
    std::fprintf(m, "%s %.6f %.6f %.6f\n", base, peakOf(e.x), peakOf(cur), peakOf(lm));
    std::fclose(m);
}

void runCondition(const Excerpt& e, bool both, const std::string& listen, double shift, double amount, double srcHf, std::vector<Row>& rows)
{
    double ms = 0.0, msL = 0.0;
    const auto y = render("psola", e, shift, amount, ms);
    rows.push_back(measure(e, "psola", shift, amount, y, ms, srcHf));
    if (! both) return;
    const auto lm = render("levelmatch", e, shift, amount, msL);
    rows.push_back(measure(e, "levelmatch", shift, amount, lm, msL, srcHf));
    if (! listen.empty() && amount == 1.0 && (shift == -2 || shift == 2 || shift == -5 || shift == 7)) exportListen(e, listen, shift, y, lm);
}

void runExcerpt(const Excerpt& e, bool both, const std::string& listen, std::vector<Row>& rows)
{
    const auto srcHf = highShareDb(e.x, e.sr);
    for (const auto shift : { -12.0, -5.0, -2.0, 2.0, 7.0, 12.0 })
        for (const auto amount : { 0.0, 0.5, 1.0 })
        {
            runCondition(e, both, listen, shift, amount, srcHf, rows);
            std::fprintf(stderr, "%s %+.0f %.1f done\n", e.name.c_str(), shift, amount);
        }
}

std::string rowText(const Row& r)
{
    char line[400];
    std::snprintf(line, sizeof line, "| %s | %s | %+.0f | %.1f | %.0f | %.0f%% | %.1f%% | %.0f%% | %.1f | %.1f | %.0f%% | %+.1f | %+.1f | %+.2f | %.2f | %+.1f | %.3f | %zu | %.0f |\n",
                  r.ex.c_str(), r.engine.c_str(), r.shift, r.amount, r.f0.medCents, r.f0.within20, r.f0.over50, r.f0.kept, r.fm.f1, r.fm.f2, r.fm.measurable,
                  r.hfDb, r.preDb, r.levelDb, r.pumpDb, r.hfSpike, r.peak, r.clips, r.ms);
    return line;
}

struct Acc { double v[14] = {}; int n[14] = {}; };

void add(Acc& a, int k, double x) { if (! std::isnan(x)) { a.v[k] += x; ++a.n[k]; } }

std::string meanLine(const std::string& label, const std::vector<Row>& rows, const std::function<bool(const Row&)>& pick)
{
    Acc a;
    for (const auto& r : rows)
    {
        if (! pick(r)) continue;
        const double x[] = { r.f0.medCents, r.f0.within20, r.f0.kept, r.fm.f1, r.fm.f2, r.fm.measurable, r.hfDb, r.preDb, r.ms, static_cast<double>(r.clips), r.f0.over50, r.levelDb, r.pumpDb, r.hfSpike };
        for (int k = 0; k < 14; ++k) add(a, k, x[k]);
    }
    char line[300];
    const auto m = [&](int k) { return a.n[k] ? a.v[k] / a.n[k] : std::nan(""); };
    std::snprintf(line, sizeof line, "| %s | %.0f | %.0f%% | %.1f%% | %.0f%% | %.1f | %.1f | %.0f%% | %+.1f | %+.1f | %+.2f | %.2f | %+.1f | %.0f | %.1f |\n", label.c_str(), m(0), m(1), m(10), m(2),
                  m(3), m(4), m(5), m(6), m(7), m(11), m(12), m(13), m(8), m(9));
    return line;
}

std::string meanTables(const std::vector<Row>& rows)
{
    const std::string head = "| group | F0 med cents | within 20c | >50c | voiced kept | F1 err % | F2 err % | formant measurable | HF/total dB chg | pre-echo dB | voiced RMS dB | pumping dB | HF p95 dB | ms | clipped samples |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    std::string t = "## Mean per engine and amount (mean of per-row medians over shifts and both excerpts)\n\n" + head;
    for (const std::string eng : { "psola", "levelmatch" })
        for (const auto am : { 0.0, 0.5, 1.0 })
            t += meanLine(eng + " amount " + std::to_string(am).substr(0, 3), rows, [&](const Row& r) { return r.engine == eng && r.amount == am; });
    t += "\n## Mean per engine and excerpt\n\n" + head;
    for (const std::string eng : { "psola", "levelmatch" })
        for (const std::string ex : { "A", "B" })
            t += meanLine(eng + " excerpt " + ex, rows, [&](const Row& r) { return r.engine == eng && r.ex == ex; });
    return t;
}

bool writeReport(const char* path, const std::vector<Row>& rows, const std::vector<const Excerpt*>& ex)
{
    std::FILE* out = std::fopen(path, "w");
    if (! out) return false;
    std::fprintf(out, "# Real-voice comparison: PSOLA\n\nExpected formant ratio is the design formula 2^((1-a)s/12) (a = formant amount, s = shift in semitones), not a measured truth. F0 >50c: share of frames voiced in both whose F0 is more than 50 cents from source x 2^(s/12). voiced RMS dB: RMS change over source-voiced frames.\n\n");
    for (const auto* e : ex)
        std::fprintf(out, "Input excerpt %s: %.2f s, peak %.4f, samples >= 0.999: %zu\n\n", e->name.c_str(), static_cast<double>(e->x.size()) / e->sr, peakOf(e->x), countClips(e->x));
    for (const auto* e : ex)
        std::fprintf(out, "Strong onsets used for pre-echo in %s: %zu (pre-echo is n/a with none)\n\n", e->name.c_str(), strongOnsets(*e).size());
    std::fprintf(out, "| ex | engine | shift | amount | F0 med abs cents | F0 within 20c | F0 >50c | voiced kept | F1 err %% | F2 err %% | formant measurable | HF/total dB chg | pre-echo dB | voiced RMS dB | pumping std dB | HF share p95 dB | peak | clipped | ms |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const auto& r : rows) std::fputs(rowText(r).c_str(), out);
    std::fprintf(out, "\n%s", meanTables(rows).c_str());
    std::fclose(out);
    return true;
}
} // namespace
