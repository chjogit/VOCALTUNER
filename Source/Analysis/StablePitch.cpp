#include "StablePitch.h"
#include <algorithm>
#include <cmath>

double medianOf(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    const auto mid = v.size() / 2;
    return v.size() % 2 == 1 ? v[mid] : 0.5 * (v[mid - 1] + v[mid]);
}

namespace
{
// i 둘레 유성 프레임(자기 자신 제외)의 음정 중앙값. 이웃이 없으면 0
double neighbourMedian(const VocalFeatures& f, size_t begin, size_t end, size_t i)
{
    std::vector<double> near;
    for (auto j = (i > stableNeighbourFrames ? i - stableNeighbourFrames : 0); j <= i + stableNeighbourFrames && j < f.size(); ++j)
        if (j != i && j >= begin && j < end && f.midiF0[j] > 0.0)
            near.push_back(f.midiF0[j]);
    return near.size() >= 2 ? medianOf(near) : 0.0;
}
} // namespace

std::vector<double> centreMidiFrames(const VocalFeatures& f, size_t begin, size_t end)
{
    std::vector<double> all, stable;
    for (auto i = begin; i < end; ++i)
    {
        if (f.midiF0[i] <= 0.0)
            continue;
        all.push_back(f.midiF0[i]);
        if (const auto near = neighbourMedian(f, begin, end, i); near > 0.0 && std::abs(f.midiF0[i] - near) <= stableToleranceSemitones)
            stable.push_back(f.midiF0[i]);
    }
    const auto enough = stable.size() >= stableMinFrames && static_cast<double>(stable.size()) >= stableMinShare * static_cast<double>(all.size());
    return enough ? stable : all;
}
