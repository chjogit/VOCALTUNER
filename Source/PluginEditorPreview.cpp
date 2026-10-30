#include "PluginEditor.h"
#include "DocumentController.h"
#include <algorithm>
#include <cmath>

// 블록을 누르고 있는 동안 편집이 반영된 소리를 들려준다 (호스트가 멈춰 있을 때)

void VocalTunerAudioProcessorEditor::startBlockPreview(size_t index)
{
    auto* controller = getDocumentController();
    auto* source = currentSource();
    if (controller == nullptr || source == nullptr || processor.getPlayHeadSnapshot().isPlaying)
        return;
    const auto blocks = controller->getAnalysis().getBlocks(source);
    if (blocks == nullptr || index >= blocks->size())
        return;

    const auto& block = (*blocks)[index];
    const auto rate = source->getSampleRate();
    const auto begin = (juce::int64) std::floor(std::max(0.0, block.displayStartSec()) * rate);
    const auto end = (juce::int64) std::ceil(block.displayEndSec() * rate);
    controller->getPreview().start(source, begin, end);
}

void VocalTunerAudioProcessorEditor::stopBlockPreview()
{
    if (auto* controller = getDocumentController())
        controller->getPreview().stop();
}
