#pragma once
#include <juce_core/juce_core.h>

// 호스트가 문서를 편집하는 동안 오디오 스레드가 모델 객체를 읽지 않도록 막음
class ProcessingLock
{
public:
    void beginEditing() { lock.enterWrite(); }
    void endEditing()   { lock.exitWrite(); }
    juce::ScopedTryReadLock tryLockForProcessing() const { return juce::ScopedTryReadLock { lock }; }

private:
    mutable juce::ReadWriteLock lock;
};
