#include "PianoRollView.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include "BlockEditing.h"
#include "PianoRollStyle.h"

PianoRollView::PianoRollView()
{
    // 키보드 포커스를 잡으면 Space / +/- 가 DAW로 전달되지 않으므로 버튼도 포커스를 받지 않음
    const auto style = [this](juce::TextButton& button, bool toggles, int radioGroup)
    {
        button.setClickingTogglesState(toggles);
        button.setRadioGroupId(radioGroup);
        button.setWantsKeyboardFocus(false);
        button.setMouseClickGrabsKeyboardFocus(false);
        button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        button.getProperties().set("flat", true);
        if (auto* icon = dynamic_cast<IconTextButton*>(&button))
            icon->setIconOnly(true);
        toolbar.addAndMakeVisible(button);
    };

    style(modeButton, false, 0);
    modeButton.setBoldText(true);
    modeButton.setIconOnly(false);
    modeButton.onClick = [this] { showModeMenu(); };
    for (size_t i = 0; i < toolButtons.size(); ++i)
    {
        style(toolButtons[i], true, 2);
        toolButtons[i].onClick = [this, i] { onToolButton(i); };
    }

    style(undoButton, false, 0);
    style(redoButton, false, 0);
    undoButton.onClick = [this] { requestUndo(false); };
    redoButton.onClick = [this] { requestUndo(true); };
    undoButton.setIcon(ToolIcon::Undo);
    redoButton.setIcon(ToolIcon::Redo);
    undoButton.setEnabled(false);
    redoButton.setEnabled(false);
    undoButton.setTooltip(juce::String::fromUTF8("되돌리기  Ctrl+Z"));
    redoButton.setTooltip(juce::String::fromUTF8("다시  Ctrl+Shift+Z"));

    style(balanceButton, false, 0);
    balanceButton.setIcon(ToolIcon::Balance);
    balanceButton.onClick = [this] { toggleBalancePanel(); };
    balanceButton.setTooltip(juce::String::fromUTF8("음정 균형  Ctrl+4"));
    modeButton.setTooltip(juce::String::fromUTF8("모드  Ctrl+1 블록 / Ctrl+2 곡선 / Ctrl+3 박자"));
    toolbar.onMoved = [this](juce::Point<int> topLeft) { moveToolbar(topLeft); };
    toolbar.onDropped = [this] { dropToolbar(); };
    toolbar.onPinClicked = [this] { dockToolbar(! toolbar.isDocked()); };
    addAndMakeVisible(toolbar);
    balancePanel.onGestureBegin = [this] { if (onEditGestureBegin) onEditGestureBegin(); };
    balancePanel.onChanged = [this](const PitchBalance& b) { applyBalance(b); };
    balancePanel.onClosed = [this] { toggleBalancePanel(); };
    addChildComponent(balancePanel);

    setWantsKeyboardFocus(true);    // Ctrl+Z가 Studio One의 되돌리기로 넘어가지 않게 키를 먼저 받는다
    setMouseClickGrabsKeyboardFocus(true);
    syncToolbar();
}

// ------------------------------------------------------------------ 모드 / 도구

void PianoRollView::setMode(Mode newMode)
{
    mode = newMode;
    if (mode == Mode::Rhythm && needsPitchEditing(blobTool))
        blobTool = BlobTool::Timing;    // 박자 모드에서는 음정 이동 불가
    if (mode == Mode::Blob && blobTool == BlobTool::Timing)
        blobTool = BlobTool::Move;      // 블롭 모드의 타이밍은 이동 도구에 합쳐져 있다
    syncToolbar();
}

void PianoRollView::setBlobTool(BlobTool newTool)
{
    if (mode == Mode::Rhythm && needsPitchEditing(newTool))
        return;
    blobTool = newTool;
    if (isShapeTool(newTool))
        shapeTool = newTool;
    syncToolbar();
}

void PianoRollView::setCurveTool(CurveTool newTool)
{
    curveTool = newTool;
    syncToolbar();
}

