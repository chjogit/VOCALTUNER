#pragma once
#include <atomic>
#include <cstdint>

// 블록 미리 듣기 요청 (메시지 스레드가 쓰고 오디오 스레드가 읽는다). 호스트가 멈춰 있을 때만 재생된다.
// 값을 쓴 뒤 세대 번호를 올리고, 읽는 쪽은 세대가 앞뒤로 같을 때만 값을 믿는다.
class PreviewRequest
{
public:
    struct Snapshot
    {
        const void* source = nullptr;   // nullptr = 멈춤
        std::int64_t begin = 0, end = 0; // 소스 샘플 [begin, end)
        std::uint32_t generation = 0;
    };

    void start(const void* source, std::int64_t begin, std::int64_t end) noexcept
    {
        writing.store(true, std::memory_order_release);
        beginSample.store(begin, std::memory_order_relaxed);
        endSample.store(end, std::memory_order_relaxed);
        sourceId.store(source, std::memory_order_relaxed);
        generation.fetch_add(1, std::memory_order_release);
        writing.store(false, std::memory_order_release);
    }

    void stop() noexcept { start(nullptr, 0, 0); }

    // 쓰는 중이면 false (다음 블록에서 다시 읽는다)
    bool read(Snapshot& out) const noexcept
    {
        const auto before = generation.load(std::memory_order_acquire);
        if (writing.load(std::memory_order_acquire))
            return false;
        out.source = sourceId.load(std::memory_order_relaxed);
        out.begin = beginSample.load(std::memory_order_relaxed);
        out.end = endSample.load(std::memory_order_relaxed);
        out.generation = before;
        return generation.load(std::memory_order_acquire) == before && ! writing.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> writing { false };
    std::atomic<const void*> sourceId { nullptr };
    std::atomic<std::int64_t> beginSample { 0 }, endSample { 0 };
    std::atomic<std::uint32_t> generation { 0 };
};
