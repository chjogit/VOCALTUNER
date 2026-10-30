#include <algorithm>
#include <cmath>
#include "BlockEditing.h"
#include "BlockTransforms.h"
#include "PianoRollStyle.h"
#include "PianoRollView.h"

using namespace rollstyle;

void PianoRollView::paint(juce::Graphics& g)
{
    g.fillAll(background);

    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(plotBounds());
        g.setOrigin(plotBounds().getPosition());

        drawPitchGrid(g);
        drawTimeGrid(g);
        drawBackdropLayers(g);
        if (content.features != nullptr && content.blocks != nullptr)
        {
            drawBlocks(g);
            if (mode == Mode::Blob)
                drawTransitions(g);
            drawBlockCurves(g);
            drawEditHandles(g);
            drawMarquee(g);
        }
        drawPlayhead(g);
        if (mode != Mode::Curve && blobTool == BlobTool::Cut)
            drawCutGuide(g);
    }

    drawKeyLabels(g);
    drawRuler(g);
    drawToolbarHint(g);

    if (content.features == nullptr)
    {
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.setFont(15.0f);
        g.drawFittedText(content.message.isEmpty() ? "Analyzing..." : content.message,
                         plotBounds().reduced(20), juce::Justification::centred, 3);
    }
}

// ------------------------------------------------------------------ 격자 / 눈금자

void PianoRollView::drawPitchGrid(juce::Graphics& g) const
{
    const auto first = static_cast<int>(std::floor(viewport.bottomMidi)) - 1;
    const auto last = static_cast<int>(std::ceil(viewport.topMidi)) + 1;
    const auto width = static_cast<float>(viewport.width);

    for (int midi = first; midi <= last; ++midi)
    {
        const auto top = static_cast<float>(viewport.midiToY(midi + 0.5));
        const auto bottom = static_cast<float>(viewport.midiToY(midi - 0.5));
        g.setColour(isBlackKey(midi) ? blackKeyRow : whiteKeyRow);
        g.fillRect(0.0f, top, width, bottom - top);
        g.setColour(midi % 12 == 0 ? octaveLine : gridLine);
        g.drawHorizontalLine(static_cast<int>(bottom), 0.0f, width);
    }
}

double PianoRollView::timeGridStep() const
{
    static const double steps[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 300.0 };
    for (const auto candidate : steps)
        if (candidate / viewport.secondsPerPixel >= 70.0)
            return candidate;
    return steps[std::size(steps) - 1];
}

void PianoRollView::drawTimeGrid(juce::Graphics& g) const
{
    const auto step = timeGridStep();
    g.setColour(gridLine);
    for (auto n = std::ceil(viewport.startSec / step); n * step <= viewport.endSec(); n += 1.0)
        g.drawVerticalLine(static_cast<int>(viewport.timeToX(n * step)), 0.0f, static_cast<float>(viewport.height));
}

void PianoRollView::drawRuler(juce::Graphics& g) const
{
    const juce::Rectangle<int> ruler(gutterWidth, 0, getWidth() - gutterWidth, rulerHeight);
    g.setColour(juce::Colour(0xff2a2c33));
    g.fillRect(ruler);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawHorizontalLine(rulerHeight - 1, static_cast<float>(gutterWidth), static_cast<float>(getWidth()));

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(ruler);
    g.setOrigin(gutterWidth, 0);

    const auto step = timeGridStep();
    g.setFont(11.0f);
    for (auto n = std::ceil(viewport.startSec / step); n * step <= viewport.endSec(); n += 1.0)
    {
        const auto x = static_cast<int>(viewport.timeToX(n * step));
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawVerticalLine(x, static_cast<float>(rulerHeight - 8), static_cast<float>(rulerHeight - 1));
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.drawText(formatTime(n * step), x + 4, 2, 60, rulerHeight - 4, juce::Justification::centredLeft, false);
    }

    g.setColour(playhead);
    g.fillRect(static_cast<float>(viewport.timeToX(playheadSec)) - 0.75f, 0.0f, 1.5f, static_cast<float>(rulerHeight));
}

