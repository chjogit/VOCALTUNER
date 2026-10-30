#pragma once
#include <algorithm>
#include <cmath>

// 피아노롤의 시간(가로) / 음높이(세로) <-> 픽셀 좌표 변환. JUCE 비의존(단위 테스트용).
struct RollViewport
{
    static constexpr double minSecondsPerPixel = 0.0005;
    static constexpr double maxSecondsPerPixel = 0.5;
    static constexpr double minSemitoneSpan = 6.0;
    static constexpr double maxSemitoneSpan = 96.0;

    double startSec = 0.0;          // 왼쪽 가장자리의 시간
    double secondsPerPixel = 0.01;
    double topMidi = 72.0;          // 위쪽 가장자리의 음 (더 높은 음)
    double bottomMidi = 48.0;
    double width = 1.0;             // 그리는 영역 크기 (픽셀)
    double height = 1.0;

    double timeToX(double sec) const noexcept { return (sec - startSec) / secondsPerPixel; }
    double xToTime(double x) const noexcept { return startSec + x * secondsPerPixel; }
    double endSec() const noexcept { return xToTime(width); }

    double pixelsPerSemitone() const noexcept { return height / (topMidi - bottomMidi); }
    double midiToY(double midi) const noexcept { return (topMidi - midi) * pixelsPerSemitone(); }
    double yToMidi(double y) const noexcept { return topMidi - y / pixelsPerSemitone(); }

    // anchorX 아래의 시간이 움직이지 않도록 확대 (factor > 1 이면 확대)
    void zoomTimeAt(double anchorX, double factor) noexcept
    {
        const auto anchorTime = xToTime(anchorX);
        secondsPerPixel = std::clamp(secondsPerPixel / factor, minSecondsPerPixel, maxSecondsPerPixel);
        startSec = anchorTime - anchorX * secondsPerPixel;
    }

    // anchorY 아래의 음이 움직이지 않도록 세로 확대 (factor > 1 이면 확대)
    void zoomPitchAt(double anchorY, double factor) noexcept
    {
        const auto anchorMidi = yToMidi(anchorY);
        const auto span = std::clamp((topMidi - bottomMidi) / factor, minSemitoneSpan, maxSemitoneSpan);
        const auto ratio = anchorY / std::max(height, 1.0);
        topMidi = anchorMidi + ratio * span;
        bottomMidi = topMidi - span;
    }

    // 내용을 (dx, dy) 픽셀만큼 끌어서 이동
    void panByPixels(double dx, double dy) noexcept
    {
        startSec -= dx * secondsPerPixel;
        const auto semitones = dy / pixelsPerSemitone();
        topMidi += semitones;
        bottomMidi += semitones;
    }

    void fitTime(double fromSec, double toSec) noexcept
    {
        const auto span = std::max(toSec - fromSec, 0.1);
        const auto margin = span * 0.02;
        secondsPerPixel = std::clamp((span + 2.0 * margin) / std::max(width, 1.0), minSecondsPerPixel, maxSecondsPerPixel);
        startSec = fromSec - margin;
    }

    // 최소 12반음 (한 옥타브) 범위를 보장하고 위아래로 여유를 둠
    void fitPitch(double lowMidi, double highMidi) noexcept
    {
        const auto span = std::max(highMidi - lowMidi + 4.0, 12.0);
        const auto centre = 0.5 * (lowMidi + highMidi);
        topMidi = centre + 0.5 * span;
        bottomMidi = centre - 0.5 * span;
    }
};
