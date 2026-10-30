#include "PluginEditor.h"
#include "DocumentController.h"
#include <algorithm>
#include "UI/PianoRollStyle.h"
#include <utility>

VocalTunerAudioProcessorEditor::VocalTunerAudioProcessorEditor(VocalTunerAudioProcessor& p)
    : AudioProcessorEditor(&p), AudioProcessorEditorARAExtension(&p), processor(p)
{
    // 포커스를 잡지 않아야 Space/+/- 가 DAW로 전달됨
    setWantsKeyboardFocus(false);
    const auto savedTheme = theme::load();
    theme::apply(savedTheme);
    lookAndFeel.refreshColours();
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(trackPanel);
    addAndMakeVisible(pianoRoll);
    settingsPanel.setChoice(savedTheme);
    settingsPanel.onChanged = [this](const ThemeChoice& choice) { applyTheme(choice); };
    addChildComponent(settingsPanel);
    wireTrackPanel();
    pianoRoll.onLayerPicked = [this](const void* id) { activeOverride = static_cast<const juce::ARAPlaybackRegion*>(id); refreshTracks(); updatePianoRoll(); };
    pianoRoll.onSeekRequested = [this](double songSec) { seekHostTo(songSec); };
    pianoRoll.onEditGestureBegin = [this] { beginEditGesture(); };
    pianoRoll.onShapeSettled = [this] { bakeShapes(); };
    pianoRoll.onBlocksEdited = [this](const std::vector<BlockUpdate>& updates) { applyBlockEdits(updates); };
    pianoRoll.onUndo = [this] { undoEdit(); };
    pianoRoll.onRedo = [this] { redoEdit(); };
    pianoRoll.onPreviewStart = [this](size_t index) { startBlockPreview(index); };
    pianoRoll.onPreviewStop = [this] { stopBlockPreview(); };
    pianoRoll.onRevertRequested = [this](const std::vector<size_t>& indices, bool toFirst) { revertBlocks(indices, toFirst); };
    pianoRoll.onCutRequested = [this](double songSec, double toleranceSec) { cutBlockAt(songSec, toleranceSec); };
    if (auto* controller = getDocumentController())
        controller->getAlignment().onBeforeApply = [this](juce::ARAAudioSource* source) { pushUndoForOthers(source); };
    setResizable(true, true);
    setResizeLimits(900, 430, 2600, 1400);
    setSize(1240, 600);
    startTimerHz(30);
}

VocalTunerAudioProcessorEditor::~VocalTunerAudioProcessorEditor()
{
    stopTimer();
    stopBlockPreview();
    setLookAndFeel(nullptr);
    if (auto* controller = getDocumentController())
    {
        controller->getAlignment().cancelFit();         // 편집기가 닫히면 진행 중인 맞추기도 멈춘다
        controller->getAlignment().onBeforeApply = nullptr;
    }
}

// 위쪽 상태줄: 왼쪽 재생 상태, 오른쪽 분석 상태 (작고 단단하게)
void VocalTunerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(rollstyle::background);
    auto bar = getLocalBounds().removeFromTop(statusHeight);
    g.setColour(juce::Colours::black);
    g.drawHorizontalLine(bar.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
    bar.reduce(12, 0);
    g.setColour(rollstyle::inkFaint);
    g.fillRect(bar.removeFromLeft(7).withSizeKeepingCentre(7, 7));
    bar.removeFromLeft(10);

    const juce::Font mono(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), rollstyle::fontSize(11.0f), juce::Font::plain));
    g.setFont(mono);
    g.setColour(rollstyle::inkDim);
    g.drawText(juce::String(isARAEditorView() ? "ARA" : "non-ARA") + juce::String::fromUTF8("  ·  ") + formatPlayHead(processor.getPlayHeadSnapshot()),
               bar, juce::Justification::centredLeft, true);
    g.drawText(formatAnalysisStatus(), bar, juce::Justification::centredRight, true);
}

void VocalTunerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop(statusHeight);
    trackPanel.setBounds(area.removeFromLeft(trackPanel.preferredWidth()));
    pianoRoll.setBounds(area);
    const auto row = trackPanel.settingsRowBounds().translated(trackPanel.getX(), trackPanel.getY());
    settingsPanel.setBounds(trackPanel.getRight() + 8, row.getBottom() - SettingsPanel::preferredHeight,
                            SettingsPanel::preferredWidth, SettingsPanel::preferredHeight);
}

void VocalTunerAudioProcessorEditor::toggleSettings()
{
    const auto show = ! settingsPanel.isVisible();
    settingsPanel.setVisible(show);
    trackPanel.setSettingsOpen(show);
    if (show)
        settingsPanel.toFront(false);
}

// 테마는 모든 화면에 바로 반영하고 사용자 설정 파일에 저장한다
void VocalTunerAudioProcessorEditor::applyTheme(const ThemeChoice& choice)
{
    theme::apply(choice);
    theme::save(choice);
    lookAndFeel.refreshColours();
    sendLookAndFeelChange();
    repaint();
}

void VocalTunerAudioProcessorEditor::timerCallback()
{
    if (auto* controller = getDocumentController())
        controller->getAnalysis().pollAlignment();
    pollFit();
    refreshTracks();
    updatePianoRoll();
    pianoRoll.pollShortcutKeys();
    trackProcessing();
    if (const auto* controller = getDocumentController())
        if (auto* source = currentSource())
        {
            const auto undoState = controller->getAnalysis().getUndoState(source);
            pianoRoll.setUndoRedoEnabled(undoState.canUndo, undoState.canRedo);
        }
    const auto snapshot = processor.getPlayHeadSnapshot();
    if (pendingSeekSec && ! juce::exactlyEqual(snapshot.timeInSeconds, snapshotTimeAtSeek))
        pendingSeekSec.reset();   // 호스트가 새 위치를 알려옴
    pianoRoll.setPlayhead(pendingSeekSec.value_or(snapshot.timeInSeconds), snapshot.isPlaying);
    repaint(getLocalBounds().withHeight(statusHeight));
}

// 이 인스턴스(Event FX)가 붙은 클립을 우선, 없으면 호스트에서 선택한 클립
VocalTunerDocumentController* VocalTunerAudioProcessorEditor::getDocumentController()
{
    return const_cast<VocalTunerDocumentController*>(std::as_const(*this).getDocumentController());
}

void VocalTunerAudioProcessorEditor::seekHostTo(double songSec)
{
    auto* controller = getDocumentController();
    if (controller == nullptr || ! controller->requestHostPlaybackPosition(songSec))
        return;
    pendingSeekSec = std::max(0.0, songSec);
    snapshotTimeAtSeek = processor.getPlayHeadSnapshot().timeInSeconds;
}

juce::ARAAudioSource* VocalTunerAudioProcessorEditor::currentSource() const
{
    const auto* region = activeRegion();
    return region != nullptr ? region->getAudioModification()->getAudioSource() : nullptr;
}

// 편집 가능(투명도 0%) 상태인 트랙들의 소스. 2개 이상이면 한 트랙의 편집이 모두에게 적용된다.
std::vector<juce::ARAAudioSource*> VocalTunerAudioProcessorEditor::editableSources() const
{
    std::vector<juce::ARAAudioSource*> sources;
    auto* controller = const_cast<VocalTunerAudioProcessorEditor*>(this)->getDocumentController();
    if (controller == nullptr)
        return sources;
    const auto add = [&sources](juce::ARAAudioSource* s)
    {
        if (s != nullptr && std::find(sources.begin(), sources.end(), s) == sources.end())
            sources.push_back(s);
    };
    add(currentSource());
    for (const auto* region : liveRegions)
        if (controller->getAlignment().settings(region).view == TrackView::Editable)
            add(AlignmentController::sourceOf(region));
    return sources;
}

void VocalTunerAudioProcessorEditor::beginEditGesture()
{
    auto* controller = getDocumentController();
    if (controller == nullptr)
        return;
    gestureBase.clear();
    for (auto* source : editableSources())
    {
        controller->getAnalysis().beginEditGesture(source);
        gestureBase[source] = controller->getAnalysis().getBlocks(source);
    }
}

