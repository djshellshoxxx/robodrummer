#include "analysis/OnsetDetector.h"
#include <array>
#include <cassert>
#include <cmath>
#include <vector>

int main() {
    using namespace robodrummer;
    constexpr double sr = 1000.0;
    AdaptiveOnsetDetector detector;
    detector.prepare(sr);

    std::vector<float> audio(2400, 0.0f);
    for (int center : {200, 700, 1200, 1700, 2200}) {
        for (int i = 0; i < 20; ++i) {
            const int idx = center + i;
            if (idx < static_cast<int>(audio.size()))
                audio[static_cast<std::size_t>(idx)] = 0.9f * static_cast<float>(std::exp(-0.18 * i));
        }
    }

    std::array<OnsetEvent, 16> events{};
    const auto count = detector.processBlock(audio.data(), static_cast<int>(audio.size()), events.data(), events.size());
    assert(count >= 4 && count <= 6);
    assert(detector.frameSize() >= 32);

    int matched = 0;
    for (std::size_t i = 0; i < count; ++i) {
        for (int target : {200, 700, 1200, 1700, 2200}) {
            if (std::abs(events[i].sampleOffset - target) <= detector.frameSize() + 20) {
                ++matched;
                break;
            }
        }
    }
    assert(matched >= 4);
}
