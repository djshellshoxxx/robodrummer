#pragma once
#include "core/DrumEvent.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <vector>
namespace robodrummer {
class DrumSamplePlayer {
public:
    static constexpr std::size_t InstrumentCount = 16;
    static constexpr std::size_t MaxVoices = 64;
    struct Sample { std::vector<float> left; std::vector<float> right; };
    void setSample(DrumInstrument i, Sample s) { samples[index(i)] = std::move(s); }
    void clear() noexcept { for (auto& v : voices) v.active = false; }
    void trigger(const DrumEvent& e) noexcept {
        const auto idx = index(e.instrument);
        if (samples[idx].left.empty()) return;
        for (auto& v : voices) {
            if (!v.active) { v = {true, idx, 0, std::clamp(e.velocity, 0.0f, 1.0f)}; return; }
        }
    }
    void render(float** outputs, int channels, int numSamples) noexcept {
        if (!outputs || channels <= 0 || numSamples <= 0) return;
        for (auto& v : voices) {
            if (!v.active) continue;
            const auto& s = samples[v.sampleIndex];
            for (int n = 0; n < numSamples && v.position < s.left.size(); ++n, ++v.position) {
                const float l = s.left[v.position] * v.gain;
                const float r = (s.right.empty() ? l : s.right[v.position] * v.gain);
                outputs[0][n] += l;
                if (channels > 1) outputs[1][n] += r;
            }
            if (v.position >= s.left.size()) v.active = false;
        }
    }
private:
    struct Voice { bool active{false}; std::size_t sampleIndex{0}; std::size_t position{0}; float gain{1.0f}; };
    static constexpr std::size_t index(DrumInstrument i) noexcept { return static_cast<std::size_t>(i); }
    std::array<Sample, InstrumentCount> samples{};
    std::array<Voice, MaxVoices> voices{};
};
}
