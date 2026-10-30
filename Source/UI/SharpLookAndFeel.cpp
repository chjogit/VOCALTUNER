#include "SharpLookAndFeel.h"
#include <algorithm>
#include "PianoRollStyle.h"

SharpLookAndFeel::SharpLookAndFeel()
{
    refreshColours();
}

void SharpLookAndFeel::refreshColours()
{
    using namespace rollstyle;
    setColour(juce::TextButton::buttonColourId, panel);
    setColour(juce::TextButton::buttonOnColourId, ink);
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::TextButton::textColourOnId, chrome);
    setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2e2d2a));
    setColour(juce::Slider::trackColourId, inkDim);
    setColour(juce::Slider::thumbColourId, ink);
    setColour(juce::Slider::textBoxTextColourId, ink);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::PopupMenu::backgroundColourId, chrome);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ink);
    setColour(juce::PopupMenu::highlightedTextColourId, chrome);
    setColour(juce::ComboBox::backgroundColourId, panel);
    setColour(juce::ComboBox::outlineColourId, chromeEdge);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ToggleButton::textColourId, ink);
    setColour(juce::ToggleButton::tickColourId, ink);
    setColour(juce::TooltipWindow::backgroundColourId, ink);
    setColour(juce::TooltipWindow::textColourId, chrome);
}

void SharpLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                            bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds();
    auto fill = background;
    if (down)
        fill = fill.contrasting(0.15f);
    else if (highlighted)
        fill = fill.contrasting(0.06f);
    g.setColour(fill.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.fillRect(bounds);
    if (! static_cast<bool>(button.getProperties()["flat"]))
    {
        g.setColour(button.getToggleState() ? rollstyle::ink : juce::Colour(0xff3a3833));
        g.drawRect(bounds);
    }
}

void SharpLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                        float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
        return LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
    const auto centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    g.setColour(slider.findColour(juce::Slider::backgroundColourId));
    g.fillRect(static_cast<float>(x), centreY - 1.0f, static_cast<float>(width), 2.0f);
    g.setColour(slider.findColour(juce::Slider::trackColourId));
    g.fillRect(static_cast<float>(x), centreY - 1.0f, sliderPos - static_cast<float>(x), 2.0f);
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.fillRect(sliderPos - 1.0f, centreY - 5.0f, 2.0f, 10.0f);
}

juce::Font SharpLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::Font(juce::FontOptions(rollstyle::fontSize(std::min(13.0f, static_cast<float>(buttonHeight) * 0.55f))));
}

void SharpLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll(findColour(juce::TooltipWindow::backgroundColourId));
    g.setColour(findColour(juce::TooltipWindow::textColourId));
    g.setFont(juce::Font(juce::FontOptions(rollstyle::fontSize(12.0f), juce::Font::bold)));
    g.drawFittedText(text, juce::Rectangle<int>(width, height).reduced(7, 3), juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> SharpLookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(juce::Font(juce::FontOptions(rollstyle::fontSize(12.0f), juce::Font::bold)), text, 0.0f, 0.0f);
    const auto w = juce::jmin(360, static_cast<int>(glyphs.getBoundingBox(0, -1, true).getWidth()) + 16);
    const auto h = static_cast<int>(rollstyle::fontSize(12.0f)) + 10;
    return juce::Rectangle<int>(screenPos.x - w / 2, screenPos.y + 18, w, h).constrainedWithin(parentArea);
}
