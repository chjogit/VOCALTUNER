#include "AnalysisManager.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace
{
// 호스트의 오디오 스레드와 코어를 다투지 않도록 2개는 남김
int chooseNumWorkers()
{
    return juce::jlimit(1, 8, juce::SystemStats::getNumCpus() - 2);
}
}

AnalysisManager::AnalysisManager()
    : numWorkers(chooseNumWorkers()),
      workerPool(juce::ThreadPoolOptions{}.withThreadName("VocalTuner worker")
                                          .withNumberOfThreads(numWorkers)
                                          .withDesiredThreadPriority(juce::Thread::Priority::low)),
      pool(juce::ThreadPoolOptions{}.withThreadName("VocalTuner analysis")
                                    .withNumberOfThreads(1)
                                    .withDesiredThreadPriority(juce::Thread::Priority::low))
{
}

ParallelFor AnalysisManager::makeParallelFor()
{
    return [this](int count, const std::function<void(int)>& body)
    {
        const auto workers = juce::jmin(numWorkers, count);
        std::atomic<int> next { 0 };
        std::atomic<int> remaining { workers };
        juce::WaitableEvent allDone;

        for (int w = 0; w < workers; ++w)
            workerPool.addJob([&]
            {
                for (int i; (i = next++) < count;)
                    body(i);
                if (--remaining == 0)
                    allDone.signal();
            });

        allDone.wait();
    };
}

AnalysisManager::~AnalysisManager()
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    for (auto& [source, entry] : entries)
    {
        source->removeListener(this);
        if (entry.task != nullptr)
            entry.task->cancel();
        if (entry.alignmentTask != nullptr)
            entry.alignmentTask->cancel();
    }
}

void AnalysisManager::track(juce::ARAAudioSource& source)
{
    source.addListener(this);
    if (source.isSampleAccessEnabled())
        startAnalysis(source);
}

void AnalysisManager::didEnableAudioSourceSamplesAccess(juce::ARAAudioSource* source, bool enable)
{
    if (enable)
        startAnalysis(*source);
}

void AnalysisManager::willEnableAudioSourceSamplesAccess(juce::ARAAudioSource* source, bool enable)
{
    if (! enable)
        cancelAnalysis(source);
}

void AnalysisManager::willDestroyAudioSource(juce::ARAAudioSource* source)
{
    cancelAnalysis(source);
    source->removeListener(this);
    const std::lock_guard<std::mutex> lock(entriesMutex);
    entries.erase(source);
}

void AnalysisManager::startAnalysis(juce::ARAAudioSource& source)
{
    std::shared_ptr<AnalysisTask> task;
    {
        const std::lock_guard<std::mutex> lock(entriesMutex);
        auto& entry = entries[&source];
        if (entry.features != nullptr || entry.task != nullptr)
            return;
        entry.task = task = std::make_shared<AnalysisTask>(source);
    }

    pool.addJob([this, sourcePtr = &source, task]
    {
        if (auto result = task->run(makeParallelFor()))
            storeResult(sourcePtr, task, std::move(*result));
    });
}

void AnalysisManager::cancelAnalysis(juce::ARAAudioSource* source)
{
    std::shared_ptr<AnalysisTask> task, alignmentTask;
    {
        const std::lock_guard<std::mutex> lock(entriesMutex);
        const auto it = entries.find(source);
        if (it == entries.end())
            return;
        task = std::exchange(it->second.task, nullptr);
        alignmentTask = std::exchange(it->second.alignmentTask, nullptr);
        it->second.alignment = nullptr;
    }
    // entriesMutex 밖에서 취소: 워커가 storeResult에서 entriesMutex를 잡을 수 있음
    if (task != nullptr)
        task->cancel();
    if (alignmentTask != nullptr)
        alignmentTask->cancel();
}

