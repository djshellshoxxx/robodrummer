#include "analysis/PerformanceAnalyzer.h"
#include <array>
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;

    PerformanceAnalyzer analyzer;
    analyzer.prepare(48000.0);

    std::array<float, 480> quiet{};
    for (std::size_t i = 0; i < quiet.size(); ++i)
        quiet[i] = 0.02f * std::sin(static_cast<float>(i) * 0.17f);
    for (int i = 0; i < 20; ++i) analyzer.processBlock(quiet.data(), static_cast<int>(quiet.size()));
    const auto quietState = analyzer.state();

    std::array<float, 480> loud{};
    for (std::size_t i = 0; i < loud.size(); ++i) {
        const float pulse = (i % 120) < 12 ? 0.9f : 0.0f;
        loud[i] = pulse + 0.30f * std::sin(static_cast<float>(i) * 0.23f);
    }
    for (int i = 0; i < 20; ++i) analyzer.processBlock(loud.data(), static_cast<int>(loud.size()));
    const auto loudState = analyzer.state();

    assert(loudState.rms > quietState.rms);
    assert(loudState.activity > quietState.activity);
    assert(loudState.intensity > quietState.intensity + 0.20f);
    assert(loudState.intensity <= 1.0f);
}