void VocalTunerAudioProcessorEditor::pushUndoForOthers(juce::ARAAudioSource* except)
{
    if (auto* controller = getDocumentController())
        for (auto* source : editableSources())
            if (source != except)
                controller->getAnalysis().beginEditGesture(source);
}

void VocalTunerAudioProcessorEditor::commitBlocks(juce::ARAAudioSource* source, const std::vector<VocalBlock>& blocks)
{
    std::vector<BlockUpdate> all;
    all.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i)
        all.push_back({ i, blocks[i] });
    if (auto* controller = getDocumentController())
        controller->getAnalysis().replaceBlocks(source, all);
}

// 드래그 중에는 매번 "드래그 시작 시점의 블록 + 이번 편집"으로 전체를 다시 만든다 (밀려났다가 돌아온 블록이 남지 않도록)
void VocalTunerAudioProcessorEditor::applyBlockEdits(const std::vector<BlockUpdate>& updates)
{
    auto* controller = getDocumentController();
    auto* primary = currentSource();
    if (controller == nullptr || primary == nullptr)
        return;

    auto& analysis = controller->getAnalysis();
    const auto current = analysis.getBlocks(primary);
    if (current == nullptr)
        return;

    const auto remembered = gestureBase.find(primary);
    const auto base = remembered != gestureBase.end() && remembered->second != nullptr && remembered->second->size() == current->size()
                        ? remembered->second : current;
    auto edited = *base;
    for (const auto& update : updates)
        if (update.index < edited.size())
            edited[update.index] = update.block;
    commitBlocks(primary, edited);

    // 다른 편집 가능 트랙에도 같은 편집을 (같은 시간대의 블롭에) 적용
    const auto sources = editableSources();
    if (sources.size() >= 2)
    {
        const auto& selection = pianoRoll.selectedBlocks();
        std::vector<std::pair<VocalBlock, VocalBlock>> changed;
        for (const auto& update : updates)
            if (update.index < base->size() && (selection.empty() || std::find(selection.begin(), selection.end(), update.index) != selection.end()))
                changed.emplace_back((*base)[update.index], update.block);
        const auto linked = makeLinkedEdit(changed);

        for (auto* source : sources)
        {
            if (source == primary)
                continue;
            const auto others = gestureBase.find(source);
            const auto otherCurrent = analysis.getBlocks(source);
            if (otherCurrent == nullptr)
                continue;
            const auto otherBase = others != gestureBase.end() && others->second != nullptr && others->second->size() == otherCurrent->size()
                                     ? others->second : otherCurrent;
            auto otherEdited = *otherBase;
            for (const auto& update : applyLinkedEdit(*otherBase, linked))
                otherEdited[update.index] = update.block;
            commitBlocks(source, otherEdited);
        }
    }
    updatePianoRoll();
}

// 펴기/비브라토를 하나 끝낼 때마다 결과를 곡선에 굳힌다. 다음 보정(다른 도구)은 이 완성된 곡선 위에서 이어진다.
void VocalTunerAudioProcessorEditor::bakeShapes()
{
    auto* controller = getDocumentController();
    if (controller == nullptr)
        return;
    auto& analysis = controller->getAnalysis();
    for (auto* source : editableSources())
    {
        const auto features = analysis.getFeatures(source);
        const auto blocks = analysis.getBlocks(source);
        if (features == nullptr || blocks == nullptr)
            continue;
        auto baked = *blocks;
        bool changed = false;
        for (auto& block : baked)
        {
            auto next = withShapeBaked(*features, block);
            changed = changed || next.curveOffset.size() != block.curveOffset.size() || next.flattenAmount < block.flattenAmount || next.vibratoDepth < block.vibratoDepth
                   || std::abs(next.driftAmount - block.driftAmount) > 1e-9;
            block = std::move(next);
        }
        if (changed)
            commitBlocks(source, baked);
    }
    updatePianoRoll();
}

