#include "TrackPanel.h"
#include <algorithm>
#include "PianoRollStyle.h"

namespace
{
juce::String u8(const char* text) { return juce::String::fromUTF8(text); }

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
        addAndMakeVisible(button);
    };

    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

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

    const char* const names[] = { "박자", "음정", "이음" };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& s = sliders[i];
        s.label = u8(names[i]);
        s.slider.setSliderStyle(juce::Slider::LinearHorizontal);
        s.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 18);
        s.slider.setRange(0.0, 100.0, 1.0);
        s.slider.setTextBoxIsEditable(false);   // 글자 입력으로 키보드 포커스를 잡지 않음
        s.slider.setWantsKeyboardFocus(false);
        s.slider.setMouseClickGrabsKeyboardFocus(false);
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

void TrackPanel::setCollapsed(bool shouldCollapse)
{
    if (collapsed == shouldCollapse)
        return;
    collapsed = shouldCollapse;
    layoutDetails();
    repaint();
}

void TrackPanel::setSettingsOpen(bool open)
{
    settingsOpen = open;
    repaint(settingsRowBounds());
}

void TrackPanel::setRows(std::vector<Row> newRows)
{
    const auto same = newRows.size() == rows.size()
                   && std::equal(newRows.begin(), newRows.end(), rows.begin(), [](const Row& a, const Row& b)
                      { return a.id == b.id && a.name == b.name && a.detail == b.detail && a.view == b.view
                               && a.referenceSlot == b.referenceSlot && a.isActive == b.isActive && a.baseCorrection == b.baseCorrection; });
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
    updateFitButtons();
    layoutDetails();
    repaint(statusArea());
}

void TrackPanel::setFitBlocked(bool blocked)
{
    if (blocked == fitBlocked)
        return;
    fitBlocked = blocked;
    updateFitButtons();
}

void TrackPanel::setAnalysisProgress(double fraction)
{
    if (fraction < 0.0 && analysisProgress < 0.0)
        return;
    analysisProgress = fraction;
    repaint(statusArea());      // 진행 중에는 매번 다시 그려 움직이는 표시를 갱신
}

void TrackPanel::updateFitButtons()
{
    const auto enabled = fitProgress < 0.0 && ! fitBlocked;
    const auto tip = fitBlocked ? u8("발음·호흡 분석이 끝나면 맞출 수 있습니다") : juce::String();
    for (auto* b : { &fitButton, &fitAllButton })
    {
        b->setEnabled(enabled);
        b->setTooltip(tip);
    }
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
    return { 0, headerHeight + static_cast<int>(index) * rowHeight, getWidth() - collapsedWidth, rowHeight };
}
int TrackPanel::rowsBottom() const noexcept { return headerHeight + static_cast<int>(rows.size()) * rowHeight; }
juce::Rectangle<int> TrackPanel::referenceBox(size_t index) const
{
    const auto r = rowBounds(index);
    return { r.getRight() - 46, r.getCentreY() - 10, 38, 20 };
}
juce::Rectangle<int> TrackPanel::correctionBox(size_t index) const
{
    return referenceBox(index).translated(alignmentActive ? -41 : 0, 0);
}
juce::Rectangle<int> TrackPanel::detailText(size_t index) const
{
    const auto r = rowBounds(index);
    return { 14, r.getCentreY() + 1, correctionBox(index).getX() - 18, 15 };
}
juce::Rectangle<int> TrackPanel::collapseTab() const { return { getWidth() - collapsedWidth, 58, collapsedWidth, 42 }; }
juce::Rectangle<int> TrackPanel::guideRowBounds() const
{
    return { 0, getHeight() - footerRowHeight, getWidth() - collapsedWidth, footerRowHeight };
}
juce::Rectangle<int> TrackPanel::settingsRowBounds() const { return guideRowBounds().translated(0, -footerRowHeight); }
juce::Rectangle<int> TrackPanel::statusArea() const
{
    return settingsRowBounds().withHeight(22).translated(0, -30).reduced(16, 0);
}

