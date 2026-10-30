#include "SettingsPanel.h"
#include "PianoRollStyle.h"

namespace
{
constexpr int padLeft = 16, padRight = 14, titleHeight = 40, rowHeight = 24;
constexpr int swatchSize = 15, fontBoxWidth = 36;
const char* const fontNames[] = { "작게", "보통", "크게" };
} // namespace

SettingsPanel::SettingsPanel()
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void SettingsPanel::setChoice(const ThemeChoice& newChoice)
{
    choice = theme::clamp(newChoice);
    repaint();
}

int SettingsPanel::optionCount(Row row) noexcept
{
    switch (row)
    {
        case Row::Block:      return static_cast<int>(theme::blockColours.size());
        case Row::Curve:      return static_cast<int>(theme::curveColours.size());
        case Row::Background: return static_cast<int>(theme::backgroundColours.size());
        case Row::Font:       return static_cast<int>(theme::fontScales.size());
    }
    return 0;
}

int& SettingsPanel::selected(Row row) noexcept
{
    switch (row)
    {
        case Row::Block:      return choice.block;
        case Row::Curve:      return choice.curve;
        case Row::Background: return choice.background;
        case Row::Font:       break;
    }
    return choice.font;
}

juce::Colour SettingsPanel::swatch(Row row, int index) const
{
    const auto i = static_cast<size_t>(index);
    if (row == Row::Block)
        return juce::Colour(theme::blockColours[i]);
    if (row == Row::Curve)
        return juce::Colour(theme::curveColours[i]);
    return juce::Colour(theme::backgroundColours[i]);
}

// 글씨 크기 줄은 위와 틈을 더 둔다 (색과 성격이 다른 항목)
juce::Rectangle<int> SettingsPanel::rowArea(Row row) const
{
    const auto index = static_cast<int>(row);
    const auto y = titleHeight + index * rowHeight + (row == Row::Font ? 12 : 0);
    return { padLeft, y, getWidth() - padLeft - padRight, rowHeight };
}

juce::Rectangle<int> SettingsPanel::optionBox(Row row, int index) const
{
    const auto area = rowArea(row);
    const auto count = optionCount(row);
    if (row == Row::Font)
        return { area.getRight() - (count - index) * fontBoxWidth, area.getCentreY() - 9, fontBoxWidth, 18 };
    const auto step = swatchSize + 3;
    return { area.getRight() - (count - index) * step + 3, area.getCentreY() - swatchSize / 2, swatchSize, swatchSize };
}

void SettingsPanel::paintRow(juce::Graphics& g, Row row, const char* name)
{
    g.setColour(rollstyle::inkDim);
    g.setFont(rollstyle::fontSize(12.0f));
    g.drawText(juce::String::fromUTF8(name), rowArea(row), juce::Justification::centredLeft, false);

    for (int i = 0; i < optionCount(row); ++i)
    {
        const auto box = optionBox(row, i);
        const auto on = selected(row) == i;
        if (row == Row::Font)
        {
            g.setColour(on ? rollstyle::ink : rollstyle::chromeEdge);
            on ? g.fillRect(box) : g.drawRect(box);
            g.setColour(on ? rollstyle::chrome : rollstyle::inkDim);
            g.setFont(rollstyle::fontSize(11.0f));
            g.drawText(juce::String::fromUTF8(fontNames[i]), box, juce::Justification::centred, false);
            continue;
        }
        g.setColour(swatch(row, i));
        g.fillRect(box);
        g.setColour(juce::Colours::black);
        g.drawRect(box);
        if (on)
        {
            g.setColour(rollstyle::ink);
            g.drawRect(box.expanded(2), 1);
        }
    }
}

void SettingsPanel::paint(juce::Graphics& g)
{
    // 날카로운 모서리 + 오른쪽 아래로 3px 단단한 그림자
    const auto body = getLocalBounds().withTrimmedRight(3).withTrimmedBottom(3);
    g.setColour(juce::Colours::black);
    g.fillRect(body.translated(3, 3));
    g.setColour(rollstyle::chrome);
    g.fillRect(body);
    g.setColour(rollstyle::chromeEdge);
    g.drawRect(body);

    g.setColour(rollstyle::ink);
    g.setFont(juce::Font(juce::FontOptions(rollstyle::fontSize(17.0f), juce::Font::bold)));
    g.drawText(juce::String::fromUTF8("설정"), padLeft, 12, 60, 22, juce::Justification::centredLeft, false);
    g.setColour(rollstyle::inkFaint);
    g.setFont(rollstyle::fontSize(10.5f));
    g.drawText(juce::String::fromUTF8("테마"), padLeft + 44, 16, 60, 18, juce::Justification::centredLeft, false);

    paintRow(g, Row::Block, "블록");
    paintRow(g, Row::Curve, "곡선");
    paintRow(g, Row::Background, "배경");
    paintRow(g, Row::Font, "글씨 크기");
}

void SettingsPanel::mouseDown(const juce::MouseEvent& e)
{
    for (const auto row : { Row::Block, Row::Curve, Row::Background, Row::Font })
        for (int i = 0; i < optionCount(row); ++i)
            if (optionBox(row, i).expanded(2).contains(e.getPosition()))
            {
                selected(row) = i;
                repaint();
                if (onChanged)
                    onChanged(choice);
                return;
            }
}
