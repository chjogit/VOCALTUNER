#include "../Source/UI/RollViewport.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

RollViewport makeViewport()
{
    RollViewport v;
    v.width = 800.0;
    v.height = 400.0;
    return v;
}

void testRoundTrip()
{
    auto v = makeViewport();
    v.startSec = 12.5;
    v.secondsPerPixel = 0.02;
    check(near(v.xToTime(v.timeToX(20.0)), 20.0), "time <-> x round trip");
    check(near(v.yToMidi(v.midiToY(61.3)), 61.3), "midi <-> y round trip");
    check(v.midiToY(70.0) < v.midiToY(60.0), "higher notes are drawn higher (smaller y)");
}

void testZoomKeepsAnchor()
{
    auto v = makeViewport();
    v.startSec = 3.0;
    v.secondsPerPixel = 0.01;
    const auto anchorX = 250.0;
    const auto before = v.xToTime(anchorX);
    v.zoomTimeAt(anchorX, 2.0);
    check(near(v.xToTime(anchorX), before), "zoom keeps the time under the cursor fixed");
    check(near(v.secondsPerPixel, 0.005), "zoom factor 2 halves seconds per pixel");
}

void testZoomLimits()
{
    auto v = makeViewport();
    for (int i = 0; i < 100; ++i)
        v.zoomTimeAt(100.0, 2.0);
    check(near(v.secondsPerPixel, RollViewport::minSecondsPerPixel), "zoom in is clamped");
    for (int i = 0; i < 100; ++i)
        v.zoomTimeAt(100.0, 0.5);
    check(near(v.secondsPerPixel, RollViewport::maxSecondsPerPixel), "zoom out is clamped");
}

void testPan()
{
    auto v = makeViewport();
    v.startSec = 10.0;
    v.secondsPerPixel = 0.01;
    const auto noteY = v.midiToY(64.0);
    const auto sec = 12.0;
    const auto x = v.timeToX(sec);

    v.panByPixels(50.0, 30.0);   // 내용을 오른쪽/아래로 끎
    check(near(v.timeToX(sec), x + 50.0), "dragging right moves content right");
    check(near(v.midiToY(64.0), noteY + 30.0), "dragging down moves content down");
}

void testFit()
{
    auto v = makeViewport();
    v.fitTime(5.0, 15.0);
    check(v.timeToX(5.0) > 0.0 && v.timeToX(15.0) < v.width, "fitTime shows the whole range with margin");
    check(v.timeToX(15.0) - v.timeToX(5.0) > 0.9 * v.width, "fitTime uses most of the width");

    v.fitPitch(60.0, 62.0);
    check(near(v.topMidi - v.bottomMidi, 12.0), "fitPitch guarantees at least an octave");
    check(v.yToMidi(0.0) > 62.0 && v.yToMidi(v.height) < 60.0, "fitPitch keeps the range inside the view");

    v.fitPitch(40.0, 80.0);
    check(near(v.topMidi - v.bottomMidi, 44.0), "fitPitch adds 4 semitones of margin to wide ranges");
}

void testDegenerate()
{
    auto v = makeViewport();
    v.fitTime(3.0, 3.0);
    check(std::isfinite(v.secondsPerPixel) && v.secondsPerPixel >= RollViewport::minSecondsPerPixel,
          "fitTime handles an empty range");
}
} // namespace

int main()
{
    {
        auto v = makeViewport();
        const auto anchorY = 100.0;
        const auto before = v.yToMidi(anchorY);
        v.zoomPitchAt(anchorY, 2.0);
        check(near(v.yToMidi(anchorY), before), "zoomPitchAt keeps the anchor note fixed");
        check(near(v.topMidi - v.bottomMidi, 12.0), "zoomPitchAt halves the span at factor 2");
        for (int i = 0; i < 20; ++i)
            v.zoomPitchAt(100.0, 2.0);
        check(near(v.topMidi - v.bottomMidi, RollViewport::minSemitoneSpan), "zoomPitchAt clamps to the minimum span");
        for (int i = 0; i < 20; ++i)
            v.zoomPitchAt(100.0, 0.5);
        check(near(v.topMidi - v.bottomMidi, RollViewport::maxSemitoneSpan), "zoomPitchAt clamps to the maximum span");
    }

    testRoundTrip();
    testZoomKeepsAnchor();
    testZoomLimits();
    testPan();
    testFit();
    testDegenerate();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