void VocalTunerAudioProcessorEditor::cutBlockAt(double songSec, double toleranceSec)
{
    const auto* region = activeRegion();
    auto* controller = getDocumentController();
    if (region == nullptr || controller == nullptr)
        return;

    const auto offsetSec = region->getStartInPlaybackTime() - region->getStartInAudioModificationTime();
    auto* source = region->getAudioModification()->getAudioSource();
    if (controller->getAnalysis().editBlocksAt(source, songSec - offsetSec, toleranceSec) != BlockEdit::None)
    {
        pushUndoForOthers(source);   // 되돌리기 단계를 트랙끼리 맞춤
        updatePianoRoll();
    }
}

const juce::ARAPlaybackRegion* VocalTunerAudioProcessorEditor::findRegion() const
{
    if (auto* renderer = processor.getPlaybackRenderer())
        if (const auto& regions = renderer->getPlaybackRegions(); ! regions.empty())
            return regions.front();

    if (auto* view = getARAEditorView())
        if (const auto regions = view->getViewSelection().getEffectivePlaybackRegions<juce::ARAPlaybackRegion>(); ! regions.empty())
            return regions.front();

    return nullptr;
}

PianoRollView::Content VocalTunerAudioProcessorEditor::makeContent(const juce::ARAPlaybackRegion* region) const
{
    PianoRollView::Content content;
    if (region == nullptr)
    {
        content.message = "No clip is bound to this instance.\nInsert VocalTuner as Event FX on an audio clip.";
        return content;
    }

    content.regionSongStartSec = region->getStartInPlaybackTime();
    content.regionSongEndSec = region->getEndInPlaybackTime();
    // 곡 시간 = 소스 시간 + (재생 시작 - 소스 내 시작). 타임 스트레치는 아직 미지원
    content.offsetSec = region->getStartInPlaybackTime() - region->getStartInAudioModificationTime();

    if (const auto* controller = getDocumentController())
    {
        auto* source = region->getAudioModification()->getAudioSource();
        content.features = controller->getAnalysis().getFeatures(source);
        content.blocks = controller->getAnalysis().getBlocks(source);
    }
    if (content.features == nullptr)
        content.message = "Analyzing...";
    return content;
}

void VocalTunerAudioProcessorEditor::updatePianoRoll()
{
    const auto* region = activeRegion();
    auto content = makeContent(region);

    const auto* features = content.features.get();
    const auto start = region != nullptr ? region->getStartInPlaybackTime() : 0.0;
    const auto duration = region != nullptr ? region->getDurationInPlaybackTime() : 0.0;
    const auto regionChanged = region != shownRegion;
    const auto* blocks = content.blocks.get();
    if (! regionChanged && features == shownFeatures && blocks == shownBlocks && juce::exactlyEqual(start, shownStart) && juce::exactlyEqual(duration, shownDuration))
        return;

    // 같은 클립의 분석이 끝났을 때와 클립이 바뀌었을 때만 화면을 맞춤
    pianoRoll.setContent(std::move(content), regionChanged || features != shownFeatures);
    shownRegion = region;
    shownFeatures = features;
    shownBlocks = blocks;
    shownStart = start;
    shownDuration = duration;
}

const VocalTunerDocumentController* VocalTunerAudioProcessorEditor::getDocumentController() const
{
    auto* editorView = getARAEditorView();
    if (editorView == nullptr)
        return nullptr;
    return juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<VocalTunerDocumentController>(
        editorView->getDocumentController());
}

juce::String VocalTunerAudioProcessorEditor::formatAnalysisStatus() const
{
    const auto* controller = getDocumentController();
    if (controller == nullptr)
        return "Analysis: ARA not bound";

    const auto summary = controller->getAnalysis().getSummary();
    const auto voicedPercent = summary.totalFrames > 0
        ? 100.0 * static_cast<double>(summary.voicedFrames) / static_cast<double>(summary.totalFrames)
        : 0.0;
    auto text = juce::String::formatted("Analysis: %d done, %d running | %d frames, voiced %.0f%%",
                                        summary.analysed, summary.pending,
                                        static_cast<int>(summary.totalFrames), voicedPercent);
    if (summary.analysed > 0)
        text << juce::String::formatted(" | blocks: %d (%d consonant)",
                                        static_cast<int>(summary.numBlocks), static_cast<int>(summary.numConsonantBlocks));
    if (summary.lastElapsedSeconds > 0.0)
        text << juce::String::formatted(" | last: %.0f s audio in %.1f s (%d threads)",
                                        summary.lastAudioSeconds, summary.lastElapsedSeconds, summary.numWorkers);
    return text;
}