// 같은 버튼을 모드에 따라 이름만 바꿔 쓴다
void PianoRollView::selectToolByNumber(size_t index)
{
    if (mode == Mode::Curve)
    {
        if (index < 2)
            setCurveTool(static_cast<CurveTool>(index));
    }
    else if (mode == Mode::Rhythm)
    {
        static constexpr BlobTool rhythmTools[] = { BlobTool::Timing, BlobTool::Cut, BlobTool::Volume };
        if (index < 3)
            setBlobTool(rhythmTools[index]);
    }
    else
    {
        static constexpr BlobTool blobTools[] = { BlobTool::Move, BlobTool::Cut, BlobTool::Volume };
        if (index < 3)
            setBlobTool(blobTools[index]);
        else if (index == 3)
            setBlobTool(shapeTool);
    }
}

// 곡선 도구 버튼을 이미 쓰는 중에 다시 누르면 음정 흐름 / 비브라토 / 펴기 중에서 고른다
void PianoRollView::onToolButton(size_t index)
{
    if (mode == Mode::Blob && index == 3 && isShapeTool(blobTool))
        showShapeMenu();
    else
        selectToolByNumber(index);
}

namespace
{
constexpr size_t modeCount = 3;
const char* const modeNames[modeCount] = { "블록", "곡선", "박자" };
}

void PianoRollView::showModeMenu()
{
    juce::PopupMenu menu;
    for (size_t i = 0; i < modeCount; ++i)
        menu.addItem(static_cast<int>(i) + 1, juce::String::fromUTF8(modeNames[i]) + "    Ctrl+" + juce::String(i + 1),
                     true, static_cast<size_t>(mode) == i);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&modeButton),
                       [safe = juce::Component::SafePointer<PianoRollView>(this)](int chosen)
                       {
                           if (safe != nullptr && chosen >= 1 && chosen <= static_cast<int>(modeCount))
                               safe->setMode(static_cast<Mode>(chosen - 1));
                       });
}

void PianoRollView::showShapeMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, juce::String::fromUTF8("음정 흐름 (Pitch Drift)"), true, shapeTool == BlobTool::Drift);
    menu.addItem(2, juce::String::fromUTF8("비브라토"), true, shapeTool == BlobTool::Vibrato);
    menu.addItem(3, juce::String::fromUTF8("펴기"), true, shapeTool == BlobTool::Flatten);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&toolButtons[3]),
                       [safe = juce::Component::SafePointer<PianoRollView>(this)](int chosen)
                       {
                           static constexpr BlobTool tools[] = { BlobTool::Drift, BlobTool::Vibrato, BlobTool::Flatten };
                           if (safe != nullptr && chosen >= 1 && chosen <= 3)
                               safe->setBlobTool(tools[chosen - 1]);
                       });
}

void PianoRollView::syncToolbar()
{
    modeButton.setButtonText(juce::String::fromUTF8(modeNames[static_cast<size_t>(mode)]) + juce::String::fromUTF8(" ▾"));

    const char* const shapeName = shapeTool == BlobTool::Vibrato ? "비브라토 ▾" : (shapeTool == BlobTool::Flatten ? "펴기 ▾" : "음정 흐름 ▾");
    const char* const blobNames[] = { "이동", "자르기", "볼륨", shapeName };
    static const char* const curveNames[] = { "상하", "좌우", "", "" };
    const auto shapeIcon = shapeTool == BlobTool::Vibrato ? ToolIcon::Vibrato : (shapeTool == BlobTool::Flatten ? ToolIcon::Flatten : ToolIcon::Drift);
    const ToolIcon blobIcons[] = { ToolIcon::Move, ToolIcon::Cut, ToolIcon::Volume, shapeIcon };
    static constexpr ToolIcon curveIcons[] = { ToolIcon::UpDown, ToolIcon::LeftRight, ToolIcon::None, ToolIcon::None };
    for (size_t i = 0; i < toolButtons.size(); ++i)
    {
        const auto visible = mode == Mode::Curve ? i < 2 : (mode == Mode::Rhythm ? i < 3 : true);
        toolButtons[i].setVisible(visible);
        toolButtons[i].setButtonText(juce::String::fromUTF8(mode == Mode::Curve ? curveNames[i] : blobNames[i]));
        toolButtons[i].setIcon(mode == Mode::Curve ? curveIcons[i] : blobIcons[i]);
        toolButtons[i].setTooltip(toolButtons[i].getButtonText() + "  " + juce::String(i + 1));
        bool active = false;
        if (mode == Mode::Curve)
            active = static_cast<size_t>(curveTool) == i;
        else if (i == 0)
            active = blobTool == (mode == Mode::Rhythm ? BlobTool::Timing : BlobTool::Move);
        else if (i == 3)
            active = isShapeTool(blobTool);
        else
            active = blobTool == (i == 1 ? BlobTool::Cut : BlobTool::Volume);
        toolButtons[i].setToggleState(active, juce::dontSendNotification);
    }
    resized();
    if (hover)
        updateCursor(*hover);
    repaint();
}

