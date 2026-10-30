#include "ToolIcons.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr float iconGrid = 16.0f;
constexpr float iconSize = 14.0f;
constexpr float iconGap = 5.0f;

void arrowHead(juce::Path& p, float x, float y, float dx, float dy)
{
    // (x, y)가 끝점, (dx, dy)는 화살 방향의 단위 벡터
    const auto s = 2.4f;
    p.startNewSubPath(x - dx * s - dy * s, y - dy * s + dx * s);
    p.lineTo(x, y);
    p.lineTo(x - dx * s + dy * s, y - dy * s - dx * s);
}

juce::Path moveIcon()
{
    juce::Path p;
    p.startNewSubPath(8, 1.5f); p.lineTo(8, 14.5f);
    p.startNewSubPath(1.5f, 8); p.lineTo(14.5f, 8);
    arrowHead(p, 8, 1.5f, 0, -1);  arrowHead(p, 8, 14.5f, 0, 1);
    arrowHead(p, 1.5f, 8, -1, 0);  arrowHead(p, 14.5f, 8, 1, 0);
    return p;
}

juce::Path cutIcon()
{
    juce::Path p;
    p.addEllipse(2, 10, 4, 4);
    p.addEllipse(10, 10, 4, 4);
    p.startNewSubPath(5.5f, 10.5f); p.lineTo(12, 1.5f);
    p.startNewSubPath(10.5f, 10.5f); p.lineTo(4, 1.5f);
    return p;
}

juce::Path volumeIcon()
{
    juce::Path p;
    p.startNewSubPath(2, 6); p.lineTo(5, 6); p.lineTo(8.5f, 2.5f); p.lineTo(8.5f, 13.5f); p.lineTo(5, 10); p.lineTo(2, 10);
    p.closeSubPath();
    p.addCentredArc(9, 8, 3, 3, 0, 0.6f, 2.55f, true);
    p.addCentredArc(9, 8, 5.5f, 5.5f, 0, 0.6f, 2.55f, true);
    return p;
}

// y = centre + amplitude(x) * sin(...) 를 x0..x1 구간에 이어 그림
template <class Amplitude>
void wave(juce::Path& p, float x0, float x1, float centre, float cycles, Amplitude amplitude)
{
    constexpr int steps = 24;
    for (int i = 0; i <= steps; ++i)
    {
        const auto t = static_cast<float>(i) / steps;
        const auto x = x0 + (x1 - x0) * t;
        const auto y = centre - amplitude(t) * std::sin(t * cycles * juce::MathConstants<float>::twoPi);
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
}

juce::Path driftIcon()
{
    // 느리게 떠도는 음정이 가운데 선으로 모이는 모양
    juce::Path p;
    wave(p, 1.5f, 14.5f, 8, 1.0f, [](float t) { return 5.0f * (1.0f - t); });
    for (float x = 2; x < 15; x += 3)
    {
        p.startNewSubPath(x, 14); p.lineTo(x + 1.5f, 14);
    }
    return p;
}

juce::Path vibratoIcon()
{
    juce::Path p;
    wave(p, 1.5f, 14.5f, 8, 3.0f, [](float) { return 4.0f; });
    return p;
}

juce::Path flattenIcon()
{
    // 흔들림이 점점 줄어 곧은 선이 되는 모양
    juce::Path p;
    wave(p, 1.5f, 9.5f, 8, 2.0f, [](float t) { return 5.0f * (1.0f - t); });
    p.lineTo(14.5f, 8);
    return p;
}

juce::Path upDownIcon()
{
    juce::Path p;
    p.startNewSubPath(8, 1.5f); p.lineTo(8, 14.5f);
    arrowHead(p, 8, 1.5f, 0, -1);
    arrowHead(p, 8, 14.5f, 0, 1);
    return p;
}

juce::Path leftRightIcon()
{
    juce::Path p;
    p.startNewSubPath(1.5f, 8); p.lineTo(14.5f, 8);
    arrowHead(p, 1.5f, 8, -1, 0);
    arrowHead(p, 14.5f, 8, 1, 0);
    return p;
}

juce::Path undoIcon()
{
    juce::Path p;
    p.addCentredArc(9, 9, 5, 5, 0, -1.4f, 1.9f, true);
    arrowHead(p, 4.0f, 8.6f, 0.0f, 1.0f);
    return p;
}

juce::Path balanceIcon()
{
    // 저울: 기둥, 가로대, 양쪽 접시
    juce::Path p;
    p.startNewSubPath(8, 2); p.lineTo(8, 14);
    p.startNewSubPath(5, 14); p.lineTo(11, 14);
    p.startNewSubPath(2, 4); p.lineTo(14, 4);
    p.startNewSubPath(3.5f, 4); p.lineTo(1.5f, 9); p.lineTo(5.5f, 9); p.closeSubPath();
    p.startNewSubPath(12.5f, 4); p.lineTo(10.5f, 9); p.lineTo(14.5f, 9); p.closeSubPath();
    return p;
}
juce::Path mirrored(juce::Path p)
{
    p.applyTransform(juce::AffineTransform::scale(-1.0f, 1.0f, iconGrid * 0.5f, 0.0f));
    return p;
}

float textWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}
} // namespace

juce::Path makeToolIcon(ToolIcon icon)
{
    switch (icon)
    {
        case ToolIcon::Move:      return moveIcon();
        case ToolIcon::Cut:       return cutIcon();
        case ToolIcon::Volume:    return volumeIcon();
        case ToolIcon::Drift:     return driftIcon();
        case ToolIcon::Vibrato:   return vibratoIcon();
        case ToolIcon::Flatten:   return flattenIcon();
        case ToolIcon::UpDown:    return upDownIcon();
        case ToolIcon::LeftRight: return leftRightIcon();
        case ToolIcon::Undo:      return undoIcon();
        case ToolIcon::Redo:      return mirrored(undoIcon());
        case ToolIcon::Balance:   return balanceIcon();
        case ToolIcon::None:      break;
    }
    return {};
}

// ------------------------------------------------------------------ IconTextButton

void IconTextButton::setIcon(ToolIcon newIcon)
{
    if (icon == newIcon)
        return;
    icon = newIcon;
    iconPath = makeToolIcon(icon);
    repaint();
}

juce::Colour IconTextButton::currentTextColour() const
{
    const auto id = getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId;
    return findColour(id).withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f);
}

void IconTextButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    if (icon == ToolIcon::None)
        return juce::TextButton::paintButton(g, highlighted, down);

    auto& lf = getLookAndFeel();
    const auto background = findColour(getToggleState() ? buttonOnColourId : buttonColourId);
    lf.drawButtonBackground(g, *this, background, highlighted, down);

    const auto font = lf.getTextButtonFont(*this, getHeight());
    const auto contentWidth = iconSize + iconGap + textWidth(font, getButtonText());
    auto area = getLocalBounds().toFloat();
    const auto left = std::max(4.0f, (area.getWidth() - contentWidth) * 0.5f);

    const auto iconBox = juce::Rectangle<float>(left, (area.getHeight() - iconSize) * 0.5f, iconSize, iconSize);
    const auto fit = juce::AffineTransform::scale(iconSize / iconGrid).translated(iconBox.getX(), iconBox.getY());
    g.setColour(currentTextColour());
    g.strokePath(iconPath, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), fit);

    g.setFont(font);
    g.drawText(getButtonText(), area.withLeft(iconBox.getRight() + iconGap), juce::Justification::centredLeft, false);
}