juce::String VocalTunerAudioProcessorEditor::formatPlayHead(const PlayHeadSnapshot& s)
{
    return juce::String(s.isPlaying ? "Playing" : "Stopped")
         + juce::String::formatted(" | BPM: %.1f | Pos: %.2f", s.bpm, s.timeInSeconds);
}

// ------------------------------------------------------------------ 여러 트랙

const juce::ARAPlaybackRegion* VocalTunerAudioProcessorEditor::activeRegion() const
{
    if (activeOverride != nullptr && std::find(liveRegions.begin(), liveRegions.end(), activeOverride) != liveRegions.end())
        return activeOverride;
    return findRegion();
}

juce::String VocalTunerAudioProcessorEditor::regionLabel(const juce::ARAPlaybackRegion* region)
{
    juce::String name;
    if (const auto* sequence = region->getRegionSequence<juce::ARARegionSequence>())
        if (const char* text = sequence->getName())
            name = juce::String::fromUTF8(text);
    if (name.isEmpty())
        if (const char* text = region->getName())
            name = juce::String::fromUTF8(text);
    if (name.isEmpty())
        name = "Track";
    return name + "  " + rollstyle::formatTime(region->getStartInPlaybackTime());
}

// 트랙 이름 (스튜디오 원 트랙 이름, 없으면 클립 이름 + 시작 시각)
juce::String VocalTunerAudioProcessorEditor::trackLabel(const juce::ARAPlaybackRegion* region)
{
    if (const auto* sequence = region->getRegionSequence<juce::ARARegionSequence>())
        if (const char* text = sequence->getName(); text != nullptr && *text != 0)
            return juce::String::fromUTF8(text);
    return regionLabel(region);
}

// 따라가는 기준의 색 이름 + 트랙 이름 (예: "네이비 기준 audio [vocals]")
juce::String VocalTunerAudioProcessorEditor::referenceLabel(const juce::ARAPlaybackRegion* target) const
{
    const auto* controller = getDocumentController();
    if (controller == nullptr || target == nullptr)
        return {};
    const auto* reference = controller->getAlignment().referenceFor(target);
    if (reference == nullptr)
        return {};
    const auto slot = controller->getAlignment().referenceSlot(reference);
    return juce::String::fromUTF8(slot == 2 ? "베이지 기준 " : "네이비 기준 ") + trackLabel(reference);
}

// 트랙 줄의 둘째 줄: 기준이면 색, 따라가는 트랙이면 맞추는 기준, 아니면 표시 상태. 클립이 여럿이면 개수
juce::String VocalTunerAudioProcessorEditor::rowDetail(const juce::ARAPlaybackRegion* region, int clipCount) const
{
    const auto* controller = getDocumentController();
    if (controller == nullptr)
        return {};
    const auto& alignment = controller->getAlignment();
    juce::String text;
    if (const auto slot = alignment.referenceSlot(region); slot > 0)
        text = juce::String::fromUTF8(slot == 1 ? "네이비 기준" : "베이지 기준");
    else if (controller->getAnalysis().isAlignmentActive() && alignment.hasBaseline(region))
        text = juce::String::fromUTF8(alignment.referenceSlot(alignment.referenceFor(region)) == 2 ? "베이지 기준에 맞춤" : "네이비 기준에 맞춤");
    else
    {
        const auto view = const_cast<AlignmentController&>(alignment).settings(region).view;
        text = juce::String::fromUTF8(view == TrackView::Editable ? "편집" : (view == TrackView::Visible ? "보기만" : "숨김"));
    }
    if (clipCount > 1)
        text << juce::String::fromUTF8(" · 클립 ") << clipCount;
    return text;
}

