#include "PluginEditor.h"
#include "DocumentController.h"
#include "Analysis/BlockHistory.h"

// 되돌리기: Ctrl+Z / Ctrl+Shift+Z는 전체 기록, 우클릭 메뉴는 블록별 기록

void VocalTunerAudioProcessorEditor::undoEdit()
{
    auto* controller = getDocumentController();
    if (controller == nullptr)
        return;
    for (auto* source : editableSources())
        controller->getAnalysis().undo(source);
    updatePianoRoll();
}

void VocalTunerAudioProcessorEditor::redoEdit()
{
    auto* controller = getDocumentController();
    if (controller == nullptr)
        return;
    for (auto* source : editableSources())
        controller->getAnalysis().redo(source);
    updatePianoRoll();
}

// 고른 블록만 '처음으로' 또는 '한 단계 전'으로. 그 자체도 되돌리기 한 단계가 된다.
void VocalTunerAudioProcessorEditor::revertBlocks(const std::vector<size_t>& indices, bool toFirst)
{
    auto* controller = getDocumentController();
    auto* source = currentSource();
    const auto* region = activeRegion();
    if (controller == nullptr || source == nullptr || region == nullptr)
        return;
    auto& analysis = controller->getAnalysis();
    const auto blocks = analysis.getBlocks(source);
    const auto original = analysis.getOriginalBlocks(source);
    if (blocks == nullptr || original == nullptr)
        return;

    const auto history = analysis.getUndoSnapshots(source);
    const auto baseTidy = controller->getAlignment().settings(region).baseCorrection ? defaultTidyAmount : 0.0;
    std::vector<BlockUpdate> updates;
    for (const auto index : indices)
    {
        if (index >= blocks->size())
            continue;
        const auto& current = (*blocks)[index];
        auto reverted = toFirst ? std::optional<VocalBlock>(firstStateOf(current, *original, baseTidy))
                                : previousStateOf(current, history);
        if (reverted && ! sameEdits(*reverted, current))
            updates.push_back({ index, std::move(*reverted) });
    }
    if (updates.empty())
        return;
    beginEditGesture();
    analysis.replaceBlocks(source, updates);
    updatePianoRoll();
}
