#include "PluginEditor.h"
#include "DocumentController.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include "Analysis/BlockHistory.h"

// 블록을 누르고 있는 동안 편집이 반영된 소리를 들려준다.
// 호스트가 멈춘 동안에도 오디오 처리를 하면 플러그인이 그 블록만 직접 재생하고,
// 처리가 멈춰 있으면(스튜디오 원 이벤트 FX 등) 호스트에 그 블록 위치부터 재생을 요청하고 놓으면 멈춘다.

namespace
{
// 끌기 중에는 값이 같아도 블록 목록이 새로 만들어진다. 소리가 같으면 다시 렌더/재생하지 않는다.
bool soundsSame(const std::vector<VocalBlock>& a, const std::vector<VocalBlock>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].beginFrame != b[i].beginFrame || a[i].endFrame != b[i].endFrame || ! sameEdits(a[i], b[i]))
            return false;
    return true;
}
} // namespace

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
    if (controller == nullptr || source == nullptr || activeRegion() == nullptr || processor.getPlayHeadSnapshot().isPlaying)
        return;
    const auto blocks = controller->getAnalysis().getBlocks(source);
    if (blocks == nullptr || index >= blocks->size())
        return;

    previewSource = source;
    previewIndex = index;
    previewBlocks = blocks;
    previewUsesHost = ! hostProcessesWhileStopped();
    requestPreviewRender();
    // 전체 렌더가 최신이면 바로 재생, 아니면 구간 렌더가 끝나는 대로 재생 (편집 전 소리가 나지 않게)
    previewWaiting = ! controller->getRenderCache().isUpToDate(source);
    if (! previewWaiting)
        playBlockPreview();
}

void VocalTunerAudioProcessorEditor::requestPreviewRender()
{
    auto* controller = getDocumentController();
    if (controller == nullptr || previewBlocks == nullptr || previewIndex >= previewBlocks->size())
        return;
    const auto& block = (*previewBlocks)[previewIndex];
    const auto rate = previewSource->getSampleRate();
    controller->getRenderCache().requestPreview(previewSource, previewBlocks, (juce::int64) std::floor(std::max(0.0, block.displayStartSec()) * rate),
                                                (juce::int64) std::ceil(block.displayEndSec() * rate));
}

// 누르고 있는 동안: 피치 등이 바뀌면 그 구간만 다시 렌더한다. 재생은 끊지 않고 새 결과가 오는 대로 그 자리에서 넘어간다.
// 블록 끝까지 들리면 처음부터 다시 (누르고 있는 동안 계속 들리게)
void VocalTunerAudioProcessorEditor::pollBlockPreview()
{
    auto* controller = getDocumentController();
    if (controller == nullptr || previewSource == nullptr)
        return;
    const auto blocks = controller->getAnalysis().getBlocks(previewSource);
    if (blocks == nullptr || previewIndex >= blocks->size() || ! juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        return stopBlockPreview();      // 클릭 홀드: 버튼을 놓으면 바로 멈춘다
    if (blocks != previewBlocks && ! soundsSame(*blocks, *previewBlocks))
    {
        previewBlocks = blocks;
        requestPreviewRender();
    }
    const auto now = juce::Time::getMillisecondCounter();
    if (previewWaiting ? controller->getRenderCache().isPreviewReady(previewSource, previewBlocks)
                       : now - previewStartedMs >= previewLengthMs)
    {
        previewWaiting = false;
        playBlockPreview();
    }
}

void VocalTunerAudioProcessorEditor::playBlockPreview()
{
    auto* controller = getDocumentController();
    const auto* region = activeRegion();
    if (controller == nullptr || region == nullptr || previewBlocks == nullptr || previewIndex >= previewBlocks->size())
        return;
    const auto& block = (*previewBlocks)[previewIndex];
    previewStartedMs = juce::Time::getMillisecondCounter();
    previewLengthMs = (juce::uint32) juce::jmax(50.0, 1000.0 * (block.displayEndSec() - std::max(0.0, block.displayStartSec())));
    if (! previewUsesHost)
    {
        const auto rate = previewSource->getSampleRate();
        controller->getPreview().start(previewSource, (juce::int64) std::floor(std::max(0.0, block.displayStartSec()) * rate),
                                       (juce::int64) std::ceil(block.displayEndSec() * rate));
        return;
    }
    previewSongStart = block.displayStartSec() + AlignmentController::offsetOf(region);
    controller->requestHostPlaybackPosition(previewSongStart);
    if (! previewByHost)
    {
        controller->requestHostPlayback(true);
        previewByHost = true;       // 요청 결과와 상관없이 놓을 때 반드시 정지를 요청한다
    }
}

void VocalTunerAudioProcessorEditor::stopBlockPreview()
{
    auto* controller = getDocumentController();
    previewSource = nullptr;
    previewBlocks = nullptr;
    previewWaiting = false;
    if (controller == nullptr)
        return;
    controller->getRenderCache().clearPreview();
    controller->getPreview().stop();
    if (std::exchange(previewByHost, false))
    {
        controller->requestHostPlayback(false);
        controller->requestHostPlaybackPosition(previewSongStart);   // 재생 위치는 블록 앞으로 돌려 둔다
    }
}
