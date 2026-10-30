// Python 프로토타입과 C++ 블록 분할 비교용. 빌드: g++ -std=c++17 -O2 -ISource/Analysis split_cli.cpp Source/Analysis/BlockParser.cpp -o split_cli
// 실행: python3 compare.py ./split_cli  (numpy, scipy 필요)
#include "BlockParser.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
int main() {
  std::string line; VocalFeatures f; f.framePeriodMs = 5.0;
  auto flush = [&] { if (f.midiF0.empty()) return;
    auto blocks = parseBlocks(f);
    for (auto& b : blocks) if (!b.isConsonant) std::printf("%zu %zu ", b.beginFrame, b.endFrame);
    std::printf("\n"); f = VocalFeatures(); f.framePeriodMs = 5.0; };
  while (std::getline(std::cin, line)) { if (line.empty()) { flush(); continue; }
    f.midiF0.push_back(std::stod(line)); f.rms.push_back(0.5); f.timeSec.push_back(0.005 * (double)f.timeSec.size()); }
  flush(); }