// 문서의 모든 재생 영역 (트랙 순서, 시작 시각 순)
std::vector<const juce::ARAPlaybackRegion*> VocalTunerAudioProcessorEditor::collectRegions()
{
    std::vector<const juce::ARAPlaybackRegion*> regions;
    auto* controller = getDocumentController();
    auto* document = controller != nullptr ? controller->getDocument<juce::ARADocument>() : nullptr;
    if (document == nullptr)
        return regions;

    for (auto* source : document->getAudioSources<juce::ARAAudioSource>())
        for (auto* modification : source->getAudioModifications<juce::ARAAudioModification>())
            for (auto* region : modification->getPlaybackRegions<juce::ARAPlaybackRegion>())
                regions.push_back(region);

    const auto orderOf = [](const juce::ARAPlaybackRegion* r)
    {
        const auto* sequence = r->getRegionSequence<juce::ARARegionSequence>();
        return sequence != nullptr ? static_cast<int>(sequence->getOrderIndex()) : 0;
    };
    std::stable_sort(regions.begin(), regions.end(), [&](const juce::ARAPlaybackRegion* a, const juce::ARAPlaybackRegion* b)
    {
        return orderOf(a) != orderOf(b) ? orderOf(a) < orderOf(b) : a->getStartInPlaybackTime() < b->getStartInPlaybackTime();
    });
    return regions;
}

void VocalTunerAudioProcessorEditor::refreshTracks()
{
    auto* controller = getDocumentController();
    if (controller == nullptr)
        return;

    auto regions = collectRegions();
    liveRegions = regions;
    auto& alignment = controller->getAlignment();
    alignment.prune(regions);
    // 보정을 끈 트랙: 새로 분석된 블록에도 꺼진 상태를 유지
    for (const auto* region : regions)
        if (! alignment.settings(region).baseCorrection)
            applyBaseCorrection(AlignmentController::sourceOf(region), false, false);

    // 편집 중인 트랙이 편집 불가로 바뀌면 다른 편집 가능 트랙으로 옮기고, 없으면 편집 가능으로 되돌린다
    if (const auto* active = activeRegion(); active != nullptr && alignment.settings(active).view != TrackView::Editable)
    {
        const auto other = std::find_if(regions.begin(), regions.end(), [&](const juce::ARAPlaybackRegion* r)
                                        { return r != active && alignment.settings(r).view == TrackView::Editable; });
        if (other != regions.end())
            activeOverride = *other;
        else
            alignment.settings(active).view = TrackView::Editable;
    }

    if (activeRegion() == nullptr)
    {
        const auto first = std::find_if(regions.begin(), regions.end(), [&](const juce::ARAPlaybackRegion* r)
                                        { return alignment.settings(r).view == TrackView::Editable; });
        if (first != regions.end())
            activeOverride = *first;
    }

    pushTrackPanel(regions);
    pushLayers(regions);
}

void VocalTunerAudioProcessorEditor::pushTrackPanel(const std::vector<const juce::ARAPlaybackRegion*>& regions)
{
    auto* controller = getDocumentController();
    auto& alignment = controller->getAlignment();
    const auto* active = activeRegion();

    // 스튜디오 원 트랙마다 한 줄 (그 트랙의 첫 클립이 줄을 대표한다)
    std::vector<TrackPanel::Row> rows;
    std::vector<int> clipCounts;
    std::vector<AlignmentController::TrackKey> keys;
    for (const auto* region : regions)
    {
        const auto key = AlignmentController::trackOf(region);
        if (const auto found = std::find(keys.begin(), keys.end(), key); found != keys.end())
        {
            ++clipCounts[(size_t) std::distance(keys.begin(), found)];
            continue;
        }
        keys.push_back(key);
        clipCounts.push_back(1);
        rows.push_back({ region, trackLabel(region), {}, alignment.settings(region).view, alignment.referenceSlot(region),
                         AlignmentController::trackOf(active) == key, alignment.settings(region).baseCorrection });
    }
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i].detail = rowDetail(static_cast<const juce::ARAPlaybackRegion*>(rows[i].id), clipCounts[i]);
    trackPanel.setRows(std::move(rows));

    const auto& analysis = controller->getAnalysis();
    const auto on = analysis.isAlignmentActive();
    juce::String status = alignmentMessage;
    if (on)
    {
        const auto [done, total] = analysis.getAlignmentProgress();
        if (done < total)
            status = juce::String::fromUTF8("발음·호흡 분석 중... ") + juce::String(done) + "/" + juce::String(total);
        else if (status.isEmpty())
            status = juce::String::fromUTF8("분석 완료");
    }
    trackPanel.setAlignmentStatus(on, on ? status : juce::String());

    const auto canEdit = on && active != nullptr && alignment.hasReference() && ! alignment.isReference(active);
    trackPanel.setActiveRoles(active != nullptr ? alignment.settings(active).roles : TrackRoles(), canEdit, referenceLabel(active));
}

