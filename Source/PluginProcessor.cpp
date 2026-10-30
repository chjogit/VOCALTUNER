#include "PluginProcessor.h"
#include "PluginEditor.h"

VocalTunerAudioProcessor::VocalTunerAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void VocalTunerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    prepareToPlayForARA(sampleRate, samplesPerBlock,
                        getMainBusNumOutputChannels(), getProcessingPrecision());
}

void VocalTunerAudioProcessor::releaseResources()
{
    releaseResourcesForARA();
}

double VocalTunerAudioProcessor::getTailLengthSeconds() const
{
    double tail = 0.0;
    getTailLengthSecondsForARA(tail);
    return tail;
}

void VocalTunerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto position = readPosition();
    storePosition(position);
    processCount.fetch_add(1, std::memory_order_relaxed);

    const auto realtime = isNonRealtime() ? Realtime::no : Realtime::yes;
    if (processBlockForARA(buffer, realtime, position))
        return;

    clearUnusedOutputs(buffer);
}

juce::AudioPlayHead::PositionInfo VocalTunerAudioProcessor::readPosition() const
{
    if (auto* host = getPlayHead())
        if (auto position = host->getPosition())
            return *position;
    return {};
}

void VocalTunerAudioProcessor::storePosition(const juce::AudioPlayHead::PositionInfo& position) noexcept
{
    isPlaying.store(position.getIsPlaying(), std::memory_order_relaxed);
    if (auto value = position.getBpm())
        bpm.store(*value, std::memory_order_relaxed);
    if (auto value = position.getTimeInSeconds())
        timeInSeconds.store(*value, std::memory_order_relaxed);
}

void VocalTunerAudioProcessor::clearUnusedOutputs(juce::AudioBuffer<float>& buffer) const
{
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
}

PlayHeadSnapshot VocalTunerAudioProcessor::getPlayHeadSnapshot() const noexcept
{
    return { isPlaying.load(std::memory_order_relaxed),
             bpm.load(std::memory_order_relaxed),
             timeInSeconds.load(std::memory_order_relaxed) };
}

juce::AudioProcessorEditor* VocalTunerAudioProcessor::createEditor()
{
    return new VocalTunerAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalTunerAudioProcessor();
}
