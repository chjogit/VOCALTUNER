#include "PluginEditor.h"
#include "DocumentController.h"
#include <algorithm>
#include <cmath>
#include <utility>

// 블록을 누르고 있는 동안 편집이 반영된 소리를 들려준다.
// 호스트가 멈춘 동안에도 오디오 처리를 하면 플러그인이 그 블록만 직접 재생하고,
// 처리가 멈춰 있으면(스튜디오 원 이벤트 FX 등) 호스트에 그 블록 위치부터 재생을 요청하고 놓으면 멈춘다.

void VocalTunerAudioProcessorEditor::trackProcessing()
{
    const auto count = processor.getProcessCount();
    const auto now = juce::Time::getMillisecondCounter();
    if (count != lastProcessCount)
    {
        lastProcessCount = count;
        lastProcessChangeMs = now;
    }
}

bool VocalTunerAudioProcessorEditor::hostProcessesWhileStopped() const
{
    return juce::Time::getMillisecondCounter() - lastProcessChangeMs < 200;
}

void VocalTunerAudioProcessorEditor::startBlockPreview(size_t index)
{
    auto* controller = getDocumentController();
    auto* source = currentSource();
    const auto* region = activeRegion();
    if (controller == nullptr || source == nullptr || region == nullptr || processor.getPlayHeadSnapshot().isPlaying)
        return;
    const auto blocks = controller->getAnalysis().getBlocks(source);
    if (blocks == nullptr || index >= blocks->size())
        return;

    const auto& block = (*blocks)[index];
    if (hostProcessesWhileStopped())
    {
        const auto rate = source->getSampleRate();
        const auto begin = (juce::int64) std::floor(std::max(0.0, block.displayStartSec()) * rate);
        const auto end = (juce::int64) std::ceil(block.displayEndSec() * rate);
        controller->getPreview().start(source, begin, end);
        return;
    }
    previewSongStart = block.displayStartSec() + AlignmentController::offsetOf(region);
    if (controller->requestHostPlaybackPosition(previewSongStart))
        previewByHost = controller->requestHostPlayback(true);
}

void VocalTunerAudioProcessorEditor::stopBlockPreview()
{
    auto* controller = getDocumentController();
    if (controller == nullptr)
        return;
    controller->getPreview().stop();
    if (std::exchange(previewByHost, false))
    {
        controller->requestHostPlayback(false);
        controller->requestHostPlaybackPosition(previewSongStart);   // 재생 위치는 블록 앞으로 돌려 둔다
    }
}