// 편집 중인 트랙을 뺀 다른 트랙을 피아노롤 뒤에 겹쳐 그린다
void VocalTunerAudioProcessorEditor::pushLayers(const std::vector<const juce::ARAPlaybackRegion*>& regions)
{
    auto* controller = getDocumentController();
    auto& alignment = controller->getAlignment();
    const auto* active = activeRegion();

    std::vector<const void*> key;
    std::vector<PianoRollView::Layer> layers;
    int colour = 0;
    for (const auto* region : regions)
    {
        ++colour;
        if (region == active)
            continue;
        const auto view = alignment.settings(region).view;
        auto* source = region->getAudioModification()->getAudioSource();
        auto features = controller->getAnalysis().getFeatures(source);
        auto blocks = controller->getAnalysis().getBlocks(source);

        key.push_back(region);
        key.push_back(features.get());
        key.push_back(blocks.get());
        key.push_back(reinterpret_cast<const void*>(static_cast<intptr_t>(view)));
        key.push_back(reinterpret_cast<const void*>(static_cast<intptr_t>(alignment.referenceSlot(region))));
        key.push_back(reinterpret_cast<const void*>(static_cast<intptr_t>(std::llround(region->getStartInPlaybackTime() * 1000.0))));

        PianoRollView::Layer layer;
        layer.id = region;
        layer.view = view;
        layer.colourIndex = colour;
        layer.referenceSlot = alignment.referenceSlot(region);
        layer.content.features = std::move(features);
        layer.content.blocks = std::move(blocks);
        layer.content.offsetSec = AlignmentController::offsetOf(region);
        layer.content.regionSongStartSec = region->getStartInPlaybackTime();
        layer.content.regionSongEndSec = region->getEndInPlaybackTime();
        layers.push_back(std::move(layer));
    }
    if (key == shownLayerKey)
        return;
    shownLayerKey = std::move(key);
    pianoRoll.setLayers(std::move(layers));
}

