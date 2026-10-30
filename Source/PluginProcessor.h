#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>

struct PlayHeadSnapshot
{
    bool isPlaying = false;
    double bpm = 120.0;
    double timeInSeconds = 0.0;
};

class VocalTunerAudioProcessor : public juce::AudioProcessor,
                                 public juce::AudioProcessorARAExtension
{
public:
    VocalTunerAudioProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // ARA 모드의 상태는 DocumentController가 저장
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    PlayHeadSnapshot getPlayHeadSnapshot() const noexcept;
    // processBlock이 불린 횟수. 호스트가 멈춰 있을 때도 오디오 처리를 하는지 편집기가 알아보는 데 쓴다
    std::uint32_t getProcessCount() const noexcept { return processCount.load(std::memory_order_relaxed); }

private:
    juce::AudioPlayHead::PositionInfo readPosition() const;
    void storePosition(const juce::AudioPlayHead::PositionInfo&) noexcept;
    void clearUnusedOutputs(juce::AudioBuffer<float>&) const;

    // 오디오 스레드 쓰기 / UI 스레드 읽기
    std::atomic<bool> isPlaying { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> timeInSeconds { 0.0 };
    std::atomic<std::uint32_t> processCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalTunerAudioProcessor)
};
