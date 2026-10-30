#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include "PreviewRequest.h"
#include "ProcessingLock.h"
#include "RenderCache.h"

class PlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    PlaybackRenderer(ARA::PlugIn::DocumentController*, const ProcessingLock&, RenderCache&, const PreviewRequest&);

    void prepareToPlay(double sampleRate, int maxBlockSize, int numChannels,
                       juce::AudioProcessor::ProcessingPrecision, AlwaysNonRealtime) override;
    void releaseResources() override;
    bool processBlock(juce::AudioBuffer<float>&, juce::AudioProcessor::Realtime,
                      const juce::AudioPlayHead::PositionInfo&) noexcept override;

    using ARAPlaybackRenderer::processBlock;

private:
    struct Reader
    {
        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::BufferingAudioReader* buffered = nullptr;   // reader가 버퍼링 리더일 때만 유효
    };

    // 소스별 재생 상태 (오디오 스레드 전용). 렌더 결과가 바뀌면 짧게 크로스페이드해 '툭' 소리를 막는다.
    struct Playing
    {
        std::shared_ptr<const AudioPlanes> current, previous;  // nullptr = 원본 리더
        bool known = false;
        int fadeDone = 0;
    };

    // 블록 미리 듣기 (호스트가 멈춰 있을 때). 누르는 동안 한 번 재생하고, 놓으면 짧게 줄이며 멈춘다.
    struct Preview
    {
        juce::ARAAudioSource* source = nullptr;
        juce::int64 position = 0, end = 0;
        std::uint32_t generation = 0;
        int fadeIn = 0;         // 지금까지 올린 샘플 수
        int fadeOut = -1;       // -1 = 줄이는 중 아님, 그 외 남은 샘플 수
    };

    void renderPreview(juce::AudioBuffer<float>&, juce::AudioProcessor::Realtime);
    void followPreviewRequest();
    juce::ARAAudioSource* ownedSource(const void* id) const noexcept;
    void applyPreviewEnvelope(int numSamples) noexcept;

    Reader createReader(juce::ARAAudioSource*);
    std::shared_ptr<const AudioPlanes> latestFor(juce::ARAAudioSource*, Playing*, juce::AudioProcessor::Realtime) const;
    void switchTo(Playing&, std::shared_ptr<const AudioPlanes>, juce::AudioProcessor::Realtime) const noexcept;
    bool readSource(const AudioPlanes*, juce::ARAAudioSource*, juce::AudioBuffer<float>& target, int startInBuffer,
                    int numSamples, juce::int64 startInSource, juce::AudioProcessor::Realtime);
    void fadeFromPrevious(Playing&, juce::ARAAudioSource*, int startInBuffer, int numSamples, juce::int64 startInSource,
                          juce::AudioProcessor::Realtime);
    bool renderRegion(juce::ARAPlaybackRegion&, juce::AudioBuffer<float>& target,
                      juce::Range<juce::int64> block, juce::AudioProcessor::Realtime, bool& didRender);
    bool canRender(const juce::ARAAudioSource&) const noexcept;
    bool copyRendered(const AudioPlanes&, juce::AudioBuffer<float>& target, int startInBuffer, int numSamples,
                      juce::int64 startInSource) const noexcept;

    const ProcessingLock& processingLock;
    RenderCache& renderCache;
    const PreviewRequest& previewRequest;
    Preview preview;
    juce::TimeSliceThread readAheadThread { "VocalTuner read-ahead" };
    std::map<juce::ARAAudioSource*, Reader> readers;
    std::map<juce::ARAAudioSource*, Playing> playing;      // prepareToPlay에서 만든다 (오디오 스레드에서 할당하지 않게)
    juce::AudioBuffer<float> mixBuffer, fadeBuffer;
    int fadeLength = 960;
    double sampleRate = 44100.0;
    int numChannels = 2;
    bool useBuffering = true;
};
