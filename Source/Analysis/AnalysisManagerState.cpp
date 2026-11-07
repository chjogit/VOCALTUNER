#include "AnalysisManager.h"
#include <utility>

// 프로젝트 저장/복원 (AnalysisManager의 일부)
// 저장된 블록 목록을 지문이 맞을 때만 적용한다 (original은 새 분석 결과 그대로). 성공/실패와 상관없이 대기 상태는 비운다.
void AnalysisManager::applyPendingRestore(Entry& entry)
{
    if (entry.pendingRestore.empty() || entry.features == nullptr)
        return;
    const auto bytes = std::move(entry.pendingRestore);
    entry.pendingRestore.clear();
    if (auto restored = decodeEditState(bytes.data(), bytes.size(), entry.fingerprint))
    {
        entry.blocks = std::make_shared<const std::vector<VocalBlock>>(std::move(*restored));
        entry.undoStack.clear();
        entry.redoStack.clear();
    }
}

void AnalysisManager::setRestoredState(juce::ARAAudioSource* source, std::vector<uint8_t> state)
{
    if (source == nullptr || state.empty())
        return;
    const std::lock_guard<std::mutex> lock(entriesMutex);
    auto& entry = entries[source];
    entry.pendingRestore = std::move(state);
    applyPendingRestore(entry);   // 분석이 이미 끝났을 때만 실제로 적용됨
}

std::optional<std::vector<uint8_t>> AnalysisManager::getSavedState(juce::ARAAudioSource* source) const
{
    std::shared_ptr<const std::vector<VocalBlock>> blocks;
    EditFingerprint fingerprint;
    {
        const std::lock_guard<std::mutex> lock(entriesMutex);
        const auto it = entries.find(source);
        if (it == entries.end())
            return std::nullopt;
        if (it->second.blocks == nullptr)
        {
            if (it->second.pendingRestore.empty())
                return std::nullopt;
            return it->second.pendingRestore;
        }
        if (it->second.blocks == it->second.original
            || (it->second.original != nullptr && ! hasEdits(*it->second.blocks, *it->second.original)))
            return std::nullopt;   // 편집이 없으면 저장하지 않음 (다음에 열 때 새 분석 결과를 그대로 씀)
        blocks = it->second.blocks;
        fingerprint = it->second.fingerprint;
    }
    return encodeEditState(*blocks, fingerprint);   // 잠금 밖에서 인코딩 (블록은 불변 스냅샷)
}
