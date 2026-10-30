#include "EditTimeMap.h"
#include <algorithm>

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
