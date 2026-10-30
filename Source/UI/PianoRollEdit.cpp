#include <algorithm>
#include <cmath>
#include "BlockEditing.h"
#include "BlockTransforms.h"
#include "PianoRollStyle.h"
#include "PianoRollView.h"

namespace
{
constexpr double maxGainDb = 24.0;
constexpr double maxShiftSemitones = 48.0;
constexpr double dbPerPixel = 0.15;
constexpr float minHitHeight = 14.0f;
constexpr float jointHandleRadius = 8.0f;
constexpr float jointLineTolerance = 5.0f;

double smoothstep01(double x) noexcept
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}
}

double PianoRollView::blockAmplitude(const VocalBlock& b, double frame) const
{
    const auto& rms = content.features->rms;
    const auto centre = static_cast<long>(std::floor(frame));
    const auto lo = std::max<long>(static_cast<long>(b.beginFrame), centre - 4);
    const auto hi = std::min<long>(static_cast<long>(b.endFrame) - 1, centre + 4);
    if (hi < lo)
        return 0.0;
    double sum = 0.0;
    for (auto i = lo; i <= hi; ++i)
        sum += rms[static_cast<size_t>(i)];
    return sum / static_cast<double>(hi - lo + 1);
}

double PianoRollView::meanAmplitude(const VocalBlock& b) const
{
    const auto& rms = content.features->rms;
    double sum = 0.0;
    for (auto i = b.beginFrame; i < b.endFrame; ++i)
        sum += rms[i];
    return sum / static_cast<double>(std::max<size_t>(1, b.endFrame - b.beginFrame));
}

// 플롯 좌표계에서 블롭이 차지하는 영역 (히트 테스트용: 너무 얇아도 최소 높이를 보장)
juce::Rectangle<float> PianoRollView::blockBounds(const VocalBlock& b) const
{
    const auto x1 = timeX(b.displayStartSec());
    const auto x2 = timeX(b.displayEndSec());
    const auto centreY = midiY(blockCentreMidi(b));
    const auto amplitude = meanAmplitude(b) * rollstyle::dbToLinear(b.gainDb);
    const auto thickness = static_cast<float>(rollstyle::thicknessSemitones(b.isConsonant, amplitude) * viewport.pixelsPerSemitone());
    const auto height = std::max(thickness, minHitHeight);
    return { x1, centreY - height * 0.5f, std::max(x2 - x1, 3.0f), height };
}

std::optional<PianoRollView::BlockPick> PianoRollView::pickBlock(juce::Point<float> plotPos, bool includeConsonants) const
{
    if (content.blocks == nullptr || content.features == nullptr)
        return std::nullopt;

    std::optional<BlockPick> best;
    float bestDistance = 0.0f;
    const auto& blocks = *content.blocks;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        if ((blocks[i].isConsonant && ! includeConsonants) || ! inRegion(blocks[i]))
            continue;
        // 짧거나 얇은 블롭도 집을 수 있도록 최소 조작 영역(약 18x14 px)을 보장한다
        const auto bounds = blockBounds(blocks[i]);
        const auto hit = bounds.expanded(std::max(3.0f, 0.5f * (18.0f - bounds.getWidth())), std::max(0.0f, 0.5f * (14.0f - bounds.getHeight())));
        if (hit.getRight() < plotPos.x || hit.getX() > plotPos.x || ! hit.contains(plotPos))
            continue;

        const auto distance = std::abs(bounds.getCentreY() - plotPos.y);
        if (best && distance >= bestDistance)
            continue;

        const auto zone = std::min(edgeHandlePixels, bounds.getWidth() / 3.0f);
        BlockPick pick { i, EdgeSide::None };
        if (plotPos.x - bounds.getX() <= zone)
            pick.edge = EdgeSide::Front;
        else if (bounds.getRight() - plotPos.x <= zone)
            pick.edge = EdgeSide::Back;
        best = pick;
        bestDistance = distance;
    }
    return best;
}

