#include "TrackPanel.h"
#include <algorithm>
#include "PianoRollStyle.h"

namespace
{
juce::String u8(const char* text) { return juce::String::fromUTF8(text); }

const char* viewName(TrackView view)
{
    switch (view)
    {
        case TrackView::Editable: return "편집";
        case TrackView::Visible:  return "보기";
        case TrackView::Hidden:   return "숨김";
    }
    return "";
}

TrackView nextView(TrackView view)
{
    switch (view)
    {
        case TrackView::Editable: return TrackView::Visible;
        case TrackView::Visible:  return TrackView::Hidden;
        case TrackView::Hidden:   return TrackView::Editable;
    }
    return TrackView::Editable;
}
} // namespace

TrackPanel::TrackPanel()
{
    const auto style = [this](juce::TextButton& button, bool toggles)
    {
        button.setClickingTogglesState(toggles);
        button.setWantsKeyboardFocus(false);
        button.setMouseClickGrabsKeyboardFocus(false);
        button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2c2e35));
        button.setColour(juce::TextButton::buttonOnColourId, rollstyle::voicedFill);
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.8f));
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(button);
    };

    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    style(alignmentButton, true);
    alignmentButton.setButtonText(u8("레퍼런스 정렬 켜기"));
    alignmentButton.onClick = [this]
    {
        if (onAlignmentToggled)
            onAlignmentToggled(alignmentButton.getToggleState());
    };

    style(timingToggle, true);
    style(pitchToggle, true);
    style(relationButton, true);
    style(fitButton, false);
    style(fitAllButton, false);
    style(cancelButton, false);
    cancelButton.setButtonText(u8("취소"));
    cancelButton.setVisible(false);
    cancelButton.onClick = [this] { if (onFitCancelled) onFitCancelled(); };
    timingToggle.setButtonText(u8("박자"));
    pitchToggle.setButtonText(u8("음정"));
    fitButton.setButtonText(u8("이 트랙 맞추기"));
    fitAllButton.setButtonText(u8("모두 맞추기"));
    timingToggle.onClick = [this] { roles.timing = timingToggle.getToggleState(); notifyRoles(); layoutDetails(); };
    pitchToggle.onClick = [this] { roles.pitch = pitchToggle.getToggleState(); notifyRoles(); layoutDetails(); };
    relationButton.onClick = [this]
    {
        roles.relation = relationButton.getToggleState() ? PitchRelation::Harmony : PitchRelation::Unison;
        relationButton.setButtonText(u8(roles.relation == PitchRelation::Harmony ? "화음" : "동음(더블링)"));
        notifyRoles();
    };
    fitButton.onClick = [this] { if (onFitRequested) onFitRequested(); };
    fitAllButton.onClick = [this] { if (onFitAllRequested) onFitAllRequested(); };

    const char* const names[] = { "박자 정도", "음정 정도", "이음 정도" };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& s = sliders[i];
        s.label = u8(names[i]);
        s.slider.setSliderStyle(juce::Slider::LinearHorizontal);
        s.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
        s.slider.setRange(0.0, 100.0, 1.0);
        s.slider.setTextValueSuffix("%");
        s.slider.setTextBoxIsEditable(false);   // 글자 입력으로 키보드 포커스를 잡지 않음
        s.slider.setWantsKeyboardFocus(false);
        s.slider.setMouseClickGrabsKeyboardFocus(false);
        s.slider.getValueObject();
        s.slider.onDragStart = [this] { if (onRolesGestureBegin) onRolesGestureBegin(); };
        s.slider.onValueChange = [this]
        {
            if (syncing)
                return;
            roles.timingAmount = sliders[0].slider.getValue() / 100.0;
            roles.pitchAmount = sliders[1].slider.getValue() / 100.0;
            roles.smoothAmount = sliders[2].slider.getValue() / 100.0;
            notifyRoles();
        };
        addAndMakeVisible(s.slider);
    }
    syncRoleControls();
    layoutDetails();
}

void TrackPanel::setRows(std::vector<Row> newRows)
{
    const auto same = newRows.size() == rows.size()
                   && std::equal(newRows.begin(), newRows.end(), rows.begin(), [](const Row& a, const Row& b)
                      { return a.id == b.id && a.name == b.name && a.view == b.view && a.isReference == b.isReference && a.isActive == b.isActive
                               && a.baseCorrection == b.baseCorrection; });
    if (same)
        return;
    rows = std::move(newRows);
    layoutDetails();
    repaint();
}

void TrackPanel::setAlignmentStatus(bool active, const juce::String& newStatus)
{
    if (active == alignmentActive && newStatus == status)
        return;
    alignmentActive = active;
    status = newStatus;
    alignmentButton.setToggleState(active, juce::dontSendNotification);
    alignmentButton.setButtonText(u8(active ? "레퍼런스 정렬 끄기" : "레퍼런스 정렬 켜기"));
    layoutDetails();
    repaint();
}

