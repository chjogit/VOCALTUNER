#include "Theme.h"
#include "PianoRollStyle.h"

namespace theme
{
namespace
{
int clampIndex(int value, size_t count) { return juce::jlimit(0, static_cast<int>(count) - 1, value); }

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "VocalTuner";
    options.filenameSuffix = ".settings";
    options.folderName = "VocalTuner";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

// 배경이 밝으면 격자/건반 줄도 밝은 쪽으로 맞춘다
void applyBackground(juce::Colour bg)
{
    const auto light = bg.getPerceivedBrightness() > 0.5f;
    rollstyle::background = bg;
    rollstyle::whiteKeyRow = light ? bg.darker(0.04f) : bg.brighter(0.06f);
    rollstyle::blackKeyRow = light ? bg.darker(0.10f) : bg.brighter(0.02f);
    rollstyle::gridLine = light ? bg.darker(0.12f) : bg.brighter(0.12f);
    rollstyle::octaveLine = light ? bg.darker(0.30f) : bg.brighter(0.45f);
}
} // namespace

ThemeChoice clamp(ThemeChoice c) noexcept
{
    c.block = clampIndex(c.block, blockColours.size());
    c.curve = clampIndex(c.curve, curveColours.size());
    c.background = clampIndex(c.background, backgroundColours.size());
    c.font = clampIndex(c.font, fontScales.size());
    return c;
}

void apply(const ThemeChoice& choice)
{
    const auto c = clamp(choice);
    rollstyle::voicedFill = juce::Colour(blockColours[static_cast<size_t>(c.block)]);
    rollstyle::curve = juce::Colour(curveColours[static_cast<size_t>(c.curve)]);
    applyBackground(juce::Colour(backgroundColours[static_cast<size_t>(c.background)]));
    rollstyle::fontScale = fontScales[static_cast<size_t>(c.font)];
}

ThemeChoice load()
{
    juce::PropertiesFile file(settingsOptions());
    ThemeChoice c;
    c.block = file.getIntValue("themeBlock", c.block);
    c.curve = file.getIntValue("themeCurve", c.curve);
    c.background = file.getIntValue("themeBackground", c.background);
    c.font = file.getIntValue("themeFont", c.font);
    return clamp(c);
}

void save(const ThemeChoice& c)
{
    juce::PropertiesFile file(settingsOptions());
    file.setValue("themeBlock", c.block);
    file.setValue("themeCurve", c.curve);
    file.setValue("themeBackground", c.background);
    file.setValue("themeFont", c.font);
    file.saveIfNeeded();
}
} // namespace theme