// ------------------------------------------------------------------ 음정 이음선

std::vector<juce::Point<float>> PianoRollView::transitionLine(const PitchJoint& joint) const
{
    const auto& blocks = *content.blocks;
    const auto& left = blocks[joint.left];
    const auto& right = blocks[joint.right];
    const auto yLeft = midiY(blockCentreMidi(left));
    const auto yRight = midiY(blockCentreMidi(right));
    const auto xJoint = timeX(joint.timeSec);
    const auto half = std::max(joint.halfWidthSec, 0.008);
    const auto halfPixels = timeX(joint.timeSec + half) - xJoint;

    std::vector<juce::Point<float>> points;
    points.push_back({ std::min(timeX(left.displayEndSec()), xJoint - halfPixels), yLeft });
    constexpr int steps = 12;
    for (int k = 0; k <= steps; ++k)
    {
        const auto u = static_cast<double>(k) / steps;
        points.push_back({ xJoint - halfPixels + 2.0f * halfPixels * static_cast<float>(u),
                           yLeft + (yRight - yLeft) * static_cast<float>(smoothstep01(u)) });
    }
    points.push_back({ std::max(timeX(right.displayStartSec()), xJoint + halfPixels), yRight });
    return points;
}

std::optional<size_t> PianoRollView::pickTransition(juce::Point<float> plotPos) const
{
    if (content.blocks == nullptr || mode != Mode::Blob)
        return std::nullopt;

    // 블롭 위에서는 핸들만 전환선으로 취급 → 블롭 선택이 우선
    const bool overBlob = pickBlock(plotPos, true).has_value();
    std::optional<size_t> best;
    float bestDistance = 1e9f;
    for (const auto& joint : findPitchJoints(*content.blocks))
    {
        const auto xJoint = timeX(joint.timeSec);
        if (xJoint < -20.0f || xJoint > static_cast<float>(viewport.width) + 20.0f)
            continue;

        const auto line = transitionLine(joint);
        const juce::Point<float> handle(xJoint, 0.5f * (line.front().y + line.back().y));
        auto distance = handle.getDistanceFrom(plotPos) - jointHandleRadius;
        for (size_t k = 0; ! overBlob && k + 1 < line.size(); ++k)
        {
            juce::Point<float> nearest;
            juce::Line<float>(line[k], line[k + 1]).getDistanceFromPoint(plotPos, nearest);
            distance = std::min(distance, nearest.getDistanceFrom(plotPos) - jointLineTolerance);
        }
        if (distance <= 0.0f && distance < bestDistance)
        {
            best = joint.left;
            bestDistance = distance;
        }
    }
    return best;
}

// ------------------------------------------------------------------ F0 곡선 핸들

const std::vector<size_t>& PianoRollView::curveHandlesForSelection() const
{
    static const std::vector<size_t> none;
    const auto selected = singleSelection();
    if (! selected || content.blocks == nullptr || content.features == nullptr || *selected >= content.blocks->size())
        return none;

    const auto& block = (*content.blocks)[*selected];
    if (handlesBlock != selected || handlesBeginFrame != block.beginFrame || handlesUserKey != block.userHandles)
    {
        curveHandles = allCurveHandles(*content.features, block);
        handlesBlock = selected;
        handlesBeginFrame = block.beginFrame;
        handlesUserKey = block.userHandles;
    }
    return curveHandles;
}

juce::Point<float> PianoRollView::handlePoint(const VocalBlock& block, size_t frame) const
{
    const auto& f = *content.features;
    const auto index = static_cast<size_t>(&block - content.blocks->data());
    const auto pitch = editedPitchAt(f, *content.blocks, index, frame);
    return { timeX(curveFrameDisplaySec(f, block, frame)), midiY(pitch > 0.0 ? pitch : block.displayCenterMidi()) };
}