void TrackPanel::setFitProgress(double progress, const juce::String& stage)
{
    const auto running = progress >= 0.0;
    if (running == (fitProgress >= 0.0) && std::abs(progress - fitProgress) < 0.005 && stage == fitStage)
        return;
    fitProgress = running ? juce::jlimit(0.0, 1.0, progress) : -1.0;
    fitStage = stage;
    cancelButton.setVisible(running);
    fitButton.setEnabled(! running);
    fitAllButton.setEnabled(! running);
    resized();
    repaint(statusArea());
}

void TrackPanel::setActiveRoles(const TrackRoles& newRoles, bool canEditRoles, const juce::String& newReferenceName)
{
    const auto changed = canEditRoles != rolesEditable || newReferenceName != referenceName
                      || newRoles.timing != roles.timing || newRoles.pitch != roles.pitch
                      || newRoles.relation != roles.relation
                      || ! juce::approximatelyEqual(newRoles.timingAmount, roles.timingAmount)
                      || ! juce::approximatelyEqual(newRoles.pitchAmount, roles.pitchAmount)
                      || ! juce::approximatelyEqual(newRoles.smoothAmount, roles.smoothAmount);
    if (! changed)
        return;
    rolesEditable = canEditRoles;
    referenceName = newReferenceName;
    roles = newRoles;
    syncRoleControls();
    layoutDetails();
    repaint();
}

void TrackPanel::syncRoleControls()
{
    const juce::ScopedValueSetter<bool> guard(syncing, true);
    timingToggle.setToggleState(roles.timing, juce::dontSendNotification);
    pitchToggle.setToggleState(roles.pitch, juce::dontSendNotification);
    relationButton.setToggleState(roles.relation == PitchRelation::Harmony, juce::dontSendNotification);
    relationButton.setButtonText(u8(roles.relation == PitchRelation::Harmony ? "화음" : "동음(더블링)"));
    sliders[0].slider.setValue(roles.timingAmount * 100.0, juce::dontSendNotification);
    sliders[1].slider.setValue(roles.pitchAmount * 100.0, juce::dontSendNotification);
    sliders[2].slider.setValue(roles.smoothAmount * 100.0, juce::dontSendNotification);
}

void TrackPanel::notifyRoles()
{
    if (! syncing && onRolesChanged)
        onRolesChanged(roles);
}

// ------------------------------------------------------------------ 배치

juce::Rectangle<int> TrackPanel::rowBounds(size_t index) const
{
    return { 0, headerHeight + static_cast<int>(index) * rowHeight, getWidth(), rowHeight };
}
juce::Rectangle<int> TrackPanel::viewBox(size_t index) const { return rowBounds(index).withWidth(44).reduced(4, 3); }
juce::Rectangle<int> TrackPanel::referenceBox(size_t index) const { return rowBounds(index).removeFromRight(44).reduced(4, 3); }
juce::Rectangle<int> TrackPanel::correctionBox(size_t index) const
{
    return rowBounds(index).removeFromRight(alignmentActive ? 88 : 44).removeFromLeft(44).reduced(4, 3);
}
juce::Rectangle<int> TrackPanel::statusArea() const { return { 8, headerHeight - 26, getWidth() - 16, 20 }; }

void TrackPanel::layoutDetails()
{
    const auto visible = alignmentActive && rolesEditable;
    for (juce::Component* c : std::initializer_list<juce::Component*>{ &timingToggle, &pitchToggle, &relationButton, &fitButton, &fitAllButton,
                                &sliders[0].slider, &sliders[1].slider, &sliders[2].slider })
        c->setVisible(false);
    detailsArea = {};

    auto area = getLocalBounds().withTrimmedTop(headerHeight + static_cast<int>(rows.size()) * rowHeight + 8).reduced(8, 0);
    fitAllButton.setVisible(alignmentActive);
    if (alignmentActive)
        fitAllButton.setBounds(getLocalBounds().withTop(getHeight() - 30).reduced(8, 4));

    if (! visible)
        return;

    detailsArea = area.withHeight(150);
    auto r = detailsArea.withTrimmedTop(22);
    auto buttons = r.removeFromTop(24);
    timingToggle.setBounds(buttons.removeFromLeft(48));
    buttons.removeFromLeft(4);
    pitchToggle.setBounds(buttons.removeFromLeft(48));
    buttons.removeFromLeft(4);
    relationButton.setBounds(buttons);
    r.removeFromTop(6);

    const bool show[] = { roles.timing, roles.pitch, roles.pitch };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        if (! show[i])
            continue;
        auto line = r.removeFromTop(24);
        line.removeFromLeft(58);
        sliders[i].slider.setBounds(line);
        sliders[i].slider.setVisible(true);
    }
    timingToggle.setVisible(true);
    pitchToggle.setVisible(true);
    relationButton.setVisible(roles.pitch);
    if (! roles.pitch)
        relationButton.setBounds({});
    fitButton.setBounds(r.removeFromTop(26).withTrimmedTop(2));
    fitButton.setVisible(roles.timing || roles.pitch);
    repaint();      // 슬라이더 이름표는 paint에서 슬라이더 위치에 맞춰 그리므로 배치가 바뀌면 다시 그린다
}