void AnalysisManager::storeResult(juce::ARAAudioSource* source, const std::shared_ptr<AnalysisTask>& task,
                                  AnalysisResult&& result)
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    // 그 사이 취소/재시작된 작업의 결과는 버림
    if (it == entries.end() || it->second.task != task)
        return;
    it->second.features = std::make_shared<const VocalFeatures>(std::move(result.features));
    it->second.blocks = std::make_shared<const std::vector<VocalBlock>>(std::move(result.blocks));
    it->second.original = it->second.blocks;
    it->second.task = nullptr;
    it->second.alignment = nullptr;   // 새 분석 결과에는 정렬 분석을 다시 해야 함
    it->second.alignmentFailed = false;
    it->second.undoStack.clear();   // 재분석 결과가 편집을 대체함
    it->second.redoStack.clear();
    lastElapsedSeconds = result.elapsedSeconds;
    lastAudioSeconds = result.audioSeconds;
}

std::shared_ptr<const VocalFeatures> AnalysisManager::getFeatures(juce::ARAAudioSource* source) const
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    return it != entries.end() ? it->second.features : nullptr;
}

std::shared_ptr<const std::vector<VocalBlock>> AnalysisManager::getBlocks(juce::ARAAudioSource* source) const
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    return it != entries.end() ? it->second.blocks : nullptr;
}

BlockEdit AnalysisManager::editBlocksAt(juce::ARAAudioSource* source, double displaySourceSec, double toleranceSec)
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    if (it == entries.end() || it->second.features == nullptr || it->second.blocks == nullptr)
        return BlockEdit::None;

    auto edited = *it->second.blocks;
    const auto result = editBlocksAtDisplayTime(edited, *it->second.features, displaySourceSec, toleranceSec);
    if (result != BlockEdit::None)
    {
        pushUndo(it->second);
        it->second.blocks = std::make_shared<const std::vector<VocalBlock>>(std::move(edited));
    }
    return result;
}

void AnalysisManager::pushUndo(Entry& entry)
{
    constexpr size_t maxUndoSteps = 100;
    entry.undoStack.push_back(entry.blocks);
    if (entry.undoStack.size() > maxUndoSteps)
        entry.undoStack.erase(entry.undoStack.begin());
    entry.redoStack.clear();
}

void AnalysisManager::beginEditGesture(juce::ARAAudioSource* source)
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    if (it != entries.end() && it->second.blocks != nullptr)
        pushUndo(it->second);
}

bool AnalysisManager::replaceBlocks(juce::ARAAudioSource* source, const std::vector<BlockUpdate>& updates)
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    if (it == entries.end() || it->second.blocks == nullptr || updates.empty())
        return false;

    auto edited = *it->second.blocks;
    for (const auto& update : updates)
    {
        if (update.index >= edited.size())
            return false;
        edited[update.index] = update.block;
    }
    it->second.blocks = std::make_shared<const std::vector<VocalBlock>>(std::move(edited));
    return true;
}

bool AnalysisManager::undo(juce::ARAAudioSource* source)
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    if (it == entries.end() || it->second.undoStack.empty())
        return false;
    it->second.redoStack.push_back(it->second.blocks);
    it->second.blocks = std::move(it->second.undoStack.back());
    it->second.undoStack.pop_back();
    return true;
}

bool AnalysisManager::redo(juce::ARAAudioSource* source)
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    if (it == entries.end() || it->second.redoStack.empty())
        return false;
    it->second.undoStack.push_back(it->second.blocks);
    it->second.blocks = std::move(it->second.redoStack.back());
    it->second.redoStack.pop_back();
    return true;
}

UndoState AnalysisManager::getUndoState(juce::ARAAudioSource* source) const
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    if (it == entries.end())
        return {};
    return { ! it->second.undoStack.empty(), ! it->second.redoStack.empty() };
}

BlockSnapshot AnalysisManager::getOriginalBlocks(juce::ARAAudioSource* source) const
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    return it != entries.end() ? it->second.original : nullptr;
}

std::vector<BlockSnapshot> AnalysisManager::getUndoSnapshots(juce::ARAAudioSource* source) const
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    return it != entries.end() ? it->second.undoStack : std::vector<BlockSnapshot> {};
}

