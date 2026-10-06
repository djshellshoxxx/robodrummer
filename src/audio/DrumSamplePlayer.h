#pragma once
#include "core/DrumEvent.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace robodrummer {

class DrumSamplePlayer {
public:
    static constexpr std::size_t InstrumentCount = 16;
    static constexpr std::size_t MaxVoices = 64;
    static constexpr std::size_t MaxVelocityLayers = 4;
    static constexpr std::size_t MaxRoundRobinVariants = 4;

    struct Sample {
        std::vector<float> left;
        std::vector<float> right;
    };

    void setSample(DrumInstrument instrument, Sample sample) {
        auto& set = instruments_[index(instrument)];
        set.layers = {};
        set.roundRobinCounters.fill(0);
        set.layers[0].configured = true;
        set.layers[0].minVelocity = 0.0f;
        set.layers[0].maxVelocity = 1.0f;
        set.layers[0].variants[0] = std::move(sample);
        set.layers[0].variantCount = 1;
    }

    void setVelocityLayer(DrumInstrument instrument,
                          std::size_t layerIndex,
                          float minVelocity,
                          float maxVelocity,
                          Sample sample) {
        if (layerIndex >= MaxVelocityLayers)
            return;

        auto& set = instruments_[index(instrument)];
        auto& layer = set.layers[layerIndex];
        layer = {};
        layer.configured = true;
        layer.minVelocity = std::clamp(minVelocity, 0.0f, 1.0f);
        layer.maxVelocity = std::clamp(maxVelocity, layer.minVelocity, 1.0f);
        layer.variants[0] = std::move(sample);
        layer.variantCount = 1;
        set.roundRobinCounters[layerIndex] = 0;
    }

    [[nodiscard]] bool addRoundRobinSample(DrumInstrument instrument,
                                           std::size_t layerIndex,
                                           Sample sample) {
        if (layerIndex >= MaxVelocityLayers)
            return false;

        auto& layer = instruments_[index(instrument)].layers[layerIndex];
        if (!layer.configured || layer.variantCount >= MaxRoundRobinVariants)
            return false;

        layer.variants[layer.variantCount++] = std::move(sample);
        return true;
    }

    void setChokeGroup(DrumInstrument instrument, int group) noexcept {
        instruments_[index(instrument)].chokeGroup = std::max(0, group);
    }

    void setInstrumentGainPan(DrumInstrument instrument, float gain, float pan) noexcept {
        auto& set = instruments_[index(instrument)];
        set.gain = std::clamp(gain, 0.0f, 4.0f);
        set.pan = std::clamp(pan, -1.0f, 1.0f);
    }

    void clear() noexcept {
        for (auto& voice : voices_)
            voice.active = false;
    }

    void trigger(const DrumEvent& event) noexcept {
        const auto instrumentIndex = index(event.instrument);
        if (instrumentIndex >= InstrumentCount)
            return;

        auto& instrument = instruments_[instrumentIndex];
        const float velocity = std::clamp(event.velocity, 0.0f, 1.0f);
        const auto layerIndex = findLayer(instrument, velocity);
        if (layerIndex >= MaxVelocityLayers)
            return;

        auto& layer = instrument.layers[layerIndex];
        if (layer.variantCount == 0)
            return;

        const std::size_t variantIndex =
            instrument.roundRobinCounters[layerIndex]++ % layer.variantCount;
        const auto& sample = layer.variants[variantIndex];
        if (sample.left.empty())
            return;

        if (instrument.chokeGroup > 0) {
            for (auto& voice : voices_) {
                if (voice.active && voice.chokeGroup == instrument.chokeGroup)
                    voice.active = false;
            }
        }

        for (auto& voice : voices_) {
            if (!voice.active) {
                const float leftPan = instrument.pan > 0.0f ? 1.0f - instrument.pan : 1.0f;
                const float rightPan = instrument.pan < 0.0f ? 1.0f + instrument.pan : 1.0f;
                voice.active = true;
                voice.sample = &sample;
                voice.position = 0;
                voice.leftGain = velocity * instrument.gain * leftPan;
                voice.rightGain = velocity * instrument.gain * rightPan;
                voice.chokeGroup = instrument.chokeGroup;
                return;
            }
        }
    }

    void render(float** outputs, int channels, int numSamples) noexcept {
        if (!outputs || !outputs[0] || channels <= 0 || numSamples <= 0)
            return;

        for (auto& voice : voices_) {
            if (!voice.active || voice.sample == nullptr)
                continue;

            const auto& sample = *voice.sample;
            for (int n = 0; n < numSamples && voice.position < sample.left.size(); ++n, ++voice.position) {
                const float left = sample.left[voice.position] * voice.leftGain;
                const float rightSource =
                    voice.position < sample.right.size() ? sample.right[voice.position] : sample.left[voice.position];

                outputs[0][n] += left;
                if (channels > 1 && outputs[1])
                    outputs[1][n] += rightSource * voice.rightGain;
            }

            if (voice.position >= sample.left.size())
                voice.active = false;
        }
    }

private:
    struct Layer {
        bool configured{false};
        float minVelocity{0.0f};
        float maxVelocity{1.0f};
        std::array<Sample, MaxRoundRobinVariants> variants{};
        std::size_t variantCount{0};
    };

    struct InstrumentSet {
        std::array<Layer, MaxVelocityLayers> layers{};
        std::array<std::size_t, MaxVelocityLayers> roundRobinCounters{};
        int chokeGroup{0};
        float gain{1.0f};
        float pan{0.0f};
    };

    struct Voice {
        bool active{false};
        const Sample* sample{nullptr};
        std::size_t position{0};
        float leftGain{1.0f};
        float rightGain{1.0f};
        int chokeGroup{0};
    };

    [[nodiscard]] static constexpr std::size_t index(DrumInstrument instrument) noexcept {
        return static_cast<std::size_t>(instrument);
    }

    [[nodiscard]] static std::size_t findLayer(const InstrumentSet& instrument, float velocity) noexcept {
        std::size_t firstConfigured = MaxVelocityLayers;
        for (std::size_t i = 0; i < MaxVelocityLayers; ++i) {
            const auto& layer = instrument.layers[i];
            if (!layer.configured)
                continue;
            if (firstConfigured == MaxVelocityLayers)
                firstConfigured = i;
            if (velocity >= layer.minVelocity && velocity <= layer.maxVelocity)
                return i;
        }
        return firstConfigured;
    }

    std::array<InstrumentSet, InstrumentCount> instruments_{};
    std::array<Voice, MaxVoices> voices_{};
};

} // namespace robodrummer
