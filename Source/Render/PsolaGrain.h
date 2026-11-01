#pragma once
#include <utility>
#include <vector>
#include "Psola.h"

// TD-PSOLA 구현이 같이 쓰는 조각들 (Psola.cpp, PsolaWarp.cpp)
namespace psola
{
constexpr double lockBelow = 0.15;          // 이동이 이보다 작으면 원본 마크 쪽으로 위상을 끌어당긴다
constexpr size_t activeDilateFrames = 8;    // 합성 구간을 앞뒤로 40ms 넓힌다 (마크가 자리 잡을 여유)
constexpr double maxExpandSec = 20.0;       // 무성 틈을 찾아 넓히는 최대 길이
constexpr double pi = 3.14159265358979323846;

double hzOf(double midi) noexcept;
size_t frameOf(const PsolaInput&, double sample) noexcept;
double hann(long i, long n) noexcept;

struct Synth { std::vector<double> out, weight; size_t offset = 0; };
// samples[mark - half, mark + half)을 Hann 창으로 centre 둘레에 더한다
void addGrain(const PsolaInput&, Synth&, long mark, long centre, long half);
long nearestMark(const std::vector<long>& marks, double t) noexcept;
// [begin, end)을 앞뒤 무성 틈(합성 구간 넓힘보다 긴)까지 넓힌다. 어느 구간을 렌더하든 마크와 합성이 같아야
// 구간을 이어 붙이는 곳(겹쳐 섞는 곳)에서 두 결과의 위상이 어긋나지 않는다
std::pair<size_t, size_t> expandedToQuiet(const PsolaInput&, size_t beginSample, size_t endSample);
} // namespace psola