void TrackPanel::layoutDetails()
{
    const auto visible = ! collapsed && alignmentActive && rolesEditable;
    for (juce::Component* c : std::initializer_list<juce::Component*>{ &timingToggle, &pitchToggle, &relationButton, &fitButton, &fitAllButton,
                                &cancelButton, &sliders[0].slider, &sliders[1].slider, &sliders[2].slider })
        c->setVisible(false);
    detailsArea = {};
    if (collapsed)
        return repaint();

    const auto running = fitProgress >= 0.0;
    cancelButton.setVisible(running);
    cancelButton.setBounds(statusArea().removeFromRight(40));
    fitAllButton.setVisible(alignmentActive);
    fitAllButton.setBounds(statusArea().translated(0, -34).withHeight(28));

    if (! visible)
        return repaint();

    // 역할: 위쪽 틈을 넉넉히 (트랙 목록과 성격이 다름), 슬라이더 사이는 촘촘히
    detailsArea = juce::Rectangle<int>(16, rowsBottom() + 20, getWidth() - collapsedWidth - 30, 170);
    auto r = detailsArea.withTrimmedTop(22);
    auto buttons = r.removeFromTop(24);
    timingToggle.setBounds(buttons.removeFromLeft(44));
    buttons.removeFromLeft(3);
    pitchToggle.setBounds(buttons.removeFromLeft(44));
    buttons.removeFromLeft(3);
    relationButton.setBounds(buttons);
    r.removeFromTop(8);

    const bool show[] = { roles.timing, roles.pitch, roles.pitch };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        if (! show[i])
            continue;
        auto line = r.removeFromTop(24);
        line.removeFromLeft(40);
        sliders[i].slider.setBounds(line);
        sliders[i].slider.setVisible(true);
    }
    timingToggle.setVisible(true);
    pitchToggle.setVisible(true);
    relationButton.setVisible(roles.pitch);
    fitButton.setBounds(r.removeFromTop(40).withTrimmedTop(10));
    fitButton.setVisible(roles.timing || roles.pitch);
    repaint();      // 슬라이더 이름표는 paint에서 슬라이더 위치에 맞춰 그리므로 배치가 바뀌면 다시 그린다
}

void TrackPanel::resized()
{
    layoutDetails();
}

// ------------------------------------------------------------------ 클릭 / 안내

void TrackPanel::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (collapseTab().contains(p) || collapsed)
    {
        if (onCollapseToggled)
            onCollapseToggled();
        return;
    }
    if (settingsRowBounds().contains(p))
    {
        if (onSettingsClicked)
            onSettingsClicked();
        return;
    }
    if (guideRowBounds().contains(p))
    {
        if (onAlignmentToggled)
            onAlignmentToggled(! alignmentActive);
        return;
    }
    for (size_t i = 0; i < rows.size(); ++i)
        if (rowBounds(i).contains(p))
            return handleRowClick(i, p);
}

void TrackPanel::handleRowClick(size_t i, juce::Point<int> p)
{
    const auto& row = rows[i];
    if (correctionBox(i).contains(p))
    {
        if (onBaseCorrectionToggled)
            onBaseCorrectionToggled(row.id, ! row.baseCorrection);
    }
    else if (alignmentActive && referenceBox(i).contains(p))
    {
        if (onReferenceChosen)
            onReferenceChosen(row.id, (row.referenceSlot + 1) % 3);
    }
    else if (detailText(i).contains(p))
    {
        if (onViewChanged)
            onViewChanged(row.id, nextView(row.view));
    }
    else if (onActivate)
        onActivate(row.id);
}

juce::String TrackPanel::getTooltip()
{
    const auto p = getMouseXYRelative();
    if (collapseTab().contains(p) || collapsed)
        return u8(collapsed ? "패널 펼치기" : "패널 접기");
    if (guideRowBounds().contains(p))
        return u8("가이드 모드: 기준 트랙의 박자와 음정에 다른 트랙을 맞춥니다. 켜면 발음·호흡 분석을 시작합니다");
    if (settingsRowBounds().contains(p))
        return u8("설정: 블록·곡선·배경 색과 글씨 크기");
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (correctionBox(i).contains(p))
            return u8("기초 보정: 블록 음정을 가까운 음 쪽으로 5% 옮깁니다");
        if (alignmentActive && referenceBox(i).contains(p))
            return u8("기준: 누를 때마다 꺼짐 → 네이비 → 베이지. 다른 트랙은 겹치는 기준에 맞춥니다");
        if (detailText(i).contains(p))
            return u8("눌러서 편집 / 보기 / 숨김");
    }
    return {};
}
