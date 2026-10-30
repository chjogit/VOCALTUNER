#include "Logo.h"
#include <algorithm>
#include <cmath>
#include "PianoRollStyle.h"

namespace
{
constexpr float grid = 30.0f;   // 30x30 격자에 그린 뒤 area에 맞춘다

juce::Path logoWave()
{
    juce::Path p;
    p.startNewSubPath(3.0f, 15.0f);
    p.lineTo(5.0f, 15.0f);
    for (int i = 0; i <= 60; ++i)
    {
        const auto t = static_cast<float>(i) / 60.0f;
        const auto fade = t < 0.85f ? 1.0f : std::max(0.0f, (1.0f - t) / 0.15f);
        const auto amplitude = 8.0f * std::exp(-3.2f * t) * fade;
        p.lineTo(5.0f + 12.0f * t, 15.0f - amplitude * std::sin(t * juce::MathConstants<float>::twoPi * 2.6f));
    }
    p.lineTo(27.0f, 15.0f);
    return p;
}
} // namespace

void drawLogo(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto size = std::min(area.getWidth(), area.getHeight());
    const auto scale = size / grid;
    const auto fit = juce::AffineTransform::scale(scale).translated(area.getCentreX() - size * 0.5f, area.getCentreY() - size * 0.5f);
    const auto frame = juce::Rectangle<float>(1.0f, 1.0f, 28.0f, 28.0f).transformedBy(fit);

    g.setColour(rollstyle::logoNavy);
    g.fillRoundedRectangle(frame, 3.0f * scale);
    g.setColour(rollstyle::referenceBeige.withAlpha(0.22f));
    juce::Path lines;
    for (const auto v : { 9.0f, 15.0f, 21.0f })
    {
        lines.startNewSubPath(v, 3.0f); lines.lineTo(v, 27.0f);
        lines.startNewSubPath(3.0f, v); lines.lineTo(27.0f, v);
    }
    g.strokePath(lines, juce::PathStrokeType(0.6f), fit);

    g.setColour(rollstyle::referenceBeige);
    g.drawRoundedRectangle(frame, 3.0f * scale, 1.6f * scale);
    g.strokePath(logoWave(), juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), fit);
}