void TrackPanel::resized()
{
    alignmentButton.setBounds(getLocalBounds().removeFromTop(headerHeight).withTrimmedBottom(26).reduced(8, 6));
    cancelButton.setBounds(statusArea().removeFromRight(44));
    layoutDetails();
}

// ------------------------------------------------------------------ 그리기 / 클릭

void TrackPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff202228));
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));

    paintStatus(g);

    g.setFont(13.0f);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        const auto bounds = rowBounds(i);
        if (row.isActive)
        {
            g.setColour(juce::Colour(0xff2f3a52));
            g.fillRect(bounds);
        }

        const auto alpha = row.view == TrackView::Hidden ? 0.35f : (row.view == TrackView::Visible ? 0.6f : 1.0f);
        const auto box = viewBox(i).toFloat();
        g.setColour(row.view == TrackView::Editable ? rollstyle::voicedFill : juce::Colour(0xff3a3d47));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(11.0f);
        g.drawText(u8(viewName(row.view)), box.toNearestInt(), juce::Justification::centred, false);

        g.setFont(13.0f);
        g.setColour(juce::Colours::white.withAlpha(alpha));
        g.drawText(row.name, bounds.withTrimmedLeft(50).withTrimmedRight(alignmentActive ? 92 : 48), juce::Justification::centredLeft, true);

        const auto fix = correctionBox(i).toFloat();
        g.setColour(row.baseCorrection ? rollstyle::voicedFill.withAlpha(0.8f) : juce::Colour(0xff3a3d47));
        g.fillRoundedRectangle(fix, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(row.baseCorrection ? 0.95f : 0.5f));
        g.setFont(11.0f);
        g.drawText(u8("보정"), fix.toNearestInt(), juce::Justification::centred, false);

        if (alignmentActive)
        {
            const auto refBox = referenceBox(i).toFloat();
            g.setColour(row.isReference ? rollstyle::accent : juce::Colour(0xff3a3d47));
            g.fillRoundedRectangle(refBox, 3.0f);
            g.setColour(row.isReference ? juce::Colour(0xff10301c) : juce::Colours::white.withAlpha(0.7f));
            g.setFont(11.0f);
            g.drawText(u8("기준"), refBox.toNearestInt(), juce::Justification::centred, false);
        }
    }

    if (alignmentActive && rolesEditable)
    {
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.setFont(12.0f);
        g.drawText(u8("기준: ") + referenceName + u8("에 맞춤"), detailsArea.withHeight(20), juce::Justification::centredLeft, true);
        g.setFont(11.5f);
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        const juce::String names[] = { sliders[0].label, sliders[1].label, sliders[2].label };
        for (size_t i = 0; i < sliders.size(); ++i)
            if (sliders[i].slider.isVisible())
                g.drawText(names[i], sliders[i].slider.getBounds().withLeft(detailsArea.getX()).withWidth(58), juce::Justification::centredLeft, false);
    }
    else if (alignmentActive)
    {
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.setFont(12.0f);
        g.drawFittedText(u8(rows.empty() ? "트랙이 없습니다" : "‘기준’을 눌러 레퍼런스 트랙을 정하고, 다른 트랙을 눌러 역할(박자/음정)을 설정하세요"),
                         getLocalBounds().withTrimmedTop(headerHeight + static_cast<int>(rows.size()) * rowHeight + 8).reduced(10, 0).withHeight(60),
                         juce::Justification::topLeft, 4);
    }
}

void TrackPanel::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (! rowBounds(i).contains(p))
            continue;
        const auto& row = rows[i];
        if (viewBox(i).contains(p))
        {
            if (onViewChanged)
                onViewChanged(row.id, nextView(row.view));
        }
        else if (correctionBox(i).contains(p))
        {
            if (onBaseCorrectionToggled)
                onBaseCorrectionToggled(row.id, ! row.baseCorrection);
        }
        else if (alignmentActive && referenceBox(i).contains(p))
        {
            if (onReferenceChosen)
                onReferenceChosen(row.isReference ? nullptr : row.id);
        }
        else if (onActivate)
            onActivate(row.id);
        return;
    }
}

// 상태줄: 맞추기 중이면 로딩바(+ 취소 버튼), 아니면 상태 글자
void TrackPanel::paintStatus(juce::Graphics& g)
{
    const auto area = statusArea();
    g.setFont(11.5f);
    if (fitProgress < 0.0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.drawText(status.isEmpty() ? u8("꺼짐: 켜면 발음·호흡 분석을 시작합니다") : status, area, juce::Justification::centredLeft, true);
        return;
    }
    const auto bar = area.withTrimmedRight(48).toFloat();
    g.setColour(juce::Colour(0xff2c2e35));
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(rollstyle::voicedFill);
    g.fillRoundedRectangle(bar.withWidth(juce::jmax(6.0f, bar.getWidth() * static_cast<float>(fitProgress))), 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawText(fitStage + juce::String::formatted("  %d%%", juce::roundToInt(fitProgress * 100.0)), bar.toNearestInt().reduced(6, 0),
               juce::Justification::centredLeft, true);
}
