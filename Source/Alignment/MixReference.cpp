#include "MixReference.h"
#include <cmath>

MelodyTrack melodyForTarget(int64_t referenceLength, int referenceRate, const StereoReader& readReference,
                            const VocalFeatures& target, double targetOffset, const TimeMap& targetToReference,
                            double referenceOffset, const std::function<bool()>& shouldCancel)
{
    const TimeMap referenceToTarget(targetToReference.to(), targetToReference.from());
    const auto period = target.framePeriodMs / 1000.0;
    const auto guide = [&](double referenceSec)
    {
        const auto targetSec = referenceToTarget.map(referenceSec + referenceOffset) - targetOffset;
        const auto index = static_cast<long>(std::lround(targetSec / period));
        return index >= 0 && index < static_cast<long>(target.size()) ? target.midiF0[static_cast<size_t>(index)] : 0.0;
    };
    GuidedMelodyOptions options;
    options.shouldCancel = shouldCancel;
    return extractGuidedMelody(referenceLength, referenceRate, readReference, guide, options);
}