void PianoRollView::drawToolbarHint(juce::Graphics& g) const
{
    const juce::Rectangle<int> toolbar(0, rulerHeight, getWidth(), toolbarHeight);
    g.setColour(juce::Colour(0xff24262c));
    g.fillRect(toolbar);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawHorizontalLine(rulerHeight + toolbarHeight - 1, 0.0f, static_cast<float>(getWidth()));

    const char* hint = "";
    if (mode == Mode::Curve)
        hint = curveTool == CurveTool::Vertical
            ? "블록 클릭 → 핸들을 위아래로 드래그  |  곡선 더블클릭: 핸들 추가/삭제"
            : "블록 클릭 → 핸들을 좌우로 드래그 (곡선의 타이밍)  |  곡선 더블클릭: 핸들 추가/삭제";
    else if (blobTool == BlobTool::Cut)
        hint = "더블클릭: 자르기  |  초록 경계선에서 더블클릭: 붙이기";
    else if (blobTool == BlobTool::Volume)
        hint = "위아래 드래그: 소리 크기 (이웃 블록과 부드럽게 연결)  |  드래그/Ctrl+클릭/Ctrl+A: 다중 선택";
    else if (blobTool == BlobTool::Flatten)
        hint = "위아래 드래그: F0 곡선을 일자로 펴는 정도 (기본 10%, 위 = 더 평평)  |  더블클릭: 끄기/기본값";
    else if (blobTool == BlobTool::Vibrato)
        hint = "위아래 드래그: 비브라토 깊이  |  좌우 드래그: 속도  |  더블클릭: 켜기/끄기 (깊이 0 = 원래 곡선)";
    else if (blobTool == BlobTool::Drift)
        hint = "위아래 드래그: 음정 흐름(느린 흔들림) 줄이기(위)/늘리기(아래), 비브라토는 유지  |  더블클릭: 0%/100%";
    else if (blobTool == BlobTool::Timing)
        hint = "블록 드래그: 앞뒤 이동  |  양 끝 핸들: 길이 (여러 개 선택 시 전체 비율 유지)";
    else if (mode == Mode::Rhythm)
        hint = "";
    else
        hint = "위아래: 음정  |  선택한 블록 좌우: 타이밍, 양 끝: 길이  |  더블클릭: 음정 가운데로  |  끄는 중 Ctrl: 미세 조정";

    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.setFont(12.0f);
    const auto left = redoButton.getRight() + 12;
    g.drawText(juce::String::fromUTF8(hint), toolbar.withTrimmedLeft(left).reduced(4, 0), juce::Justification::centredLeft, true);
}

void PianoRollView::drawKeyLabels(juce::Graphics& g) const
{
    const auto top = rulerHeight + toolbarHeight;
    g.setColour(background);
    g.fillRect(0, top, gutterWidth, getHeight() - top);
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(0, top, gutterWidth, getHeight() - top);

    const auto showAll = viewport.pixelsPerSemitone() >= 14.0;
    g.setFont(11.0f);
    const auto first = static_cast<int>(std::floor(viewport.bottomMidi));
    const auto last = static_cast<int>(std::ceil(viewport.topMidi));
    for (int midi = first; midi <= last; ++midi)
    {
        if (! showAll && midi % 12 != 0)
            continue;
        const auto y = static_cast<float>(viewport.midiToY(midi));
        g.setColour(juce::Colours::white.withAlpha(midi % 12 == 0 ? 0.8f : 0.45f));
        g.drawText(noteName(midi), 2, top + static_cast<int>(y) - 7, gutterWidth - 6, 14, juce::Justification::centredRight, false);
    }
}

void PianoRollView::drawPlayhead(juce::Graphics& g) const
{
    const auto x = static_cast<float>(viewport.timeToX(playheadSec));
    if (x < 0.0f || x > static_cast<float>(viewport.width))
        return;
    g.setColour(playhead.withAlpha(playing ? 1.0f : 0.7f));
    g.fillRect(x - 0.75f, 0.0f, 1.5f, static_cast<float>(viewport.height));
}

