#pragma once
#include <vector>
#include "EditGrid.h"
#include "EditRenderer.h"
#include "VocalFeatures.h"

// 렌더 구간(프레임)마다 합성기에 넘길 곡선 (이동량, 포먼트 양). EditRenderer.cpp 내부용.
struct ChunkSpan { size_t coreBegin, coreEnd, begin, end; };   // 프레임 단위

ChunkSpan spanToQuiet(const VocalFeatures&, const EditGrid&, const ChunkSpan&);
std::vector<double> psolaShiftCurve(const VocalFeatures&, const EditGrid&, const ChunkSpan&, double tolerance);
std::vector<double> psolaFormantCurve(const VocalFeatures&, const EditGrid&, const ChunkSpan&);
std::vector<double> warpShiftCurve(const AudioPlanes&, const VocalFeatures&, const EditGrid&, const ChunkSpan&, double tolerance);