void PianoRollView::setUndoRedoEnabled(bool undoAvailable, bool redoAvailable)
{
    canUndo = undoAvailable;
    canRedo = redoAvailable;
    undoButton.setEnabled(canUndo && historyAllowed());
    redoButton.setEnabled(canRedo && historyAllowed());
}

void PianoRollView::requestUndo(bool redo)
{
    if (! historyAllowed() || drag.kind != DragKind::None)
        return;
    if (redo && canRedo && onRedo)
        onRedo();
    else if (! redo && canUndo && onUndo)
        onUndo();
}

// 블록 우클릭: 고른 블록(여러 개면 모두)만 따로 되돌린다
void PianoRollView::showBlockMenu(const juce::MouseEvent& e)
{
    if (content.blocks == nullptr || ! onRevertRequested)
        return;
    const auto pick = pickBlock(toPlot(e.position), true);
    if (! pick)
        return;
    if (! isSelected(pick->index))
        selectOnly(pick->index);

    juce::PopupMenu menu;
    menu.addItem(1, juce::String::fromUTF8("한 단계 되돌리기"));
    menu.addItem(2, juce::String::fromUTF8("처음으로 되돌리기"));
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
                       [safe = juce::Component::SafePointer<PianoRollView>(this)](int chosen)
                       {
                           if (safe != nullptr && chosen > 0 && safe->onRevertRequested)
                               safe->onRevertRequested(safe->selection, chosen == 2);
                       });
}

void PianoRollView::pollShortcutKeys()
{
    // 놓은 이벤트를 못 받는 경우(호스트가 재생을 시작하며 마우스를 가져감 등)에도 미리 듣기는 버튼을 놓으면 끝낸다
    if (previewing && ! juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
    {
        previewing = false;
        if (onPreviewStop)
            onPreviewStop();
    }
    const auto inside = isMouseOver(true);
    const auto plain = ! juce::ModifierKeys::getCurrentModifiersRealtime().isAnyModifierKeyDown();
    for (size_t i = 0; i < keyWasDown.size(); ++i)
    {
        const auto down = juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('1' + i))
                       || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::numberPad1 + static_cast<int>(i));
        if (inside && down && plain && ! keyWasDown[i])
            selectToolByNumber(i);
        keyWasDown[i] = down;
    }
}

namespace
{
int digitOf(const juce::KeyPress& key) noexcept
{
    const auto code = key.getKeyCode();
    if (code >= '1' && code <= '9')
        return code - '0';
    if (code >= juce::KeyPress::numberPad1 && code <= juce::KeyPress::numberPad9)
        return code - juce::KeyPress::numberPad1 + 1;
    return 0;
}
} // namespace

