#include "../Source/Analysis/PitchFlow.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", message);
    failures += condition ? 0 : 1;
}

VocalFeatures features(const std::vector<double>& midi)
{
    VocalFeatures f;
    for (size_t i = 0; i < midi.size(); ++i)
    {
        f.timeSec.push_back(static_cast<double>(i) * 0.005);
        f.midiF0.push_back(midi[i]);
        f.rms.push_back(midi[i] > 0.0 ? 0.5 : 0.0);
    }
    return f;
}

VocalBlock block(size_t a, size_t b, double shift)
{
    VocalBlock v;
    v.beginFrame = a; v.endFrame = b; v.startSec = a * 0.005; v.endSec = b * 0.005; v.pitchShift = shift;
    return v;
}

// 무음 -> 3반음 아래에서 6프레임 동안 올라오는 스쿱 -> 60 유지 -> 6프레임 떨구는 꼬리 -> 무음
void testScoopAndTail()
{
    std::vector<double> m(20, 0.0);
    for (int i = 0; i < 6; ++i) m.push_back(57.0 + 0.5 * i);
    for (int i = 0; i < 60; ++i) m.push_back(60.0 + 0.1 * std::sin(i * 0.6));   // 작은 비브라토
    for (int i = 0; i < 6; ++i) m.push_back(60.0 - 0.6 * (i + 1));
    m.resize(m.size() + 20, 0.0);
    const auto f = features(m);
    const std::vector<VocalBlock> blocks { block(20, 92, 1.0) };
    check(flowPitchShift(f, blocks, 0, 20) < 0.01, "스쿱 시작은 이동 0");
    check(flowPitchShift(f, blocks, 0, 23) > 0.2 && flowPitchShift(f, blocks, 0, 23) < 0.8, "스쿱 중간은 움직인 만큼");
    bool landing = true;
    for (size_t k = 26; k < 86; ++k) landing = landing && std::abs(flowPitchShift(f, blocks, 0, k) - 1.0) < 1e-9;
    check(landing, "안착부(비브라토 포함)는 이동량 그대로");
    check(flowPitchShift(f, blocks, 0, 91) < 0.01, "떨구는 꼬리 끝은 이동 0");
}

// 이웃 두 음: 60 유지 -> 10프레임 글라이드 -> 62 유지. 이동량은 글라이드 안에서만 바뀐다
void testJointFollowsGlide()
{
    std::vector<double> m;
    for (int i = 0; i < 50; ++i) m.push_back(60.0);
    for (int i = 0; i < 10; ++i) m.push_back(60.0 + 0.2 * (i + 1));
    for (int i = 0; i < 50; ++i) m.push_back(62.0);
    const auto f = features(m);
    auto left = block(0, 55, 0.0), right = block(55, 110, 1.0);
    left.transitionAfter = 1.0;   // 이음 폭을 넉넉히 (블롭 길이의 절반까지)
    const std::vector<VocalBlock> blocks { left, right };
    check(std::abs(flowPitchShift(f, blocks, 0, 40)) < 1e-9, "글라이드 전 머무는 곳은 왼쪽 이동량");
    check(std::abs(flowPitchShift(f, blocks, 1, 75) - 1.0) < 1e-9, "글라이드 뒤 머무는 곳은 오른쪽 이동량");
    const auto mid = flowPitchShift(f, blocks, 1, 55);
    check(mid > 0.3 && mid < 0.7, "글라이드 가운데는 절반쯤");
    bool monotone = true;
    for (size_t k = 31; k < 80; ++k)
        monotone = monotone && flowPitchShift(f, blocks, k < 55 ? 0 : 1, k) >= flowPitchShift(f, blocks, k - 1 < 55 ? 0 : 1, k - 1) - 1e-9;
    check(monotone, "이동량이 되돌아가지 않는다");
}

// 거의 안 움직이는 두 음(미세한 떨림 한 번뿐): 이동량이 떨림 한 번에 계단으로 바뀌면 안 된다
void testJointOnSteadyNotes()
{
    std::vector<double> m;
    for (int i = 0; i < 110; ++i) m.push_back(i < 56 ? 60.0 : 60.03);     // 이음 한가운데서 0.03반음 한 번 움직일 뿐
    const auto f = features(m);
    auto left = block(0, 55, 0.0), right = block(55, 110, 1.0);
    left.transitionAfter = 1.0;
    const std::vector<VocalBlock> blocks { left, right };
    double worst = 0.0, previous = flowPitchShift(f, blocks, 0, 0);
    for (size_t k = 1; k < 110; ++k)
    {
        const auto v = flowPitchShift(f, blocks, k < 55 ? 0 : 1, k);
        worst = std::max(worst, std::abs(v - previous));
        previous = v;
    }
    std::printf("  프레임 간 최대 변화 %.3f\n", worst);
    check(worst < 0.2, "안 움직이는 두 음 사이 이음은 계단이 아니라 부드럽게 바뀐다");
    check(std::abs(flowPitchShift(f, blocks, 0, 20)) < 1e-9 && std::abs(flowPitchShift(f, blocks, 1, 90) - 1.0) < 1e-9, "이음 바깥은 각자의 이동량");
}

void testProgress()
{
    const auto p = movementProgress({ 1.0, 1.0, 2.0, 2.0, 3.0 });
    check(p[0] == 0.0 && p[1] == 0.0 && std::abs(p[2] - 0.5) < 1e-9 && p[4] == 1.0, "움직임 비율");
    const auto flat = movementProgress({ 5.0, 5.0, 5.0 });
    check(std::abs(flat[1] - 0.5) < 1e-9, "움직임이 없으면 직선");
}
} // namespace

int main()
{
    testProgress();
    testScoopAndTail();
    testJointFollowsGlide();
    testJointOnSteadyNotes();
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
