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
    for (int i = 0; i < 40; ++i) analyzer.processBlock(loud.data(), static_cast<int>(loud.size()));
    const auto loudState = analyzer.state();

    assert(loudState.rms > quietState.rms);
    assert(loudState.activity > quietState.activity);
    assert(loudState.intensity > quietState.intensity + 0.20f);
    assert(loudState.intensity <= 1.0f);

    // Required live-performance features are finite, normalized and responsive.
    assert(loudState.spectralFlux >= 0.0f && loudState.spectralFlux <= 1.0f);
    assert(loudState.lowBandEnergy >= 0.0f);
    assert(loudState.midBandEnergy >= 0.0f);
    assert(loudState.highBandEnergy >= 0.0f);
    assert(loudState.transientDensity >= 0.0f && loudState.transientDensity <= 1.0f);
    assert(loudState.rhythmicRegularity >= 0.0f && loudState.rhythmicRegularity <= 1.0f);
    assert(loudState.silenceProbability >= 0.0f && loudState.silenceProbability <= 1.0f);
    assert(loudState.accentProbability >= 0.0f && loudState.accentProbability <= 1.0f);
    assert(loudState.activityConfidence >= 0.0f && loudState.activityConfidence <= 1.0f);
    assert(loudState.phraseBoundaryProbability >= 0.0f && loudState.phraseBoundaryProbability <= 1.0f);
    assert(loudState.buildReleaseTendency >= -1.0f && loudState.buildReleaseTendency <= 1.0f);

    assert(loudState.silenceProbability < quietState.silenceProbability);
    assert(loudState.transientDensity > quietState.transientDensity);
    assert(loudState.accentProbability > quietState.accentProbability);
    assert(loudState.rhythmicRegularity > 0.35f);

    // Silence converges toward high silence probability without NaN/Inf state.
    std::array<float, 480> silence{};
    for (int i = 0; i < 80; ++i) analyzer.processBlock(silence.data(), static_cast<int>(silence.size()));
    const auto silentState = analyzer.state();
    assert(silentState.silenceProbability > 0.75f);
    assert(std::isfinite(silentState.intensity));
    assert(std::isfinite(silentState.spectralFlux));
}