bool PianoRollView::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! mods.isCtrlDown() || mods.isAltDown())
        return false;
    const auto letter = juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
    if (letter == 'Z')
    {
        requestUndo(mods.isShiftDown());    // 다중 선택 중이라 막혀 있어도 호스트로는 넘기지 않는다
        return true;
    }
    if (mods.isShiftDown())
        return false;
    if (letter == 'A')
        return selectAll(), true;
    const auto digit = digitOf(key);
    if (digit >= 1 && digit <= static_cast<int>(modeCount))
        return setMode(static_cast<Mode>(digit - 1)), true;
    if (digit == static_cast<int>(modeCount) + 1)
        return toggleBalancePanel(), true;
    return false;
}

void PianoRollView::mouseEnter(const juce::MouseEvent&)
{
    if (isShowing() && ! hasKeyboardFocus(true))
        grabKeyboardFocus();
}

void PianoRollView::updateCursor(juce::Point<float> position)
{
    auto shape = juce::MouseCursor::NormalCursor;
    if (zoneAt(position) == Zone::Plot && content.blocks != nullptr)
    {
        const auto local = toPlot(position);
        if (mode == Mode::Curve)
        {
            if (pickCurveHandle(local))
                shape = curveTool == CurveTool::Vertical ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor;
        }
        else if (blobTool == BlobTool::Cut)
            shape = juce::MouseCursor::CrosshairCursor;
        else if (mode == Mode::Blob && blobTool == BlobTool::Move && pickTransition(local))
            shape = juce::MouseCursor::UpDownResizeCursor;
        else if (const auto pick = pickBlock(local, toolPicksConsonants()))
        {
            if (blobTool == BlobTool::Timing)
                shape = pick->edge != EdgeSide::None ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
            else if (blobTool == BlobTool::Move && isSelected(pick->index))
                shape = pick->edge != EdgeSide::None ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
            else
                shape = juce::MouseCursor::UpDownResizeCursor;
        }
    }
    setMouseCursor(shape);
}

PianoRollView::Zone PianoRollView::zoneAt(juce::Point<float> p) const
{
    const auto point = p.toInt();
    if (plotBounds().contains(point))
        return Zone::Plot;
    if (point.y < rulerHeight)
        return point.x >= gutterWidth ? Zone::Ruler : Zone::None;
    if (point.y >= rulerHeight + toolbarSpace() && point.x < gutterWidth)
        return Zone::Gutter;
    return Zone::None;
}

// ------------------------------------------------------------------ 선택

bool PianoRollView::isSelected(size_t index) const
{
    return std::binary_search(selection.begin(), selection.end(), index);
}

void PianoRollView::setSelection(std::vector<size_t> indices)
{
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    selection = std::move(indices);
    primaryBlock = selection.empty() ? std::nullopt : std::optional<size_t>(selection.back());
    repaint();
}

void PianoRollView::selectOnly(size_t index)
{
    setSelection({ index });
}

void PianoRollView::toggleSelection(size_t index)
{
    auto next = selection;
    const auto it = std::find(next.begin(), next.end(), index);
    if (it != next.end())
        next.erase(it);
    else
        next.push_back(index);
    setSelection(std::move(next));
    if (isSelected(index))
        primaryBlock = index;
}

void PianoRollView::selectAll()
{
    if (mode == Mode::Curve || content.blocks == nullptr)
        return;
    std::vector<size_t> all;
    for (size_t i = 0; i < content.blocks->size(); ++i)
        if (toolPicksConsonants() || ! (*content.blocks)[i].isConsonant)
            all.push_back(i);
    setSelection(std::move(all));
}

// ------------------------------------------------------------------ 내용 / 배치

void PianoRollView::setContent(Content newContent, bool refit)
{
    const auto sameShape = content.blocks != nullptr && newContent.blocks != nullptr
                        && content.blocks->size() == newContent.blocks->size();
    content = std::move(newContent);
    if (! sameShape)
    {
        selection.clear();
        primaryBlock.reset();
        hoverBlock.reset();
        hoverJoint.reset();
        marquee.reset();
    }
    rhythmRowMidi = content.blocks != nullptr ? averagePitchMidi(*content.blocks) : 60.0;
    if (refit)
        fitToContent();
    repaint();
}

