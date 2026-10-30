#include "WhisperRunner.h"

namespace
{
constexpr int whisperRate = 16000;

juce::File searchDirectory()
{
    const auto env = juce::SystemStats::getEnvironmentVariable("VOCALTUNER_WHISPER_DIR", {});
    return env.isNotEmpty() ? juce::File(env)
                            : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("VocalTuner/whisper");
}

juce::File firstExisting(const juce::File& dir, std::initializer_list<const char*> names)
{
    for (const auto* name : names)
        if (const auto file = dir.getChildFile(name); file.existsAsFile())
            return file;
    return {};
}

std::vector<float> resampledTo16k(const std::vector<double>& mono, int sampleRate)
{
    std::vector<float> in(mono.begin(), mono.end());
    const auto ratio = static_cast<double>(sampleRate) / whisperRate;
    std::vector<float> out(static_cast<size_t>(static_cast<double>(in.size()) / ratio) + 1);
    juce::LagrangeInterpolator interpolator;
    interpolator.process(ratio, in.data(), out.data(), static_cast<int>(out.size()), static_cast<int>(in.size()), 0);
    return out;
}

bool writeWav(const juce::File& file, const std::vector<float>& samples)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;
    const auto options = juce::AudioFormatWriterOptions().withSampleRate(whisperRate).withNumChannels(1).withBitsPerSample(16);
    auto writer = format.createWriterFor(stream, options);   // 성공하면 writer가 스트림을 가져간다
    if (writer == nullptr)
        return false;
    const float* channel = samples.data();
    return writer->writeFromFloatArrays(&channel, 1, static_cast<int>(samples.size()));
}

bool runUntilDone(juce::ChildProcess& process, const std::function<bool()>& shouldCancel, int timeoutSeconds)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutSeconds) * 1000u;
    while (process.isRunning())
    {
        if (shouldCancel() || juce::Time::getMillisecondCounter() > deadline)
        {
            process.kill();
            return false;
        }
        juce::Thread::sleep(100);
    }
    return process.getExitCode() == 0;
}
} // namespace

std::optional<WhisperSetup> locateWhisper()
{
    const auto dir = searchDirectory();
    const auto exe = firstExisting(dir, { "whisper-cli.exe", "whisper-cli", "main.exe" });
    const auto model = firstExisting(dir, { "ggml-base.bin", "ggml-small.bin", "ggml-tiny.bin", "ggml-base.en.bin" });
    if (exe == juce::File() || model == juce::File())
        return std::nullopt;
    return WhisperSetup { exe, model };
}

std::vector<TimedWord> parseWhisperJson(const juce::String& json)
{
    std::vector<TimedWord> words;
    const auto root = juce::JSON::parse(json);
    const auto* items = root["transcription"].getArray();
    if (items == nullptr)
        return words;
    for (const auto& item : *items)
    {
        const auto text = item["text"].toString().trim();
        const auto from = item["offsets"]["from"], to = item["offsets"]["to"];
        if (text.isEmpty() || text.startsWith("[") || text.startsWith("(") || from.isVoid() || to.isVoid())
            continue;   // [BLANK_AUDIO], (음악) 같은 표시는 가사가 아님
        words.push_back({ text.toStdString(), static_cast<double>(from) / 1000.0, static_cast<double>(to) / 1000.0 });
    }
    return words;
}

std::vector<TimedWord> transcribeWords(const std::vector<double>& mono, int sampleRate, const WhisperSetup& setup,
                                       const std::function<bool()>& shouldCancel, int timeoutSeconds)
{
    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vocaltuner_whisper", "");
    if (! temp.createDirectory())
        return {};
    struct Cleanup { juce::File dir; ~Cleanup() { dir.deleteRecursively(); } } cleanup { temp };

    const auto wav = temp.getChildFile("input.wav");
    if (mono.empty() || ! writeWav(wav, resampledTo16k(mono, sampleRate)))
        return {};

    const auto outPrefix = temp.getChildFile("result");
    const auto threads = juce::String(juce::jmax(2, juce::SystemStats::getNumCpus() / 2));
    juce::StringArray command { setup.executable.getFullPathName(), "-m", setup.model.getFullPathName(), "-f", wav.getFullPathName(),
                                "-l", "auto", "-ml", "1", "-sow", "-oj", "-of", outPrefix.getFullPathName(), "-np", "-t", threads };
    // 출력은 받지 않는다(0): 파이프로 받으면서 읽지 않으면 버퍼가 차서 whisper가 멈춘 채 제한 시간까지 기다리게 된다.
    // 결과는 -oj 로 result.json 파일에 쓴다.
    juce::ChildProcess process;
    if (! process.start(command, 0) || ! runUntilDone(process, shouldCancel, timeoutSeconds))
        return {};
    return parseWhisperJson(temp.getChildFile("result.json").loadFileAsString());
}