// ------------------------------------------------------------------ 블롭

// 다른 트랙: 편집 가능 = 선명, 보기만 = 반투명 50% + 채도 10%, 숨김 = 그리지 않음. 트랙마다 색조를 달리한다.
void PianoRollView::drawBackdropLayers(juce::Graphics& g) const
{
    for (const auto& layer : layers)
    {
        if (layer.view == TrackView::Hidden || layer.content.features == nullptr || layer.content.blocks == nullptr)
            continue;

        const ScopedContent swap(*this, layer.content);
        const auto visibleOnly = layer.view == TrackView::Visible;
        paintingBackdrop = true;
        backdropColourIndex = layer.colourIndex;
        backdropSaturation = visibleOnly ? 0.1f : 1.0f;
        g.beginTransparencyLayer(visibleOnly ? 0.5f : 1.0f);
        drawBlocks(g);
        drawBlockCurves(g);
        g.endTransparencyLayer();
        paintingBackdrop = false;
    }
}

juce::Colour PianoRollView::tinted(juce::Colour colour) const
{
    if (! paintingBackdrop)
        return colour;
    return colour.withRotatedHue(0.17f * static_cast<float>(backdropColourIndex + 1)).withMultipliedSaturation(backdropSaturation);
}

// 모드마다 편집 대상이 아닌 쪽을 흐리게: 블록 모드는 곡선을 투명도 50%, 곡선 모드는 블록을 투명도 70%,
// 박자 모드는 둘 다 투명도 30%. 뒤에 깔리는 다른 트랙은 자기 레이어 투명도를 따로 쓴다.
float PianoRollView::blockOpacity() const noexcept
{
    if (paintingBackdrop)
        return 1.0f;
    return mode == Mode::Curve ? 0.3f : (mode == Mode::Rhythm ? 0.7f : 1.0f);
}

float PianoRollView::curveOpacity() const noexcept
{
    if (paintingBackdrop)
        return 1.0f;
    return mode == Mode::Blob ? 0.5f : (mode == Mode::Rhythm ? 0.7f : 1.0f);
}

// 이 영역(클립)이 곡에서 차지하는 구간만 그린다. 같은 녹음에서 잘라 낸 다른 조각은 자기 시간대에 따로 그려진다.
juce::Rectangle<int> PianoRollView::regionClipRect() const
{
    const auto width = static_cast<int>(std::ceil(viewport.width)), height = static_cast<int>(std::ceil(viewport.height));
    if (content.regionSongEndSec <= content.regionSongStartSec)
        return { 0, 0, width, height };
    const auto x1 = static_cast<int>(std::floor(viewport.timeToX(content.regionSongStartSec)));
    const auto x2 = static_cast<int>(std::ceil(viewport.timeToX(content.regionSongEndSec)));
    return { x1, 0, std::max(0, x2 - x1), height };
}

bool PianoRollView::inRegion(const VocalBlock& b) const
{
    if (content.regionSongEndSec <= content.regionSongStartSec)
        return true;
    return sourceToSong(b.displayEndSec()) > content.regionSongStartSec && sourceToSong(b.displayStartSec()) < content.regionSongEndSec;
}

void PianoRollView::drawBlocks(juce::Graphics& g) const
{
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(regionClipRect());
    const auto& blocks = *content.blocks;
    const auto visibleStart = songToSource(viewport.startSec);
    const auto visibleEnd = songToSource(viewport.endSec());
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        if (blocks[i].displayEndSec() < visibleStart || blocks[i].displayStartSec() > visibleEnd)
            continue;
        drawBlock(g, blocks, i);
    }
}

