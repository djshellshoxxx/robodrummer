#include "analysis/InputConditioner.h"
#include <array>
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;

    constexpr int n = 64;
    std::array<float, n> left{};
    std::array<float, n> right{};
    std::array<float, n> out{};
    for (int i = 0; i < n; ++i) {
        left[static_cast<std::size_t>(i)] = 0.25f + static_cast<float>(i) * 0.001f;
        right[static_cast<std::size_t>(i)] = -0.10f + static_cast<float>(i) * 0.002f;
    }

    InputConditioner conditioner;
    conditioner.prepare(48000.0);

    InputConditionerSettings settings;
    settings.highPassHz = 0.0f;
    settings.gateThreshold = 0.0f;
    settings.gain = 1.0f;

    settings.mode = AnalysisInputMode::LeftOnly;
    conditioner.reset();
    conditioner.process(left.data(), right.data(), out.data(), n, settings);
    for (int i = 0; i < n; ++i)
        assert(std::abs(out[static_cast<std::size_t>(i)] - left[static_cast<std::size_t>(i)]) < 0.000001f);

    settings.mode = AnalysisInputMode::RightOnly;
    conditioner.reset();
    conditioner.process(left.data(), right.data(), out.data(), n, settings);
    for (int i = 0; i < n; ++i)
        assert(std::abs(out[static_cast<std::size_t>(i)] - right[static_cast<std::size_t>(i)]) < 0.000001f);

    settings.mode = AnalysisInputMode::StereoSum;
    conditioner.reset();
    conditioner.process(left.data(), right.data(), out.data(), n, settings);
    for (int i = 0; i < n; ++i) {
        const float expected = 0.5f * (left[static_cast<std::size_t>(i)] + right[static_cast<std::size_t>(i)]);
        assert(std::abs(out[static_cast<std::size_t>(i)] - expected) < 0.000001f);
    }

    // Mono accepts a single-channel source and never dereferences a missing right channel.
    settings.mode = AnalysisInputMode::Mono;
    conditioner.reset();
    conditioner.process(left.data(), nullptr, out.data(), n, settings);
    assert(std::abs(out[12] - left[12]) < 0.000001f);

    // Gain and gate are bounded preprocessing operations.
    settings.gain = 2.0f;
    settings.gateThreshold = 0.20f;
    conditioner.reset();
    std::array<float, n> gateIn{};
    gateIn[20] = 0.30f;
    conditioner.process(gateIn.data(), nullptr, out.data(), n, settings);
    assert(std::abs(out[0]) < 0.000001f);
    assert(out[20] > 0.55f);

    // DC/high-pass stage removes a constant signal over time.
    settings.gain = 1.0f;
    settings.gateThreshold = 0.0f;
    settings.highPassHz = 80.0f;
    std::array<float, 480> dc{};
    std::array<float, 480> filtered{};
    dc.fill(0.5f);
    conditioner.reset();
    for (int pass = 0; pass < 20; ++pass)
        conditioner.process(dc.data(), nullptr, filtered.data(), static_cast<int>(dc.size()), settings);
    assert(std::abs(filtered.back()) < 0.01f);
}