void PianoRollView::setLayers(std::vector<Layer> newLayers)
{
    layers = std::move(newLayers);
    repaint();
}

void PianoRollView::setPlayhead(double songSec, bool isPlaying)
{
    if (juce::exactlyEqual(songSec, playheadSec) && isPlaying == playing)
        return;
    playheadSec = songSec;
    playing = isPlaying;
    repaint();
}

void PianoRollView::resized()
{
    std::vector<FloatingToolbar::Item> items { { &modeButton, 76, false } };
    for (size_t i = 0; i < toolButtons.size(); ++i)
        items.push_back({ &toolButtons[i], 34, i == 0 });
    items.push_back({ &undoButton, 34, true });
    items.push_back({ &redoButton, 34, false });
    items.push_back({ &balanceButton, 34, true });
    toolbar.setItems(std::move(items));
    placeToolbar();

    const auto plot = plotBounds();
    balancePanel.setBounds(plot.getRight() - PitchBalancePanel::preferredWidth - 8, plot.getY() + 8,
                           PitchBalancePanel::preferredWidth, PitchBalancePanel::preferredHeight);

    viewport.width = std::max(1, plotBounds().getWidth());
    viewport.height = std::max(1, plotBounds().getHeight());
}

// ------------------------------------------------------------------ 떠 있는 도구 막대

// 붙어 있으면 눈금자 아래 줄의 왼쪽, 떠 있으면 마지막 위치 (처음에는 피아노롤 가운데 위)
void PianoRollView::placeToolbar()
{
    const auto width = toolbar.preferredWidth();
    const auto height = FloatingToolbar::barHeight + FloatingToolbar::shadow;
    const auto plot = plotBounds();
    if (toolbar.isDocked())
        return toolbar.setBounds(gutterWidth + 6, rulerHeight + 2, width, height);

    const auto fallback = juce::Point<int>(plot.getCentreX() - width / 2, plot.getY() + 12);
    auto topLeft = toolbarPosition.value_or(fallback);
    topLeft.x = juce::jlimit(plot.getX(), std::max(plot.getX(), plot.getRight() - width), topLeft.x);
    topLeft.y = juce::jlimit(plot.getY(), std::max(plot.getY(), plot.getBottom() - height), topLeft.y);
    toolbar.setBounds(topLeft.x, topLeft.y, width, height);
}

void PianoRollView::moveToolbar(juce::Point<int> topLeft)
{
    toolbarPosition = topLeft;
    placeToolbar();
}

// 위쪽 가장자리 가까이 놓으면 붙는다
void PianoRollView::dropToolbar()
{
    if (toolbar.getY() <= plotBounds().getY() + 4)
        dockToolbar(true);
}

void PianoRollView::dockToolbar(bool dock)
{
    toolbar.setDocked(dock);
    if (! dock && toolbarPosition && toolbarPosition->y <= rulerHeight + toolbarHeight + 4)
        toolbarPosition.reset();    // 떼어 내면 처음 자리로
    resized();
    repaint();
}

double PianoRollView::blockCentreMidi(const VocalBlock& block) const
{
    return mode == Mode::Rhythm ? rhythmRowMidi : block.displayCenterMidi();
}

void PianoRollView::fitToContent()
{
    viewport.width = std::max(1, plotBounds().getWidth());
    viewport.height = std::max(1, plotBounds().getHeight());
    viewport.fitTime(content.regionSongStartSec, std::max(content.regionSongEndSec, content.regionSongStartSec + 1.0));

    double low = 60.0, high = 72.0;
    if (content.blocks != nullptr)
    {
        bool any = false;
        for (const auto& block : *content.blocks)
        {
            const auto song = sourceToSong(block.startSec);
            if (block.isConsonant || song + 1e-9 < content.regionSongStartSec || song > content.regionSongEndSec)
                continue;
            const auto pitch = block.baseMidi + block.pitchOffset;
            low = any ? std::min(low, pitch) : pitch;
            high = any ? std::max(high, pitch) : pitch;
            any = true;
        }
    }
    viewport.fitPitch(low, high);
}