// 두께가 시간에 따라 변하는 블롭 (소리 크기 x 볼륨 편집). 맞닿은 블록과는 볼륨이 부드럽게 이어진다.
void PianoRollView::drawBlock(juce::Graphics& g, const std::vector<VocalBlock>& blocks, size_t index) const
{
    const auto& b = blocks[index];
    const auto x1 = timeX(b.displayStartSec());
    const auto x2 = std::max(timeX(b.displayEndSec()), x1 + 3.0f);
    const auto centreY = midiY(blockCentreMidi(b));
    const auto pixelsPerSemitone = viewport.pixelsPerSemitone();
    const auto count = juce::jlimit(2, 400, static_cast<int>(std::ceil((x2 - x1) / 3.0f)));

    std::vector<juce::Point<float>> top, bottom;
    top.reserve(static_cast<size_t>(count) + 1);
    bottom.reserve(static_cast<size_t>(count) + 1);
    for (int k = 0; k <= count; ++k)
    {
        const auto u = static_cast<double>(k) / count;
        const auto x = x1 + (x2 - x1) * static_cast<float>(u);
        const auto displaySec = b.displayStartSec() + (b.displayEndSec() - b.displayStartSec()) * u;
        const auto frame = static_cast<double>(b.beginFrame) + u * static_cast<double>(b.endFrame - b.beginFrame);
        const auto amplitude = blockAmplitude(b, frame) * dbToLinear(effectiveGainDb(blocks, index, displaySec));

        auto half = static_cast<float>(thicknessSemitones(b.isConsonant, amplitude) * pixelsPerSemitone * 0.5);
        const auto edge = std::min(x - x1, x2 - x);
        half = std::max(1.2f, half * (0.1f + 0.9f * std::sqrt(juce::jlimit(0.0f, 1.0f, edge / 8.0f))));   // 양 끝을 둥글게
        top.push_back({ x, centreY - half });
        bottom.push_back({ x, centreY + half });
    }

    juce::Path path;
    path.startNewSubPath(top.front());
    for (size_t k = 1; k < top.size(); ++k)
        path.lineTo(top[k]);
    for (size_t k = bottom.size(); k-- > 0;)
        path.lineTo(bottom[k]);
    path.closeSubPath();

    const auto selected = ! paintingBackdrop && isSelected(index);
    const auto fill = tinted(b.isConsonant ? consonantFill.withAlpha(0.8f)
                                           : voicedFill.withAlpha(b.isContinuation ? 0.75f : 0.9f))
                          .withMultipliedAlpha(blockOpacity());
    g.setColour(selected ? fill.brighter(0.25f) : fill);
    g.fillPath(path);
    g.setColour(selected ? juce::Colours::white : fill.brighter(0.5f).withAlpha(0.9f * blockOpacity()));
    g.strokePath(path, juce::PathStrokeType(selected ? 1.6f : 1.0f, juce::PathStrokeType::curved));

    if (! b.lyric.empty() && x2 - x1 > 14.0f)
    {
        g.setColour(juce::Colours::white);
        g.setFont(13.0f);
        g.drawText(juce::String::fromUTF8(b.lyric.c_str()), juce::Rectangle<float>(x1, centreY - 8.0f, x2 - x1, 16.0f),
                   juce::Justification::centred, false);
    }
}

// ------------------------------------------------------------------ F0 곡선

// 블록에 속한 곡선: 편집(이동/이음/곡선 보정/타이밍)이 반영된 모양
void PianoRollView::drawBlockCurves(juce::Graphics& g) const
{
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(regionClipRect());
    const auto& features = *content.features;
    const auto& blocks = *content.blocks;
    const auto visibleStart = songToSource(viewport.startSec);
    const auto visibleEnd = songToSource(viewport.endSec());
    const auto stride = std::max<size_t>(1, static_cast<size_t>(std::floor(viewport.secondsPerPixel / (features.framePeriodMs / 1000.0))));

    juce::Path path;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& b = blocks[i];
        if (b.isConsonant || b.displayEndSec() < visibleStart || b.displayStartSec() > visibleEnd)
            continue;

        bool drawing = false;
        for (auto frame = b.beginFrame; frame < b.endFrame; frame += stride)
        {
            const auto pitch = editedPitchAt(features, blocks, i, frame);
            if (pitch <= 0.0)
            {
                drawing = false;
                continue;
            }
            const auto x = timeX(curveFrameDisplaySec(features, b, frame));
            const auto y = midiY(pitch);
            if (drawing)
                path.lineTo(x, y);
            else
                path.startNewSubPath(x, y);
            drawing = true;
        }
    }
    g.setColour(juce::Colours::white.withAlpha(paintingBackdrop ? 0.6f : 0.9f * curveOpacity()));
    g.strokePath(path, juce::PathStrokeType(1.2f));
}

