#include "TrackPanel.h"
#include "Logo.h"
#include "ToolIcons.h"
#include "PianoRollStyle.h"

using namespace rollstyle;

namespace
{
juce::String u8(const char* text) { return juce::String::fromUTF8(text); }
juce::Font bold(float size) { return juce::Font(juce::FontOptions(fontSize(size), juce::Font::bold)); }
juce::Font plain(float size) { return juce::Font(juce::FontOptions(fontSize(size))); }
} // namespace

void TrackPanel::paint(juce::Graphics& g)
{
    g.fillAll(panel);
    g.setColour(juce::Colours::black);
    g.drawVerticalLine(getWidth() - collapsedWidth, 0.0f, static_cast<float>(getHeight()));
    paintCollapseTab(g);
    if (collapsed)
        return;

    paintHeader(g);
    for (size_t i = 0; i < rows.size(); ++i)
        paintRow(g, i);
    paintRoleLabels(g);
    paintStatus(g);
    paintFooter(g);
}

void TrackPanel::paintCollapseTab(juce::Graphics& g)
{
    const auto tab = collapseTab();
    g.setColour(juce::Colour(0xff151514));
    g.fillRect(tab);
    g.setColour(juce::Colours::black);
    g.drawRect(tab);
    g.setColour(inkDim);
    g.setFont(plain(11.0f));
    g.drawText(collapsed ? juce::String::charToString(0x203A) : juce::String::charToString(0x2039), tab, juce::Justification::centred, false);
}

// 로고 + 이름: 크고 굵게, 부제는 작고 흐리게 (일부러 대비를 크게)
void TrackPanel::paintHeader(juce::Graphics& g)
{
    drawLogo(g, { 16.0f, 18.0f, 30.0f, 30.0f });
    g.setColour(ink);
    g.setFont(bold(19.0f).withExtraKerningFactor(0.08f));
    g.drawText("VOCALTUNER", 57, 17, 170, 22, juce::Justification::centredLeft, false);
    g.setColour(inkFaint);
    g.setFont(plain(9.5f).withExtraKerningFactor(0.14f));
    g.drawText("PITCH & TIMING", 58, 38, 170, 14, juce::Justification::centredLeft, false);
}

void TrackPanel::paintChip(juce::Graphics& g, juce::Rectangle<int> box, const char* text, juce::Colour fill, juce::Colour textColour,
                           bool outlined)
{
    if (! fill.isTransparent())
    {
        g.setColour(fill);
        g.fillRect(box);
    }
    if (outlined)
    {
        g.setColour(juce::Colour(0xff4a4740));
        g.drawRect(box);
    }
    g.setColour(textColour);
    g.setFont(plain(10.5f));
    g.drawText(u8(text), box, juce::Justification::centred, false);
}

void TrackPanel::paintRow(juce::Graphics& g, size_t i)
{
    const auto& row = rows[i];
    const auto bounds = rowBounds(i);
    if (row.isActive)
    {
        g.setColour(juce::Colour(0xff171715));
        g.fillRect(bounds);
    }
    g.setColour(juce::Colour(0xff1f1f1d));
    g.drawHorizontalLine(bounds.getY(), 0.0f, static_cast<float>(bounds.getRight()));

    // 왼쪽 띠: 편집 중 = 글자색, 기준 = 기준 색
    const auto bar = row.referenceSlot > 0 ? referenceColour(row.referenceSlot) : (row.isActive ? ink : juce::Colour(0xff2b2a27));
    g.setColour(bar);
    g.fillRect(bounds.withWidth(3));

    const auto alpha = row.view == TrackView::Hidden ? 0.35f : (row.view == TrackView::Visible ? 0.65f : 1.0f);
    const auto textRight = correctionBox(i).getX() - 6;
    g.setColour(ink.withAlpha(alpha));
    g.setFont(plain(12.5f));
    g.drawText(row.name, 14, bounds.getCentreY() - 15, textRight - 14, 16, juce::Justification::centredLeft, true);
    g.setColour(inkFaint);
    g.setFont(plain(10.0f));
    g.drawText(row.detail, detailText(i), juce::Justification::centredLeft, true);

    paintChip(g, correctionBox(i), "보정", juce::Colours::transparentBlack, row.baseCorrection ? ink : inkFaint, row.baseCorrection);
    if (alignmentActive)
    {
        const auto slot = row.referenceSlot;
        const auto fill = slot > 0 ? referenceColour(slot) : juce::Colours::transparentBlack;
        const auto text = slot == 2 ? juce::Colour(0xff1a160e) : (slot == 1 ? juce::Colours::white : inkDim);
        paintChip(g, referenceBox(i), "기준", fill, text, slot == 0);
    }
}