AnalysisSummary AnalysisManager::getSummary() const
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    AnalysisSummary summary;
    summary.lastElapsedSeconds = lastElapsedSeconds;
    summary.lastAudioSeconds = lastAudioSeconds;
    summary.numWorkers = numWorkers;
    for (const auto& [source, entry] : entries)
    {
        summary.pending += entry.task != nullptr ? 1 : 0;
        if (entry.features == nullptr)
            continue;
        ++summary.analysed;
        if (entry.blocks != nullptr)
        {
            summary.numBlocks += entry.blocks->size();
            summary.numConsonantBlocks += static_cast<size_t>(std::count_if(entry.blocks->begin(), entry.blocks->end(),
                                                                            [](const VocalBlock& b) { return b.isConsonant; }));
        }
        summary.totalFrames += entry.features->size();
        summary.voicedFrames += static_cast<size_t>(std::count_if(entry.features->midiF0.begin(),
                                                                  entry.features->midiF0.end(),
                                                                  [](double m) { return m > 0.0; }));
    }
    return summary;
}

// ------------------------------------------------------------------ 레퍼런스 정렬용 보조 분석

void AnalysisManager::setAlignmentActive(bool active)
{
    alignmentActive.store(active);
    if (active)
    {
        pollAlignment();
        return;
    }

    std::vector<std::shared_ptr<AnalysisTask>> tasks;
    {
        const std::lock_guard<std::mutex> lock(entriesMutex);
        for (auto& [source, entry] : entries)
        {
            if (entry.alignmentTask != nullptr)
                tasks.push_back(std::exchange(entry.alignmentTask, nullptr));
            entry.alignment = nullptr;   // 메모리 반환
            entry.alignmentFailed = false;
        }
    }
    for (const auto& task : tasks)
        task->cancel();
}

void AnalysisManager::pollAlignment()
{
    if (! alignmentActive.load())
        return;

    std::vector<juce::ARAAudioSource*> toStart;
    {
        const std::lock_guard<std::mutex> lock(entriesMutex);
        for (auto& [source, entry] : entries)
            if (entry.features != nullptr && entry.alignment == nullptr && entry.alignmentTask == nullptr
                && ! entry.alignmentFailed && source->isSampleAccessEnabled())
                toStart.push_back(source);
    }
    for (auto* source : toStart)
        startAlignmentAnalysis(*source);
}

void AnalysisManager::startAlignmentAnalysis(juce::ARAAudioSource& source)
{
    std::shared_ptr<AnalysisTask> task;
    std::shared_ptr<const VocalFeatures> features;
    {
        const std::lock_guard<std::mutex> lock(entriesMutex);
        const auto it = entries.find(&source);
        if (it == entries.end() || it->second.features == nullptr || it->second.alignment != nullptr || it->second.alignmentTask != nullptr)
            return;
        features = it->second.features;
        it->second.alignmentTask = task = std::make_shared<AnalysisTask>(source, false);
    }

    pool.addJob([this, sourcePtr = &source, task, features]
    {
        storeAlignment(sourcePtr, task, task->runAlignment(*features, makeParallelFor()));
    });
}

void AnalysisManager::storeAlignment(juce::ARAAudioSource* source, const std::shared_ptr<AnalysisTask>& task,
                                     std::optional<AlignmentData>&& data)
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    if (it == entries.end() || it->second.alignmentTask != task)
        return;   // 그 사이 취소/재시작됨
    it->second.alignmentTask = nullptr;
    if (data)
        it->second.alignment = std::make_shared<const AlignmentData>(std::move(*data));
    else
        it->second.alignmentFailed = true;
}

std::shared_ptr<const AlignmentData> AnalysisManager::getAlignment(juce::ARAAudioSource* source) const
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    const auto it = entries.find(source);
    return it != entries.end() ? it->second.alignment : nullptr;
}

std::pair<int, int> AnalysisManager::getAlignmentProgress() const
{
    const std::lock_guard<std::mutex> lock(entriesMutex);
    int done = 0, total = 0;
    for (const auto& [source, entry] : entries)
        if (entry.features != nullptr)
        {
            ++total;
            done += entry.alignment != nullptr ? 1 : 0;
        }
    return { done, total };
}