// 블롭 사이의 음정 이음선: 가운데 점을 위아래로 끌어 정도를 조절 (위 = 더 부드럽게)
void PianoRollView::drawTransitions(juce::Graphics& g) const
{
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(regionClipRect());
    const auto showHandles = blobTool == BlobTool::Move;
    for (const auto& joint : findPitchJoints(*content.blocks))
    {
        const auto xJoint = timeX(joint.timeSec);
        if (xJoint < -20.0f || xJoint > static_cast<float>(viewport.width) + 20.0f)
            continue;

        const auto line = transitionLine(joint);
        juce::Path path;
        path.startNewSubPath(line.front());
        for (size_t k = 1; k < line.size(); ++k)
            path.lineTo(line[k]);

        const auto active = hoverJoint == joint.left || (drag.kind == DragKind::Transition && drag.block == joint.left);
        g.setColour(juce::Colours::white.withAlpha(active ? 0.95f : 0.45f));
        g.strokePath(path, juce::PathStrokeType(active ? 2.0f : 1.2f));
        if (! showHandles)
            continue;

        const juce::Point<float> handle(xJoint, 0.5f * (line.front().y + line.back().y));
        const auto radius = active ? 5.5f : 4.0f;
        g.setColour(active ? accent : juce::Colours::white.withAlpha(0.8f));
        g.fillEllipse(handle.x - radius, handle.y - radius, radius * 2.0f, radius * 2.0f);
        if (active)
        {
            g.setColour(juce::Colours::white);
            g.setFont(12.0f);
            const auto percent = juce::String(juce::roundToInt(100.0 * (*content.blocks)[joint.left].transitionAfter)) + "%";
            g.drawText(percent, juce::Rectangle<float>(handle.x - 30.0f, handle.y - 24.0f, 60.0f, 14.0f), juce::Justification::centred, false);
        }
    }
}

void PianoRollView::drawMarquee(juce::Graphics& g) const
{
    if (! marquee)
        return;
    g.setColour(voicedFill.withAlpha(0.18f));
    g.fillRect(*marquee);
    g.setColour(voicedFill.brighter(0.4f).withAlpha(0.9f));
    g.drawRect(*marquee, 1.0f);
}

// ------------------------------------------------------------------ 핸들 / 가이드

