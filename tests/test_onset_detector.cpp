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
        assert(events[i].probability >= 0.0f && events[i].probability <= 1.0f);
        assert(events[i].strength >= 0.0f && events[i].strength <= 1.0f);
        for (int target : {200, 700, 1200, 1700, 2200}) {
            if (std::abs(events[i].sampleOffset - target) <= detector.frameSize() + 20) {
                ++matched;
                break;
            }
        }
    }
    assert(matched >= 4);

    // A sustained tone should not be misread as a stream of repeated attacks.
    detector.reset();
    std::vector<float> sustained(3000);
    for (std::size_t i = 0; i < sustained.size(); ++i)
        sustained[i] = 0.35f * std::sin(static_cast<float>(i) * 0.08f);
    const auto sustainedCount = detector.processBlock(
        sustained.data(), static_cast<int>(sustained.size()), events.data(), events.size());
    assert(sustainedCount <= 2);

    // Low-level amp-like hiss remains below the adaptive novelty floor.
    detector.reset();
    std::vector<float> hiss(3000);
    std::uint32_t rng = 0x12345678u;
    for (auto& sample : hiss) {
        rng = rng * 1664525u + 1013904223u;
        const float unit = static_cast<float>((rng >> 8) & 0xffffu) / 65535.0f;
        sample = (unit * 2.0f - 1.0f) * 0.012f;
    }
    const auto hissCount = detector.processBlock(
        hiss.data(), static_cast<int>(hiss.size()), events.data(), events.size());
    assert(hissCount <= 1);
}