void TrackPanel::paintRoleLabels(juce::Graphics& g)
{
    if (alignmentActive && rolesEditable)
    {
        g.setColour(inkDim);
        g.setFont(plain(11.5f));
        g.drawText(referenceName + u8("에 맞춤"), detailsArea.withHeight(18), juce::Justification::centredLeft, true);
        g.setFont(plain(11.0f));
        for (const auto& s : sliders)
            if (s.slider.isVisible())
                g.drawText(s.label, s.slider.getBounds().withX(detailsArea.getX()).withWidth(40), juce::Justification::centredLeft, false);
        return;
    }
    if (! alignmentActive)
        return;
    g.setColour(inkFaint);
    g.setFont(plain(11.5f));
    g.drawFittedText(u8(rows.empty() ? "트랙이 없습니다" : "‘기준’을 눌러 기준 트랙을 정하고, 다른 트랙을 눌러 역할(박자/음정)을 정하세요"),
                     juce::Rectangle<int>(16, rowsBottom() + 18, getWidth() - collapsedWidth - 30, 60), juce::Justification::topLeft, 4);
}

// 설정 / 가이드 모드: 같은 높이의 굵은 줄 두 개를 맨 아래에
void TrackPanel::paintFooter(juce::Graphics& g)
{
    const auto settings = settingsRowBounds();
    g.setColour(settingsOpen ? juce::Colour(0xff222220) : juce::Colour(0xff1a1a18));
    g.fillRect(settings);
    g.setColour(juce::Colour(0xff1f1f1d));
    g.drawHorizontalLine(settings.getY(), 0.0f, static_cast<float>(settings.getRight()));
    drawGearIcon(g, juce::Rectangle<float>(16.0f, static_cast<float>(settings.getCentreY()) - 9.0f, 18.0f, 18.0f));
    g.setColour(ink);
    g.setFont(bold(14.0f));
    g.drawText(u8("설정"), settings.withTrimmedLeft(44), juce::Justification::centredLeft, false);
    g.setColour(inkFaint);
    g.setFont(plain(10.0f));
    g.drawText(u8("테마"), settings.withTrimmedRight(16), juce::Justification::centredRight, false);

    const auto guide = guideRowBounds();
    g.setColour(juce::Colour(0xff141412));
    g.fillRect(guide);
    g.setColour(juce::Colours::black);
    g.drawHorizontalLine(guide.getY(), 0.0f, static_cast<float>(guide.getRight()));
    const juce::Rectangle<float> toggle(16.0f, static_cast<float>(guide.getCentreY()) - 7.5f, 30.0f, 15.0f);
    g.setColour(alignmentActive ? accent : juce::Colour(0xff3a3833));
    g.fillRect(toggle);
    g.setColour(juce::Colour(0xff141412));
    g.fillRect(alignmentActive ? toggle.withTrimmedLeft(17.0f).reduced(2.0f) : toggle.withTrimmedRight(17.0f).reduced(2.0f));
    g.setColour(ink.withAlpha(alignmentActive ? 1.0f : 0.7f));
    g.setFont(bold(14.0f));
    g.drawText(u8("가이드 모드"), guide.withTrimmedLeft(56), juce::Justification::centredLeft, false);
    g.setColour(alignmentActive ? accent : inkFaint);
    g.setFont(plain(10.0f));
    g.drawText(u8(alignmentActive ? "켜짐" : "꺼짐"), guide.withTrimmedRight(16), juce::Justification::centredRight, false);
}

// 상태줄: 맞추기 중이거나 분석 중이면 위에 단계·퍼센트, 아래에 가는 막대(+ 오른쪽 취소 버튼). 아니면 상태 글자
void TrackPanel::paintStatus(juce::Graphics& g)
{
    const auto area = statusArea();
    g.setFont(plain(11.0f));
    if (fitProgress >= 0.0)
        return paintBar(g, area.withTrimmedRight(46).toFloat(), fitProgress, fitStage + "...", false);
    if (! alignmentActive)
        return;
    if (analysisProgress >= 0.0)
        return paintBar(g, area.toFloat(), analysisProgress, status, true);
    g.setColour(ink.withAlpha(0.8f));
    g.drawText(status, area, juce::Justification::centredLeft, true);
}

// moving: 한 트랙 분석이 끝날 때까지 수치가 안 바뀌므로, 멈춘 것처럼 보이지 않게 밝은 조각을 흘려 보낸다
void TrackPanel::paintBar(juce::Graphics& g, juce::Rectangle<float> area, double fraction, const juce::String& label, bool moving)
{
    auto text = area.removeFromTop(area.getHeight() - 6.0f).toNearestInt();
    const auto bar = area.withTrimmedTop(2.0f);
    g.setColour(inkDim);
    g.drawText(label, text, juce::Justification::centredLeft, true);
    if (! moving)
    {
        g.setColour(ink);
        g.drawText(juce::String(juce::roundToInt(fraction * 100.0)) + "%", text, juce::Justification::centredRight, false);
    }
    g.setColour(juce::Colour(0xff2e2d2a));
    g.fillRect(bar);
    g.setColour(ink);
    g.fillRect(bar.withWidth(juce::jmax(3.0f, bar.getWidth() * static_cast<float>(fraction))));
    if (moving)
    {
        const auto phase = static_cast<float>(juce::Time::getMillisecondCounter() % 1400) / 1400.0f;
        const auto w = bar.getWidth() * 0.2f;
        g.setColour(ink.withAlpha(0.45f));
        g.fillRect(juce::Rectangle<float>(bar.getX() + phase * (bar.getWidth() + w) - w, bar.getY(), w, bar.getHeight()).getIntersection(bar));
    }
}
