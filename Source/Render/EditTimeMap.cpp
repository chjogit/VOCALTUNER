#include "EditTimeMap.h"
#include <algorithm>
#include <cmath>

EditTimeMap::EditTimeMap(const std::vector<VocalBlock>& blocks)
{
    double lastDisplay = -1e12, lastSource = -1e12;
    const auto add = [&](double d, double s)
    {
        d = std::max(d, lastDisplay);
        s = std::max(s, lastSource);
        display.push_back(d);
        source.push_back(s);
        lastDisplay = d;
        lastSource = s;
        identity = identity && std::abs(d - s) < 1e-9;
    };
    for (const auto& b : blocks)
    {
        add(b.displayStartSec(), b.startSec);
        add(b.displayEndSec(), b.endSec);
    }
}

EditTimeMap::EditTimeMap(const std::vector<VocalBlock>& blocks, const VocalFeatures& f) : EditTimeMap(blocks)
{
    if (identity || f.size() == 0)
        return;
    const auto period = f.framePeriodMs / 1000.0;
    constexpr size_t attackFrames = 8;   // 소리가 선 뒤 40 ms
    constexpr size_t jumpFrames = 3;     // 음량이 15 ms 사이 6 dB 넘게 오른 곳(클릭·어택)과 그 뒤 15 ms
    const auto jumps = [&](size_t j) {
        if (j < jumpFrames || j >= f.rms.size()) return false;
        double low = f.rms[j];
        for (size_t k = 1; k <= jumpFrames; ++k) low = std::min(low, f.rms[j - k]);
        return f.rms[j] > 2.0 * low + 1e-6;
    };
    // 프레임마다: 0 = 늘여도 되는 모음, 1 = 늘이면 안 됨(무성·소리 시작·클릭), 2 = 음정이 빨리 움직임(꺾임·미끄러짐: 5 ms에 0.15반음 넘게)
    std::vector<unsigned char> kind(f.size(), 0);
    for (size_t j = 0; j < f.size(); ++j)
    {
        bool hard = f.midiF0[j] <= 0.0;
        for (size_t k = 1; k <= attackFrames && ! hard; ++k)
            hard = j < k || f.midiF0[j - k] <= 0.0;
        for (size_t k = 0; k <= jumpFrames && ! hard; ++k)
            hard = j >= k && jumps(j - k);
        const bool moving = ! hard && ((j + 1 < f.size() && f.midiF0[j + 1] > 0.0 && std::abs(f.midiF0[j + 1] - f.midiF0[j]) > 0.15)
                                       || (j > 0 && std::abs(f.midiF0[j] - f.midiF0[j - 1]) > 0.15));
        kind[j] = hard ? 1 : moving ? 2 : 0;
    }
    // 음정 움직임은 30 ms 넘게 이어질 때만(꺾임·미끄러짐) 고정. 비브라토의 짧은 오르내림까지 고정하면 모음 안에 짧은 고정 조각이
    // 잘게 생겨 합성과 원본 복사가 25 ms마다 번갈아 이어진다
    for (size_t j = 0; j < kind.size();)
    {
        if (kind[j] != 2) { ++j; continue; }
        auto end = j;
        while (end < kind.size() && kind[end] == 2) ++end;
        if (end - j < 6)
            for (auto k = j; k < end; ++k) kind[k] = 0;
        j = end;
    }
    const auto elastic = [&](size_t j) { return j < kind.size() && kind[j] == 0; };
    std::vector<double> d2 { display.front() }, s2 { source.front() };
    for (size_t n = 1; n < display.size(); ++n)
    {
        const auto s0 = source[n - 1], s1 = source[n], d0 = display[n - 1], d1 = display[n];
        if (s1 - s0 < 1e-9 || std::abs((d1 - d0) - (s1 - s0)) < 1e-9)
        {
            d2.push_back(d1); s2.push_back(s1);
            continue;
        }
        // [s0, s1)을 탄력/고정 조각으로
        struct Piece { double length; bool stretch; };
        std::vector<Piece> pieces;
        double fixedLength = 0.0, stretchLength = 0.0;
        const auto split = [&](const auto& stretchable) {
            pieces.clear();
            fixedLength = stretchLength = 0.0;
            for (auto t = s0; t < s1 - 1e-12;)
            {
                const auto j = static_cast<size_t>(std::max(0.0, std::floor(t / period + 1e-9)));
                const auto end = std::min(s1, static_cast<double>(j + 1) * period);
                const bool e = stretchable(j);
                if (! pieces.empty() && pieces.back().stretch == e) pieces.back().length += end - t;
                else pieces.push_back({ end - t, e });
                (e ? stretchLength : fixedLength) += end - t;
                t = end;
            }
        };
        split(elastic);
        // 모음이 없는 구간(블록 사이 틈): 숨·잡음은 그대로 두고, 거의 무음인 프레임만 늘이고 줄인다 (앞뒤 소리에서 20 ms 넘게 떨어진 곳)
        if (stretchLength <= 0.0)
            split([&](size_t j) {
                constexpr double silentRms = 0.01;
                constexpr size_t guard = 4;
                for (auto k = j >= guard ? j - guard : 0; k <= std::min(f.size() - 1, j + guard); ++k)
                    if (f.midiF0[k] > 0.0 || f.rms[k] >= silentRms) return false;
                return true;
            });
        if (stretchLength <= 0.0)
        {
            d2.push_back(d1); s2.push_back(s1);   // 모음이 없으면 고르게 (예전 방식)
            continue;
        }
        // 모음 몫은 0.2~3배까지. 그래도 모자라거나 남는 만큼만 고정 조각이 고르게 나눠 맡는다
        const auto k = std::clamp((d1 - d0 - fixedLength) / stretchLength, 0.2, 3.0);
        const auto r = fixedLength > 0.0 ? (d1 - d0 - k * stretchLength) / fixedLength : 1.0;
        auto d = d0, s = s0;
        for (size_t i = 0; i < pieces.size(); ++i)
        {
            s += pieces[i].length;
            d += (pieces[i].stretch ? k : r) * pieces[i].length;
            d2.push_back(i + 1 == pieces.size() ? d1 : d);
            s2.push_back(i + 1 == pieces.size() ? s1 : s);
        }
    }
    display = std::move(d2);
    source = std::move(s2);
}

namespace
{
double interpolate(const std::vector<double>& from, const std::vector<double>& to, double t) noexcept
{
    if (from.empty())
        return t;
    if (t <= from.front())
        return to.front() + (t - from.front());
    if (t >= from.back())
        return to.back() + (t - from.back());
    const auto hi = static_cast<size_t>(std::upper_bound(from.begin(), from.end(), t) - from.begin());
    const auto lo = hi - 1;
    const auto span = from[hi] - from[lo];
    return span > 0.0 ? to[lo] + (t - from[lo]) / span * (to[hi] - to[lo]) : to[hi];
}
} // namespace

double EditTimeMap::toSource(double displaySec) const noexcept { return interpolate(display, source, displaySec); }
double EditTimeMap::toDisplay(double sourceSec) const noexcept { return interpolate(source, display, sourceSec); }
