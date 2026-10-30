#include "WordAnchors.h"
#include <algorithm>
#include <cmath>

namespace
{
struct TimedChar { char32_t code; double centreSec; };

char32_t nextCodePoint(const std::string& s, size_t& i) noexcept
{
    const auto c = static_cast<unsigned char>(s[i]);
    const int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 0;
    char32_t code = extra == 0 ? c : c & (0x3F >> extra);
    ++i;
    for (int k = 0; k < extra && i < s.size(); ++k, ++i)
        code = (code << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    return code;
}

bool isSignificant(char32_t c) noexcept
{
    if (c < 0x80)
        return std::isalnum(static_cast<int>(c)) != 0;
    const bool punctuation = (c >= 0x2000 && c <= 0x206F) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF00 && c <= 0xFF0F) || c == 0xFEFF;
    return ! punctuation;
}

std::vector<TimedChar> flatten(const std::vector<TimedWord>& words)
{
    std::vector<TimedChar> out;
    for (const auto& w : words)
    {
        std::vector<char32_t> codes;
        for (size_t i = 0; i < w.text.size();)
        {
            auto c = nextCodePoint(w.text, i);
            if (c < 0x80)
                c = static_cast<char32_t>(std::tolower(static_cast<int>(c)));
            if (isSignificant(c))
                codes.push_back(c);
        }
        const auto step = (w.endSec - w.startSec) / static_cast<double>(std::max<size_t>(1, codes.size()));
        for (size_t k = 0; k < codes.size(); ++k)
            out.push_back({ codes[k], w.startSec + (static_cast<double>(k) + 0.5) * step });
    }
    return out;
}

std::vector<WordAnchor> lcsPairs(const std::vector<TimedChar>& a, const std::vector<TimedChar>& b)
{
    const auto n = a.size(), m = b.size();
    std::vector<unsigned short> table((n + 1) * (m + 1), 0);
    const auto at = [&](size_t i, size_t j) -> unsigned short& { return table[i * (m + 1) + j]; };
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            at(i, j) = a[i].code == b[j].code ? static_cast<unsigned short>(at(i + 1, j + 1) + 1) : std::max(at(i + 1, j), at(i, j + 1));
    std::vector<WordAnchor> out;
    for (size_t i = 0, j = 0; i < n && j < m;)
    {
        if (a[i].code == b[j].code)
            out.push_back({ a[i++].centreSec, b[j++].centreSec });
        else if (at(i + 1, j) >= at(i, j + 1))
            ++i;
        else
            ++j;
    }
    return out;
}

std::vector<WordAnchor> withoutOutliers(const std::vector<WordAnchor>& pairs, double limitSec)
{
    constexpr long radius = 6;
    std::vector<WordAnchor> out;
    for (size_t i = 0; i < pairs.size(); ++i)
    {
        std::vector<double> shifts;
        for (auto k = std::max<long>(0, static_cast<long>(i) - radius); k <= std::min<long>(static_cast<long>(pairs.size()) - 1, static_cast<long>(i) + radius); ++k)
            shifts.push_back(pairs[static_cast<size_t>(k)].targetSec - pairs[static_cast<size_t>(k)].referenceSec);
        std::nth_element(shifts.begin(), shifts.begin() + static_cast<long>(shifts.size() / 2), shifts.end());
        const auto shift = pairs[i].targetSec - pairs[i].referenceSec;
        if (std::abs(shift - shifts[shifts.size() / 2]) <= limitSec)
            out.push_back(pairs[i]);
    }
    return out;
}
} // namespace

std::vector<WordAnchor> matchWords(const std::vector<TimedWord>& reference, const std::vector<TimedWord>& target, size_t maxChars,
                                   double outlierSec)
{
    const auto a = flatten(reference), b = flatten(target);
    if (a.empty() || b.empty() || a.size() > maxChars || b.size() > maxChars)
        return {};
    return withoutOutliers(lcsPairs(a, b), outlierSec);
}

TimeMap guideFromAnchors(const std::vector<WordAnchor>& anchors, size_t minAnchors)
{
    if (anchors.size() < minAnchors)
        return {};
    std::vector<double> from, to;
    for (const auto& a : anchors)
    {
        if (! from.empty() && (a.referenceSec <= from.back() || a.targetSec < to.back()))
            continue;
        from.push_back(a.referenceSec);
        to.push_back(a.targetSec);
    }
    return from.size() < minAnchors ? TimeMap{} : TimeMap(std::move(from), std::move(to));
}
