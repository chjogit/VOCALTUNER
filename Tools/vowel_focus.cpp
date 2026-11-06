// 모음 중심 이동 측정 (JUCE 비의존, 테스트 아님)
//   vt_vowel_focus A.wav B.wav outDir
// A는 17.0-23.5 s, B는 24.0-30.5 s. outDir 아래에 csv/, segments/(+index.txt), listen/, metrics.md를 만든다.
#include "VowelFocusRender.h"
#include <filesystem>

namespace
{
void report(const vf::Take& t, const std::string& out, std::FILE* md, std::FILE* index)
{
    namespace fs = std::filesystem;
    vf::writeCsv(t, out + "/csv/" + t.name + "_weights.csv");
    const auto segs = vf::lowSegments(t, 4);
    vf::exportSegments(t, segs, out + "/segments", index);
    double total = 0.0;
    for (const auto& s : segs) total += static_cast<double>(s.end - s.begin) * voicemetrics::framePeriod;
    std::fprintf(md, "\n## %s: w<0.5 segments (voiced, >= 20 ms): %zu, total %.2f s\n\n", t.name.c_str(), segs.size(), total);
    std::fprintf(md, "| shift | engine | F0 within 20c of full shift (w>=0.8) | within 20c of applied shift (w>=0.8) | within 20c of full shift (w=1 interior cores (+-40 ms all w=1)) | voiced w<0.5 share | mean abs shift on w<0.5 | HF p95 dB | voiced RMS dB |\n|---|---|---|---|---|---|---|---|---|\n");
    for (const auto st : { -2.0, 2.0, 5.0 })
    {
        const auto cur = vf::render(t, st, false), foc = vf::render(t, st, true);
        for (const auto& [label, r] : { std::pair<const char*, const vf::Render*>{ "current", &cur }, { "vowelFocus", &foc } })
        {
            const auto m = vf::measureRender(t, *r, st);
            std::fprintf(md, "| %+.0f | %s | %.1f%% (n=%zu) | %.1f%% | %.1f%% (n=%zu) | %.1f%% | %.2f st | %+.1f | %+.2f |\n", st, label, m.within20, m.strong, m.withinApplied, m.withinCore, m.core, m.lowShare, m.lowShift, m.hfSpike, m.levelDb);
        }
        char name[80];
        std::snprintf(name, sizeof name, "/listen/%s_%+.0fst.wav", t.name.c_str(), st);
        if (vf::writeListen(t, st, cur, foc, out + name)) std::fprintf(md, "\nnote: %s scaled down to avoid clipping\n\n", name);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 4) { std::fprintf(stderr, "usage: vt_vowel_focus A.wav B.wav outDir\n"); return 2; }
    const std::string out = argv[3];
    for (const char* d : { "/csv", "/segments", "/listen" }) std::filesystem::create_directories(out + d);
    vf::Take a, b;
    if (! vf::load(argv[1], "A", 17.0, 23.5, a) || ! vf::load(argv[2], "B", 24.0, 30.5, b)) { std::fprintf(stderr, "cannot read wav\n"); return 1; }
    std::FILE* md = std::fopen((out + "/metrics.md").c_str(), "w");
    std::FILE* index = std::fopen((out + "/segments/index.txt").c_str(), "w");
    for (const auto* t : { &a, &b }) report(*t, out, md, index);
    std::fclose(md);
    std::fclose(index);
    return 0;
}