// ------------------------------------------------------------------ 마우스

void PianoRollView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto delta = static_cast<double>(wheel.isReversed ? -wheel.deltaY : wheel.deltaY);
    const auto factor = std::exp(delta * 1.2);
    const auto zone = zoneAt(e.position);
    const auto anchorX = e.position.x - gutterWidth;
    const auto anchorY = e.position.y - rulerHeight - toolbarSpace();

    if (zone == Zone::Gutter)
        viewport.zoomPitchAt(anchorY, factor);
    else if (zone == Zone::Ruler)
    {
        if (e.mods.isShiftDown())
            viewport.panByPixels(delta * 400.0, 0.0);
        else
            viewport.zoomTimeAt(anchorX, factor);
    }
    else if (zone == Zone::Plot)
    {
        if (e.mods.isAltDown() && e.mods.isShiftDown())
        {
            viewport.zoomTimeAt(anchorX, factor);
            viewport.zoomPitchAt(anchorY, factor);
        }
        else if (e.mods.isShiftDown())
            viewport.panByPixels(delta * 400.0, 0.0);
        else if (e.mods.isCtrlDown())
            viewport.zoomPitchAt(anchorY, factor);
        else
            viewport.zoomTimeAt(anchorX, factor);
    }
    repaint();
}

void PianoRollView::mouseDown(const juce::MouseEvent& e)
{
    dragOrigin = viewport;
    drag = {};
    drag.zone = zoneAt(e.position);
    if (drag.zone != Zone::Plot)
        return;

    if (e.mods.isPopupMenu())
        showBlockMenu(e);
    else if (e.mods.isMiddleButtonDown() || e.mods.isAltDown())
        drag.kind = DragKind::Pan;
    else
        startPlotDrag(e);
}

void PianoRollView::mouseDrag(const juce::MouseEvent& e)
{
    if (drag.zone == Zone::Plot && drag.kind != DragKind::Pan && drag.kind != DragKind::None)
    {
        continueDrag(e);
        return;
    }
    if (drag.kind == DragKind::None && drag.zone == Zone::Plot)
        return;     // Ctrl+클릭으로 선택만 바꾼 경우

    const auto dx = static_cast<double>(e.getDistanceFromDragStartX());
    const auto dy = static_cast<double>(e.getDistanceFromDragStartY());
    viewport = dragOrigin;
    if (drag.zone == Zone::Plot)
        viewport.panByPixels(dx, dy);
    else if (drag.zone == Zone::Gutter)
        viewport.panByPixels(0.0, dy);
    else if (drag.zone == Zone::Ruler)
    {
        // 위로 끌면 확대, 아래로 끌면 축소 (처음 누른 위치 기준) + 좌우 이동
        viewport.zoomTimeAt(static_cast<double>(e.getMouseDownX() - gutterWidth), std::exp(-dy * 0.01));
        viewport.panByPixels(dx, 0.0);
    }
    repaint();
}

void PianoRollView::mouseUp(const juce::MouseEvent& e)
{
    if (std::exchange(previewing, false) && onPreviewStop)
        onPreviewStop();
    if (drag.zone == Zone::Ruler && ! e.mouseWasDraggedSinceMouseDown() && onSeekRequested)
        onSeekRequested(viewport.xToTime(e.position.x - gutterWidth));
    const auto wasMarquee = drag.kind == DragKind::Marquee;
    const auto shapeDrag = drag.kind == DragKind::BlobVibrato || drag.kind == DragKind::BlobFlatten || drag.kind == DragKind::BlobDrift;
    drag = {};
    if (shapeDrag && onShapeSettled)
        onShapeSettled();
    if (wasMarquee)
    {
        marquee.reset();
        repaint();
    }
    updateCursor(e.position);
}

