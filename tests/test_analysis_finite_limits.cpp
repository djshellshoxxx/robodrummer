#include "analysis/InputConditioner.h"
#include "analysis/PerformanceAnalyzer.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>

namespace {
void checkFinite(const robodrummer::PerformanceState& state) {
    const float values[] {state.rms, state.peak, state.activity, state.intensity,
        state.bandLow, state.bandMid, state.bandHigh, state.spectralFlux,
        state.transientDensity, state.transientStrength, state.regularity,
        state.accentProbability, state.silenceProbability};
    for (const float value : values) assert(std::isfinite(value));
    assert(state.intensity >= 0.0f && state.intensity <= 1.0f);
}

void checkConditionedChain(double rate, float amplitude, float gain, int stride) {
    robodrummer::InputConditioner conditioner;
    robodrummer::PerformanceAnalyzer analyzer;
    conditioner.prepare(rate);
    conditioner.setGainDb(gain);
    analyzer.prepare(rate);
    std::array<float, 512> input{}, output{};
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = (static_cast<int>(i) / stride % 2 == 0 ? -1.0f : 1.0f) * amplitude;
    const auto original = input;
    for (int repeat = 0; repeat < 3; ++repeat) {
        conditioner.process(input.data(), nullptr, output.data(), static_cast<int>(output.size()));
        for (const float sample : output) assert(std::isfinite(sample));
        analyzer.processBlock(output.data(), static_cast<int>(output.size()));
    }
    assert(input == original); // Analysis protection must never modify the monitored guitar input.
    input.fill(0.0f);
    for (int repeat = 0; repeat < 100; ++repeat) {
        conditioner.process(input.data(), nullptr, output.data(), static_cast<int>(output.size()));
        analyzer.processBlock(output.data(), static_cast<int>(output.size()));
    }
    checkFinite(analyzer.state()); // Original finite-input bug persisted after these silent blocks.
    for (const float sample : output) assert(std::isfinite(sample));
    assert(std::isfinite(conditioner.lastBlockRms()));
    assert(std::isfinite(conditioner.lastBlockPeak()));

    // A normal signal after the fault must remain usable without resetting either component.
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.2f * std::sin(static_cast<float>(i) * 0.13f);
    conditioner.setGainDb(0.0f);
    for (int repeat = 0; repeat < 100; ++repeat) {
        conditioner.process(input.data(), nullptr, output.data(), static_cast<int>(output.size()));
        analyzer.processBlock(output.data(), static_cast<int>(output.size()));
        checkFinite(analyzer.state());
        for (const float sample : output) assert(std::isfinite(sample));
    }
    assert(conditioner.lastBlockRms() > 0.05f && conditioner.lastBlockRms() < 0.3f);
}
}

int main() {
    // Reproduces A10 using the real conditioner -> analyzer chain.
    checkConditionedChain(48000.0, 1.0e38f, 6.0f, 4);
    for (const double rate : {11025.0, 44100.0, 48000.0, 96000.0, 192000.0})
        for (const int stride : {1, 16, 64})
            checkConditionedChain(rate, std::numeric_limits<float>::max(), 36.0f, stride);

    // The analyzer is also a public standalone core component.
    robodrummer::PerformanceAnalyzer analyzer;
    analyzer.prepare(48000.0);
    std::array<float, 512> extreme{};
    for (std::size_t i = 0; i < extreme.size(); ++i)
        extreme[i] = i % 2 == 0 ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max();
    for (int repeat = 0; repeat < 3; ++repeat) analyzer.processBlock(extreme.data(), static_cast<int>(extreme.size()));
    extreme.fill(0.0f);
    for (int repeat = 0; repeat < 100; ++repeat) analyzer.processBlock(extreme.data(), static_cast<int>(extreme.size()));
    checkFinite(analyzer.state());

    extreme.fill(std::numeric_limits<float>::infinity());
    analyzer.processBlock(extreme.data(), static_cast<int>(extreme.size()));
    extreme.fill(std::numeric_limits<float>::quiet_NaN());
    analyzer.processBlock(extreme.data(), static_cast<int>(extreme.size()));
    checkFinite(analyzer.state());
}