std::optional<size_t> PianoRollView::pickCurveHandle(juce::Point<float> plotPos) const
{
    const auto selected = singleSelection();
    const auto& handles = curveHandlesForSelection();
    if (! selected || handles.empty())
        return std::nullopt;

    const auto& block = (*content.blocks)[*selected];
    std::optional<size_t> best;
    float bestDistance = curveHandleRadius + 4.0f;
    for (size_t i = 0; i < handles.size(); ++i)
    {
        const auto distance = handlePoint(block, handles[i]).getDistanceFrom(plotPos);
        if (distance <= bestDistance)
        {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

// 마우스 아래의 곡선 위치 (더블클릭으로 핸들을 추가할 곳)
std::optional<PianoRollView::CurvePick> PianoRollView::pickCurvePoint(juce::Point<float> plotPos) const
{
    if (content.blocks == nullptr || content.features == nullptr)
        return std::nullopt;

    const auto& f = *content.features;
    const auto& blocks = *content.blocks;
    const auto displaySec = songToSource(viewport.xToTime(plotPos.x));
    const auto periodSec = f.framePeriodMs / 1000.0;

    std::optional<CurvePick> best;
    float bestDistance = 14.0f;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& b = blocks[i];
        if (b.isConsonant || b.endFrame <= b.beginFrame || displaySec < b.displayStartSec() - 0.05 || displaySec > b.displayEndSec() + 0.05)
            continue;

        const auto centre = std::round((displayToSourceSec(b, displaySec) - f.timeSec.front()) / periodSec);
        const auto first = std::clamp(centre - 8.0, static_cast<double>(b.beginFrame), static_cast<double>(b.endFrame - 1));
        const auto last = std::clamp(centre + 8.0, static_cast<double>(b.beginFrame), static_cast<double>(b.endFrame - 1));
        for (auto frame = static_cast<size_t>(first); frame <= static_cast<size_t>(last); ++frame)
        {
            const auto pitch = editedPitchAt(f, blocks, i, frame);
            if (pitch <= 0.0)
                continue;
            const juce::Point<float> p(timeX(curveFrameDisplaySec(f, b, frame)), midiY(pitch));
            const auto distance = p.getDistanceFrom(plotPos);
            if (distance < bestDistance)
            {
                best = CurvePick { i, frame };
                bestDistance = distance;
            }
        }
    }
    if (best)
        return best;

    // 곡선에서 멀어도 블롭 안쪽을 눌렀다면 그 시각의 프레임에 핸들을 넣는다 (짧거나 무성 구간이 낀 블롭)
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& b = blocks[i];
        if (b.isConsonant || b.endFrame <= b.beginFrame || displaySec < b.displayStartSec() || displaySec > b.displayEndSec())
            continue;
        const auto frame = std::round((displayToSourceSec(b, displaySec) - f.timeSec.front()) / periodSec);
        return CurvePick { i, static_cast<size_t>(std::clamp(frame, static_cast<double>(b.beginFrame), static_cast<double>(b.endFrame - 1))) };
    }
    return std::nullopt;
}

void PianoRollView::toggleCurveHandleAt(const CurvePick& pick)
{
    selectOnly(pick.block);
    const auto& block = (*content.blocks)[pick.block];
    auto toggled = withUserHandleToggled(block, pick.frame);
    if (toggled.userHandles == block.userHandles)
        return;
    if (onEditGestureBegin)
        onEditGestureBegin();
    emit({ { pick.block, std::move(toggled) } });
}

// 더블클릭: 선택한 블롭(없으면 누른 블롭)의 비브라토를 켜거나 끈다
void PianoRollView::toggleVibrato(const BlockPick& pick)
{
    const auto& blocks = *content.blocks;
    auto targets = isSelected(pick.index) ? selection : std::vector<size_t>{ pick.index };
    targets.erase(std::remove_if(targets.begin(), targets.end(), [&](size_t i) { return blocks[i].isConsonant; }), targets.end());
    if (targets.empty())
        return;

    const auto anyOn = std::any_of(targets.begin(), targets.end(), [&](size_t i) { return blocks[i].vibratoDepth > 0.0; });
    std::vector<BlockUpdate> updates;
    for (const auto index : targets)
    {
        auto block = blocks[index];
        block.vibratoDepth = anyOn ? 0.0 : vibratoDefaultDepth;
        updates.push_back({ index, std::move(block) });
    }
    if (onEditGestureBegin)
        onEditGestureBegin();
    emit(std::move(updates));
    if (onShapeSettled)
        onShapeSettled();
}

void PianoRollView::toggleFlatten(const BlockPick& pick)
{
    const auto& blocks = *content.blocks;
    auto targets = isSelected(pick.index) ? selection : std::vector<size_t>{ pick.index };
    targets.erase(std::remove_if(targets.begin(), targets.end(), [&](size_t i) { return blocks[i].isConsonant; }), targets.end());
    if (targets.empty())
        return;

    const auto anyOn = std::any_of(targets.begin(), targets.end(), [&](size_t i) { return blocks[i].flattenAmount > 0.0; });
    std::vector<BlockUpdate> updates;
    for (const auto index : targets)
    {
        auto block = blocks[index];
        block.flattenAmount = anyOn ? 0.0 : flattenDefault;
        updates.push_back({ index, std::move(block) });
    }
    if (onEditGestureBegin)
        onEditGestureBegin();
    emit(std::move(updates));
    if (onShapeSettled)
        onShapeSettled();
}

// 음정 흐름 더블클릭: 하나라도 켜져 있으면 모두 0%, 아니면 100% (느린 흔들림을 모두 걷어냄)
void PianoRollView::toggleDrift(const BlockPick& pick)
{
    const auto& blocks = *content.blocks;
    auto targets = isSelected(pick.index) ? selection : std::vector<size_t>{ pick.index };
    targets.erase(std::remove_if(targets.begin(), targets.end(), [&](size_t i) { return blocks[i].isConsonant; }), targets.end());
    if (targets.empty())
        return;
    const auto anyOn = std::any_of(targets.begin(), targets.end(), [&](size_t i) { return std::abs(blocks[i].driftAmount) > 1e-6; });
    std::vector<BlockUpdate> updates;
    for (const auto index : targets)
    {
        auto block = blocks[index];
        block.driftAmount = anyOn ? 0.0 : 1.0;
        updates.push_back({ index, std::move(block) });
    }
    if (onEditGestureBegin)
        onEditGestureBegin();
    emit(std::move(updates));
    if (onShapeSettled)
        onShapeSettled();
}

// ------------------------------------------------------------------ 편집 동작

void PianoRollView::emit(std::vector<BlockUpdate> updates)
{
    if (onBlocksEdited && ! updates.empty())
        onBlocksEdited(updates);
}

// 더블클릭: 선택한 블롭(더블클릭한 블롭이 선택에 속하면 선택 전체)을 음정의 가운데로 정렬
void PianoRollView::alignSelectionToNoteCenter(const BlockPick& pick)
{
    if (! isSelected(pick.index))
        selectOnly(pick.index);

    std::vector<BlockUpdate> updates;
    for (const auto index : selection)
    {
        const auto& block = (*content.blocks)[index];
        if (! block.isConsonant)
            updates.push_back({ index, alignedToNoteCenter(block) });
    }
    if (updates.empty())
        return;
    if (onEditGestureBegin)
        onEditGestureBegin();
    emit(std::move(updates));
}

const void* PianoRollView::pickOtherLayer(juce::Point<float> plotPos)
{
    for (auto it = layers.rbegin(); it != layers.rend(); ++it)
    {
        if (it->view != TrackView::Editable || it->content.blocks == nullptr || it->content.features == nullptr)
            continue;
        const ScopedContent swap(*this, it->content);
        if (pickBlock(plotPos, false))
            return it->id;
    }
    return nullptr;
}

void PianoRollView::startPlotDrag(const juce::MouseEvent& e)
{
    drag.kind = DragKind::Pan;
    if (content.blocks == nullptr || content.features == nullptr)
        return;

    const auto local = toPlot(e.position);
    drag.snapshot = content.blocks;
    const auto ctrl = e.mods.isCtrlDown();

    // 편집 중인 트랙에 걸리는 게 없고 다른 편집 가능 트랙의 블롭을 눌렀다면 그 트랙으로 전환
    const auto cutting = mode != Mode::Curve && blobTool == BlobTool::Cut;
    if (! ctrl && ! cutting && onLayerPicked && ! pickBlock(local, toolPicksConsonants())
        && ! (mode == Mode::Curve && pickCurveHandle(local)))
        if (const auto* other = pickOtherLayer(local))
        {
            drag.kind = DragKind::None;
            onLayerPicked(other);
            return;
        }

    if (mode == Mode::Curve)
    {
        if (const auto selected = singleSelection())
            if (const auto handle = pickCurveHandle(local))
            {
                drag.kind = DragKind::CurveHandle;
                drag.block = *selected;
                drag.handle = *handle;
                return;
            }
        if (const auto pick = pickBlock(local, false))
        {
            selectOnly(pick->index);
            startPreview(pick->index);
        }
        else
            setSelection({});
        return;     // 빈 곳은 화면 이동
    }

    if (mode == Mode::Blob && blobTool == BlobTool::Move && ! ctrl)
        if (const auto joint = pickTransition(local))
        {
            drag.kind = DragKind::Transition;
            drag.block = *joint;
            return;
        }

    if (const auto pick = pickBlock(local, toolPicksConsonants()))
    {
        if (ctrl)
        {
            toggleSelection(pick->index);
            drag.kind = DragKind::None;
            return;
        }
        drag.timingAllowed = mode == Mode::Blob && blobTool == BlobTool::Move && isSelected(pick->index);
        if (! isSelected(pick->index))
            selectOnly(pick->index);
        else
            primaryBlock = pick->index;
        startPreview(pick->index);
        startBlobDrag(*pick);
        return;
    }

    drag.kind = DragKind::Marquee;
    drag.start = local;
    drag.additive = ctrl;
    drag.baseSelection = ctrl ? selection : std::vector<size_t>{};
    if (! ctrl)
        setSelection({});
}

void PianoRollView::startPreview(size_t index)
{
    if (blobTool == BlobTool::Cut && mode != Mode::Curve)
        return;     // 자르기 도구는 소리 없이
    previewing = onPreviewStart != nullptr;
    if (previewing)
        onPreviewStart(index);
}

void PianoRollView::startBlobDrag(const BlockPick& pick)
{
    drag.block = pick.index;
    const auto& blocks = *drag.snapshot;
    switch (blobTool)
    {
        case BlobTool::Cut:
            drag.kind = DragKind::None;
            break;
        case BlobTool::Move:
            if (drag.timingAllowed && pick.edge != EdgeSide::None)
                return startTimingDrag(pick);       // 선택된 블롭의 끝: 길이
            drag.kind = drag.timingAllowed ? DragKind::BlobPitchOrTime : DragKind::BlobPitch;   // 방향은 처음 움직임으로 정한다
            for (const auto index : selection)
                if (! blocks[index].isConsonant)
                    drag.group.push_back(index);
            break;
        case BlobTool::Drift:
            drag.kind = DragKind::BlobDrift;
            for (const auto index : selection)
                if (! blocks[index].isConsonant)
                    drag.group.push_back(index);
            break;
        case BlobTool::Volume:
            drag.kind = DragKind::BlobGain;
            drag.group = selection;
            break;
        case BlobTool::Flatten:
            drag.kind = DragKind::BlobFlatten;
            for (const auto index : selection)
                if (! blocks[index].isConsonant)
                    drag.group.push_back(index);
            break;
        case BlobTool::Vibrato:
            drag.kind = DragKind::BlobVibrato;
            for (const auto index : selection)
                if (! blocks[index].isConsonant)
                    drag.group.push_back(index);
            break;
        case BlobTool::Timing:
            startTimingDrag(pick);
            break;
    }
}

void PianoRollView::startTimingDrag(const BlockPick& pick)
{
    const auto grouped = selection.size() > 1;
    drag.group = grouped ? selection : std::vector<size_t>{ pick.index };
    const auto frontOk = ! grouped || pick.index == drag.group.front();
    const auto backOk = ! grouped || pick.index == drag.group.back();
    drag.kind = (pick.edge == EdgeSide::Front && frontOk) ? DragKind::BlobFront
              : (pick.edge == EdgeSide::Back && backOk)   ? DragKind::BlobBack
                                                          : DragKind::BlobMove;
}

void PianoRollView::updateMarquee(const juce::MouseEvent& e)
{
    const auto current = toPlot(e.position);
    const auto rect = juce::Rectangle<float>(drag.start, current);
    marquee = rect;

    auto picked = drag.additive ? drag.baseSelection : std::vector<size_t>{};
    const auto& blocks = *content.blocks;
    for (size_t i = 0; i < blocks.size(); ++i)
        if ((toolPicksConsonants() || ! blocks[i].isConsonant) && blockBounds(blocks[i]).intersects(rect))
            picked.push_back(i);
    setSelection(std::move(picked));
}

void PianoRollView::continueDrag(const juce::MouseEvent& e)
{
    if (! drag.begun)
    {
        if (e.getDistanceFromDragStart() < 3)
            return;
        drag.begun = true;
        drag.last = e.mouseDownPosition;
        // 이동 도구로 선택된 블롭을 잡았으면: 처음 움직임이 가로면 타이밍, 세로면 음정
        if (drag.kind == DragKind::BlobPitchOrTime)
        {
            if (std::abs(e.getDistanceFromDragStartX()) > std::abs(e.getDistanceFromDragStartY()))
                startTimingDrag({ drag.block, EdgeSide::None });
            else
                drag.kind = DragKind::BlobPitch;
        }
        if (drag.kind != DragKind::Marquee && onEditGestureBegin)
            onEditGestureBegin();
    }
    if (drag.kind == DragKind::Marquee)
    {
        updateMarquee(e);
        return;
    }

    // 끌기 도중 Ctrl을 누르고 있는 동안 움직인 만큼은 1/10만 반영 (미세 조정). 도중에 눌러도 튀지 않도록 이동량을 쌓는다
    // (Ctrl+클릭은 다중 선택이라 끌기가 시작되지 않는다)
    const auto fine = e.mods.isCtrlDown() ? fineDragFactor : 1.0;
    drag.effectiveX += static_cast<double>(e.position.x - drag.last.x) * fine;
    drag.effectiveY += static_cast<double>(e.position.y - drag.last.y) * fine;
    drag.last = e.position;
    const auto dx = drag.effectiveX;
    const auto dy = drag.effectiveY;
    const auto semitones = -dy / dragOrigin.pixelsPerSemitone();
    const auto seconds = dx * dragOrigin.secondsPerPixel;
    const auto& blocks = *drag.snapshot;
    std::vector<BlockUpdate> updates;

    switch (drag.kind)
    {
        case DragKind::BlobPitch:
        {
            // 기본은 반음 눈금에 맞춤, Shift 또는 Ctrl(미세 조정)을 누르면 자유롭게. 선택한 블롭은 같은 양만큼 함께 움직인다.
            const auto& primary = blocks[drag.block];
            const auto baseCentre = primary.baseMidi + primary.pitchOffset;
            auto target = baseCentre + primary.pitchShift + semitones;
            if (! e.mods.isShiftDown() && ! e.mods.isCtrlDown())
                target = std::round(target);
            const auto delta = (target - baseCentre) - primary.pitchShift;
            for (const auto index : drag.group)
            {
                auto block = blocks[index];
                block.pitchShift = std::clamp(block.pitchShift + delta, -maxShiftSemitones, maxShiftSemitones);
                updates.push_back({ index, std::move(block) });
            }
            break;
        }
        case DragKind::BlobGain:
            for (const auto index : drag.group)
            {
                auto block = blocks[index];
                block.gainDb = std::clamp(block.gainDb - dy * dbPerPixel, -maxGainDb, maxGainDb);
                updates.push_back({ index, std::move(block) });
            }
            break;
        case DragKind::BlobFlatten:
            for (const auto index : drag.group)
            {
                auto block = blocks[index];
                block.flattenAmount = std::clamp(block.flattenAmount - dy / flattenPixelsForFull, 0.0, 1.0);
                updates.push_back({ index, std::move(block) });
            }
            break;
        case DragKind::BlobDrift:
            for (const auto index : drag.group)
            {
                auto block = blocks[index];
                block.driftAmount = std::clamp(block.driftAmount - dy / driftPixelsForFull, -1.0, 1.0);
                updates.push_back({ index, std::move(block) });
            }
            break;
        case DragKind::BlobVibrato:
            for (const auto index : drag.group)
            {
                auto block = blocks[index];
                block.vibratoDepth = std::clamp(block.vibratoDepth - dy / vibratoPixelsPerSemitone, 0.0, maxVibratoDepth);
                block.vibratoRateHz = std::clamp(block.vibratoRateHz + dx / vibratoPixelsPerHz, minVibratoRate, maxVibratoRate);
                updates.push_back({ index, std::move(block) });
            }
            break;
        case DragKind::BlobMove:  updates = groupMovedInTime(blocks, drag.group, seconds); break;
        case DragKind::BlobFront: updates = groupScaledInTime(blocks, drag.group, BlockEdge::Front, seconds); break;
        case DragKind::BlobBack:  updates = groupScaledInTime(blocks, drag.group, BlockEdge::Back, seconds); break;
        case DragKind::CurveHandle:
        {
            const auto& origin = blocks[drag.block];
            const auto displaySpan = origin.displayEndSec() - origin.displayStartSec();
            const auto sourceSpan = origin.endSec - origin.startSec;
            const auto framesPerSecond = 1000.0 / content.features->framePeriodMs;
            const auto frames = seconds * (displaySpan > 0.0 ? sourceSpan / displaySpan : 1.0) * framesPerSecond;
            const auto vertical = curveTool == CurveTool::Vertical;
            updates.push_back({ drag.block, withCurveHandleDragged(origin, curveHandlesForSelection(), drag.handle,
                                                                   vertical ? semitones : 0.0, vertical ? 0.0 : frames) });
            break;
        }
        case DragKind::Transition:
        {
            auto block = blocks[drag.block];
            block.transitionAfter = std::clamp(block.transitionAfter - dy / transitionPixelsForFull, 0.0, 1.0);
            updates.push_back({ drag.block, std::move(block) });
            break;
        }
        case DragKind::None:
        case DragKind::BlobPitchOrTime:
        case DragKind::Pan:
        case DragKind::Marquee: break;
    }
    emit(std::move(updates));
}

std::optional<double> PianoRollView::jointDisplaySecNear(double displaySourceSec) const
{
    if (content.blocks == nullptr)
        return std::nullopt;
    const auto joint = findJointNearDisplayTime(*content.blocks, displaySourceSec, jointTolerancePixels * viewport.secondsPerPixel);
    if (! joint)
        return std::nullopt;
    return 0.5 * ((*content.blocks)[*joint].displayEndSec() + (*content.blocks)[*joint + 1].displayStartSec());
}
