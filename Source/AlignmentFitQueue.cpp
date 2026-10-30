#include "AlignmentController.h"
#include <chrono>

// AlignmentController의 백그라운드 맞추기. 정렬(DTW)만 다른 스레드에서 돌리고, 적용은 메시지 스레드에서 한다.
namespace
{
juce::String u8(const char* text) { return juce::String::fromUTF8(text); }
}

AlignmentController::~AlignmentController()
{
    cancelFit();
    if (job != nullptr && job->pending.valid())
        job->pending.wait();
}

void AlignmentController::startFit(std::vector<const juce::ARAPlaybackRegion*> targets)
{
    cancelFit();
    if (job != nullptr && job->pending.valid())
        job->pending.wait();
    job = std::make_unique<FitJob>();
    job->targets = std::move(targets);
}

void AlignmentController::cancelFit()
{
    if (job != nullptr)
        job->cancel->store(true);
}

AlignmentController::FitProgress AlignmentController::finishJob(const juce::String& message)
{
    FitProgress done;
    done.finished = true;
    done.message = message;
    job.reset();
    return done;
}

AlignmentController::FitProgress AlignmentController::pollFit()
{
    if (job == nullptr)
        return {};
    FitProgress status;
    status.running = true;
    while (advanceJob(status)) {}
    if (status.running)
        return status;
    if (job->cancel->load())
        return finishJob(u8("맞추기를 취소했습니다"));
    if (job->targets.size() == 1)
        return finishJob(job->lastMessage);
    auto message = u8("클립 ") + juce::String(job->applied) + u8("개 맞춤 완료");
    if (job->skipped > 0)
        message << u8(" · ") << job->skipped << u8("개 건너뜀: ") << job->lastMessage;
    return finishJob(message);
}

// 한 단계 진행. 바로 다음 클립으로 넘어갈 수 있으면 true, 기다려야 하면 false (status에 진행 상태)
bool AlignmentController::advanceJob(FitProgress& status)
{
    auto& j = *job;
    const auto count = static_cast<double>(juce::jmax<size_t>(1, j.targets.size()));
    const auto at = [&](double within) { return (static_cast<double>(j.index) + within) / count; };
    const auto next = [&](const Outcome& o)
    {
        (o.ok ? j.applied : j.skipped) += 1;
        if (! o.ok || j.lastMessage.isEmpty() || j.targets.size() == 1)
            j.lastMessage = o.message;
        ++j.index;
        j.dtwProgress->store(0.0);
        return true;
    };
    if (j.index >= j.targets.size() || (j.cancel->load() && ! j.pending.valid()))
    {
        status.running = false;
        return false;
    }

    const auto* target = j.targets[j.index];
    if (j.pending.valid())
    {
        if (j.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            status.stage = u8(j.cancel->load() ? "취소 중..." : "타이밍 정렬 중");
            status.progress = at(0.4 + 0.5 * j.dtwProgress->load());
            return false;
        }
        auto result = j.pending.get();
        if (result.cancelled || j.cancel->load())
        {
            status.running = false;
            return false;
        }
        if (! isLive(target) || ! isLive(j.pendingFor.reference))
            return next({ false, u8("클립이 사라졌습니다") });
        storeMap(target, j.pendingFor, j.pendingAnchors, std::move(result));
        status.stage = u8("적용 중");
        return next(apply(target, j.pendingFor, true, true));
    }

    if (! isLive(target))
        return next({ false, u8("클립이 사라졌습니다") });
    const auto p = prepare(target);
    if (! p.ok)
        return next({ false, p.message });
    if (! p.ready)
    {
        if (! analysis.isAlignmentActive())
            return next({ false, u8("레퍼런스 정렬이 꺼져 있습니다") });
        const auto [done, total] = analysis.getAlignmentProgress();
        status.stage = u8("발음·호흡 분석 대기");
        status.progress = at(0.3 * (total > 0 ? static_cast<double>(done) / total : 0.0));
        return false;
    }
    if (cachedMap(target, p) != nullptr)
        return next(apply(target, p, true, true));

    auto plan = planFor(target, p);
    j.pendingFor = p;
    j.pendingAnchors = plan.anchorCount;
    plan.options.progress = [progress = j.dtwProgress, cancel = j.cancel](double fraction)
    {
        progress->store(fraction);
        return ! cancel->load();
    };
    j.pending = std::async(std::launch::async, [plan = std::move(plan)]() mutable { return runPlan(plan); });
    status.stage = u8("타이밍 정렬 중");
    status.progress = at(0.4);
    return false;
}
