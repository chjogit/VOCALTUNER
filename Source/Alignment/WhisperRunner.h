#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <optional>
#include "WordAnchors.h"

// whisper.cpp(whisper-cli) 실행 파일로 단어 타임스탬프를 얻는다. 설치되어 있지 않으면 사용하지 않는다(정렬은 소리 모양만으로 진행).
// 설치 위치: %APPDATA%/VocalTuner/whisper/ (whisper-cli.exe, ggml-*.bin) 또는 환경변수 VOCALTUNER_WHISPER_DIR
struct WhisperSetup
{
    juce::File executable, model;
};

std::optional<WhisperSetup> locateWhisper();

// mono를 16 kHz WAV로 내보내 실행하고 단어(소스 시간)를 돌려준다. 실패/취소/없음이면 빈 목록.
std::vector<TimedWord> transcribeWords(const std::vector<double>& mono, int sampleRate, const WhisperSetup&,
                                       const std::function<bool()>& shouldCancel, int timeoutSeconds = 180);

// whisper.cpp -oj 출력 JSON -> 단어 (테스트 가능하도록 분리)
std::vector<TimedWord> parseWhisperJson(const juce::String& json);