void PianoRollView::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto zone = zoneAt(e.position);
    if (zone == Zone::Plot && content.blocks != nullptr && content.features != nullptr)
    {
        const auto local = toPlot(e.position);
        if (mode == Mode::Curve)
        {
            if (const auto point = pickCurvePoint(local))
            {
                toggleCurveHandleAt(*point);
                return;
            }
        }
        else if (blobTool == BlobTool::Cut)
        {
            if (onCutRequested)
                onCutRequested(viewport.xToTime(e.position.x - gutterWidth), jointTolerancePixels * viewport.secondsPerPixel);
            return;
        }
        else if (mode == Mode::Blob && blobTool == BlobTool::Move)
        {
            if (const auto pick = pickBlock(local, false))
            {
                alignSelectionToNoteCenter(*pick);
                return;
            }
        }
        else if (mode == Mode::Blob && blobTool == BlobTool::Vibrato)
        {
            if (const auto pick = pickBlock(local, false))
            {
                toggleVibrato(*pick);
                return;
            }
        }
        else if (mode == Mode::Blob && blobTool == BlobTool::Flatten)
        {
            if (const auto pick = pickBlock(local, false))
            {
                toggleFlatten(*pick);
                return;
            }
        }
        else if (mode == Mode::Blob && blobTool == BlobTool::Drift)
        {
            if (const auto pick = pickBlock(local, false))
            {
                toggleDrift(*pick);
                return;
            }
        }
    }
}

void PianoRollView::mouseMove(const juce::MouseEvent& e)
{
    hover = e.position;
    updateCursor(e.position);

    std::optional<size_t> hovered, joint;
    if (zoneAt(e.position) == Zone::Plot && content.blocks != nullptr)
    {
        const auto local = toPlot(e.position);
        if (const auto pick = pickBlock(local, true))
            hovered = pick->index;
        if (mode == Mode::Blob && blobTool == BlobTool::Move)
            joint = pickTransition(local);
    }
    const auto changed = hovered != hoverBlock || joint != hoverJoint;
    hoverBlock = hovered;
    hoverJoint = joint;

    if (changed || (mode != Mode::Curve && blobTool == BlobTool::Cut) || mode == Mode::Curve)
        repaint();
}

void PianoRollView::mouseExit(const juce::MouseEvent&)
{
    hover.reset();
    hoverBlock.reset();
    hoverJoint.reset();
    repaint();
}

// ------------------------------------------------------------------ 음정 균형

// 선택한 블롭, 선택이 없으면 모든 유성 블롭
std::vector<size_t> PianoRollView::balanceTargets() const
{
    if (! selection.empty() || content.blocks == nullptr)
        return selection;
    std::vector<size_t> all;
    for (size_t i = 0; i < content.blocks->size(); ++i)
        if (! (*content.blocks)[i].isConsonant)
            all.push_back(i);
    return all;
}

void PianoRollView::toggleBalancePanel()
{
    const auto show = ! balancePanel.isVisible();
    if (show && content.blocks != nullptr)
    {
        // 지금 값으로 연다: 기준 블롭(없으면 첫 대상)의 중심 음정 / 흐름
        PitchBalance current;
        current.scale = lastScale;
        const auto targets = balanceTargets();
        const auto index = primaryBlock && std::find(targets.begin(), targets.end(), *primaryBlock) != targets.end()
                               ? *primaryBlock : (targets.empty() ? content.blocks->size() : targets.front());
        if (index < content.blocks->size())
        {
            current.center = (*content.blocks)[index].tidyAmount;
            current.drift = (*content.blocks)[index].driftAmount;
        }
        balancePanel.setBalance(current);
    }
    balancePanel.setVisible(show);
    balanceButton.setToggleState(show, juce::dontSendNotification);
    if (show)
        balancePanel.toFront(false);
}

void PianoRollView::applyBalance(const PitchBalance& balance)
{
    lastScale = balance.scale;
    if (content.blocks != nullptr)
        emit(withPitchBalance(*content.blocks, balanceTargets(), balance));
}