void VocalTunerAudioProcessorEditor::wireTrackPanel()
{
    const auto asRegion = [](const void* id) { return static_cast<const juce::ARAPlaybackRegion*>(id); };

    trackPanel.onViewChanged = [this, asRegion](const void* id, TrackView view)
    {
        if (auto* controller = getDocumentController())
            controller->getAlignment().settings(asRegion(id)).view = view;
        refreshTracks();
        updatePianoRoll();
    };
    trackPanel.onActivate = [this, asRegion](const void* id)
    {
        auto* controller = getDocumentController();
        if (controller == nullptr || controller->getAlignment().settings(asRegion(id)).view != TrackView::Editable)
            return;
        // 이미 그 트랙의 다른 클립을 편집 중이면 그대로 둔다
        if (AlignmentController::trackOf(activeRegion()) != AlignmentController::trackOf(asRegion(id)))
            activeOverride = asRegion(id);
        refreshTracks();
        updatePianoRoll();
    };
    trackPanel.onReferenceChosen = [this, asRegion](const void* id, int slot)
    {
        if (auto* controller = getDocumentController())
        {
            controller->getAlignment().setReferenceSlot(asRegion(id), slot);
            if (slot > 0)
                setBaseCorrection(asRegion(id), false);     // 기준 트랙은 원래 음정 그대로 쓴다
        }
        alignmentMessage.clear();
        refreshTracks();
    };
    trackPanel.onSettingsClicked = [this] { toggleSettings(); };
    trackPanel.onCollapseToggled = [this]
    {
        trackPanel.setCollapsed(! trackPanel.isCollapsed());
        if (trackPanel.isCollapsed() && settingsPanel.isVisible())
            toggleSettings();
        resized();
    };
    trackPanel.onAlignmentToggled = [this](bool on)
    {
        if (auto* controller = getDocumentController())
            controller->getAnalysis().setAlignmentActive(on);
        alignmentMessage.clear();
        refreshTracks();
    };
    trackPanel.onRolesGestureBegin = [this] { beginEditGesture(); };
    trackPanel.onRolesChanged = [this](const TrackRoles& roles)
    {
        auto* controller = getDocumentController();
        const auto* active = activeRegion();
        if (controller == nullptr || active == nullptr)
            return;
        auto& settings = controller->getAlignment().settings(active);
        settings.roles = roles;
        // 이미 맞춘 클립이면 정도/역할 변경을 바로 반영 (트랙의 모든 클립)
        for (const auto* region : liveRegions)
            if (AlignmentController::trackOf(region) == AlignmentController::trackOf(active) && controller->getAlignment().hasBaseline(region))
                alignmentMessage = controller->getAlignment().fit(region, false, false).message;
        updatePianoRoll();
    };
    trackPanel.onFitRequested = [this] { fitTracks(false); };
    trackPanel.onFitAllRequested = [this] { fitTracks(true); };
    trackPanel.onBaseCorrectionToggled = [this, asRegion](const void* id, bool on) { setBaseCorrection(asRegion(id), on); };
    trackPanel.onFitCancelled = [this]
    {
        if (auto* controller = getDocumentController())
            controller->getAlignment().cancelFit();
    };
}

void VocalTunerAudioProcessorEditor::fitTracks(bool all)
{
    auto* controller = getDocumentController();
    if (controller == nullptr)
        return;
    auto& alignment = controller->getAlignment();

    std::vector<const juce::ARAPlaybackRegion*> targets;
    for (const auto* region : liveRegions)
    {
        const auto& roles = alignment.settings(region).roles;
        if (all ? (! alignment.isReference(region) && (roles.timing || roles.pitch))
                : AlignmentController::trackOf(region) == AlignmentController::trackOf(activeRegion()))
            targets.push_back(region);
    }
    alignmentMessage.clear();
    alignment.startFit(std::move(targets));     // 진행은 timerCallback -> pollFit
    pollFit();
}

// 트랙의 모든 클립에 기초 자동 보정을 켜거나 끈다 (되돌리기 가능)
void VocalTunerAudioProcessorEditor::setBaseCorrection(const juce::ARAPlaybackRegion* region, bool on)
{
    auto* controller = getDocumentController();
    if (controller == nullptr || region == nullptr)
        return;
    controller->getAlignment().settings(region).baseCorrection = on;
    for (const auto* r : liveRegions)
        if (AlignmentController::trackOf(r) == AlignmentController::trackOf(region))
            applyBaseCorrection(AlignmentController::sourceOf(r), on, true);
    refreshTracks();
    updatePianoRoll();
}

void VocalTunerAudioProcessorEditor::applyBaseCorrection(juce::ARAAudioSource* source, bool on, bool undoable)
{
    auto& analysis = getDocumentController()->getAnalysis();
    const auto blocks = analysis.getBlocks(source);
    if (blocks == nullptr)
        return;
    const auto updates = withBaseCorrection(*blocks, on);
    if (updates.empty())
        return;
    if (undoable)
        analysis.beginEditGesture(source);
    analysis.replaceBlocks(source, updates);
}

void VocalTunerAudioProcessorEditor::pollFit()
{
    auto* controller = getDocumentController();
    if (controller == nullptr || ! controller->getAlignment().isFitting())
        return;
    const auto status = controller->getAlignment().pollFit();
    trackPanel.setFitProgress(status.running ? status.progress : -1.0, status.stage);
    if (status.finished)
    {
        alignmentMessage = status.message;
        updatePianoRoll();
    }
}