void PianoRollView::drawEditHandles(juce::Graphics& g) const
{
    const auto& blocks = *content.blocks;

    if (mode == Mode::Curve)
    {
        const auto selected = singleSelection();
        if (! selected || *selected >= blocks.size())
            return;
        const auto& block = blocks[*selected];
        const auto local = hover ? toPlot(*hover) : juce::Point<float>(-100.0f, -100.0f);
        const auto active = pickCurveHandle(local);
        const auto& handles = curveHandlesForSelection();
        for (size_t i = 0; i < handles.size(); ++i)
        {
            const auto p = handlePoint(block, handles[i]);
            const auto hot = active == i || (drag.kind == DragKind::CurveHandle && drag.handle == i);
            const auto user = std::find(block.userHandles.begin(), block.userHandles.end(), handles[i]) != block.userHandles.end();
            const auto radius = hot ? curveHandleRadius + 1.5f : curveHandleRadius;
            g.setColour(hot ? accent : (user ? juce::Colour(0xffffd166) : juce::Colours::white));
            g.fillEllipse(p.x - radius, p.y - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(juce::Colour(0xff1b1c20));
            g.drawEllipse(p.x - radius, p.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);
        }
        return;
    }

    if (blobTool == BlobTool::Cut || selection.empty())
        return;

    // 기준 블롭 위의 값 표시
    if (primaryBlock && *primaryBlock < blocks.size() && isSelected(*primaryBlock))
    {
        const auto& primary = blocks[*primaryBlock];
        juce::String label;
        if (blobTool == BlobTool::Move)
            label = rollstyle::noteName(static_cast<int>(std::lround(primary.displayCenterMidi())))
                  + juce::String::formatted("  %+.2f", primary.displayCenterMidi() - std::round(primary.displayCenterMidi()));
        else if (blobTool == BlobTool::Volume)
            label = juce::String::formatted("%+.1f dB", primary.gainDb);
        else if (blobTool == BlobTool::Flatten)
            label = juce::String::fromUTF8("펴기 ") + juce::String::formatted("%.0f%%", primary.flattenAmount * 100.0);
        else if (blobTool == BlobTool::Drift)
            label = juce::String::fromUTF8("음정 흐름 ") + juce::String::formatted("%+.0f%%", primary.driftAmount * 100.0);
        else if (blobTool == BlobTool::Vibrato)
            label = primary.vibratoDepth > 0.0 ? juce::String::formatted("%.2f st  %.1f Hz", primary.vibratoDepth, primary.vibratoRateHz)
                                               : juce::String::fromUTF8("비브라토 꺼짐");
        else
            label = juce::String::formatted("%.0f ms", (primary.displayEndSec() - primary.displayStartSec()) * 1000.0);
        if (selection.size() > 1)
            label += juce::String::formatted("  (%d)", static_cast<int>(selection.size()));

        const auto bounds = blockBounds(primary);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(12.0f);
        g.drawText(label, juce::Rectangle<float>(bounds.getX(), bounds.getY() - 18.0f, 140.0f, 16.0f), juce::Justification::centredLeft, false);
    }

    const auto moveWithSelection = mode == Mode::Blob && blobTool == BlobTool::Move && ! selection.empty();
    if (blobTool != BlobTool::Timing && ! moveWithSelection)
        return;

    // 타이밍 도구: 선택(여러 개면 덩어리의 맨 앞/뒤)과 마우스를 올린 블롭의 앞뒤 핸들
    const auto drawHandles = [&](const juce::Rectangle<float>& front, const juce::Rectangle<float>& back)
    {
        const auto hot = drag.kind == DragKind::BlobFront || drag.kind == DragKind::BlobBack;
        g.setColour(juce::Colours::white.withAlpha(hot ? 1.0f : 0.8f));
        g.fillRoundedRectangle(front.getX() - 1.0f, front.getY(), 4.0f, front.getHeight(), 2.0f);
        g.fillRoundedRectangle(back.getRight() - 3.0f, back.getY(), 4.0f, back.getHeight(), 2.0f);
    };
    if (selection.size() > 1)
        drawHandles(blockBounds(blocks[selection.front()]), blockBounds(blocks[selection.back()]));
    else
        for (const auto index : { primaryBlock, hoverBlock })
            if (index && *index < blocks.size())
            {
                const auto r = blockBounds(blocks[*index]);
                drawHandles(r, r);
            }
}

// 자르기 도구: 흰 점선 = 여기서 자름, 초록 = 이 경계를 붙임
void PianoRollView::drawCutGuide(juce::Graphics& g) const
{
    if (! hover || zoneAt(*hover) != Zone::Plot)
        return;

    const auto displaySec = songToSource(viewport.xToTime(hover->x - gutterWidth));
    const auto joint = jointDisplaySecNear(displaySec);
    const auto x = timeX(joint.value_or(displaySec));
    g.setColour(joint ? accent : juce::Colours::white.withAlpha(0.85f));
    const float dashes[] = { 4.0f, 3.0f };
    g.drawDashedLine(juce::Line<float>(x, 0.0f, x, static_cast<float>(viewport.height)), dashes, 2, joint ? 2.0f : 1.0f);
}
