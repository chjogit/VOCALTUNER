#include "FloatingToolbar.h"
#include <utility>
#include "PianoRollStyle.h"

FloatingToolbar::FloatingToolbar()
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void FloatingToolbar::setItems(std::vector<Item> newItems)
{
    items = std::move(newItems);
    for (auto& item : items)
        if (item.component->getParentComponent() != this)
            addChildComponent(item.component);
    layoutItems();
}

void FloatingToolbar::setDocked(bool shouldDock)
{
    docked = shouldDock;
    repaint();
}

juce::Rectangle<int> FloatingToolbar::body() const
{
    return getLocalBounds().withTrimmedRight(shadow).withTrimmedBottom(shadow);
}

int FloatingToolbar::preferredWidth() const
{
    auto width = gripWidth + pinWidth + shadow;
    for (const auto& item : items)
        if (item.component->isVisible())
            width += item.width + (item.separatorBefore ? separatorGap : 0);
    return width;
}

void FloatingToolbar::layoutItems()
{
    separators.clear();
    auto x = gripWidth;
    const auto h = body().getHeight();
    for (const auto& item : items)
    {
        if (! item.component->isVisible())
            continue;
        if (item.separatorBefore)
        {
            separators.push_back(x + separatorGap / 2);
            x += separatorGap;
        }
        item.component->setBounds(x, 1, item.width, h - 2);
        x += item.width;
    }
    repaint();
}

void FloatingToolbar::paint(juce::Graphics& g)
{
    const auto b = body().toFloat();
    constexpr auto corner = 5.0f;
    if (! docked)
    {
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(b.translated(static_cast<float>(shadow), static_cast<float>(shadow)), corner);
    }
    g.setColour(rollstyle::chrome);
    g.fillRoundedRectangle(b, corner);
    g.setColour(rollstyle::chromeEdge);
    g.drawRoundedRectangle(b.reduced(0.5f), corner, 1.0f);

    const auto grip = gripArea();
    g.setColour(rollstyle::inkFaint);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 2; ++col)
            g.fillRect(grip.getCentreX() - 3 + col * 4, grip.getCentreY() - 5 + row * 4, 2, 2);
    g.setColour(juce::Colour(0xff2a2926));
    g.drawVerticalLine(grip.getRight(), b.getY() + 6.0f, b.getBottom() - 6.0f);
    for (const auto x : separators)
        g.drawVerticalLine(x, b.getY() + 8.0f, b.getBottom() - 8.0f);

    // 핀: 붙어 있으면 아래로 꽂힌 모양, 떠 있으면 위로 올리는 화살표
    const auto pin = pinArea().toFloat();
    g.drawVerticalLine(static_cast<int>(pin.getX()), b.getY() + 6.0f, b.getBottom() - 6.0f);
    g.setColour(docked ? rollstyle::ink : rollstyle::inkDim);
    juce::Path p;
    const auto c = pin.getCentre();
    p.startNewSubPath(c.x - 4.0f, c.y - 5.0f); p.lineTo(c.x + 4.0f, c.y - 5.0f);
    p.startNewSubPath(c.x, c.y - 2.0f); p.lineTo(c.x, c.y + 5.0f);
    p.startNewSubPath(c.x - 3.0f, docked ? c.y + 2.0f : c.y + 1.0f); p.lineTo(c.x, docked ? c.y + 5.0f : c.y - 2.0f);
    p.lineTo(c.x + 3.0f, docked ? c.y + 2.0f : c.y + 1.0f);
    g.strokePath(p, juce::PathStrokeType(1.3f));
}

void FloatingToolbar::mouseDown(const juce::MouseEvent& e)
{
    dragging = gripArea().contains(e.getPosition()) && ! docked;
    dragStart = getPosition();
    if (pinArea().contains(e.getPosition()) && onPinClicked)
        onPinClicked();
}

void FloatingToolbar::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging && onMoved)
        onMoved(dragStart + e.getOffsetFromDragStart());
}

void FloatingToolbar::mouseUp(const juce::MouseEvent&)
{
    if (std::exchange(dragging, false) && onDropped)
        onDropped();
}

juce::String FloatingToolbar::getTooltip()
{
    const auto p = getMouseXYRelative();
    if (pinArea().contains(p))
        return juce::String::fromUTF8(docked ? "떼어서 띄우기" : "위에 붙이기");
    if (gripArea().contains(p) && ! docked)
        return juce::String::fromUTF8("끌어서 옮기기");
    return {};
}
