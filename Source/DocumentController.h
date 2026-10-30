#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AlignmentController.h"
#include "Analysis/AnalysisManager.h"
#include "PreviewRequest.h"
#include "ProcessingLock.h"
#include "RenderCache.h"

class VocalTunerDocumentController : public juce::ARADocumentControllerSpecialisation
{
public:
    using ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

    const AnalysisManager& getAnalysis() const noexcept { return analysis; }
    AnalysisManager& getAnalysis() noexcept { return analysis; }
    AlignmentController& getAlignment() noexcept { return alignment; }
    const AlignmentController& getAlignment() const noexcept { return alignment; }

    PreviewRequest& getPreview() noexcept { return preview; }

    // 호스트(Studio One)의 재생 위치를 옮기도록 요청. 호스트가 지원하지 않으면 false. 메시지 스레드 전용.
    bool requestHostPlaybackPosition(double songSec);

protected:
    void willBeginEditing(juce::ARADocument*) override { processingLock.beginEditing(); }
    void didEndEditing(juce::ARADocument*) override    { processingLock.endEditing(); }

    juce::ARAAudioSource* doCreateAudioSource(juce::ARADocument*, ARA::ARAAudioSourceHostRef) override;
    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;

    // 분석 결과 직렬화 전까지는 저장할 상태 없음 (프로젝트를 열 때마다 재분석)
    bool doRestoreObjectsFromStream(juce::ARAInputStream&, const juce::ARARestoreObjectsFilter*) override;
    bool doStoreObjectsToStream(juce::ARAOutputStream&, const juce::ARAStoreObjectsFilter*) override;

private:
    ProcessingLock processingLock;
    AnalysisManager analysis;
    AlignmentController alignment { analysis };
    RenderCache renderCache { analysis };
    PreviewRequest preview;
};
