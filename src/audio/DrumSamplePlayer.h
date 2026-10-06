#pragma once
#include "core/DrumEvent.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
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

    void setInstrumentTuning(DrumInstrument instrument, float semitones) noexcept {
        instruments_[index(instrument)].tuningSemitones = std::clamp(semitones, -24.0f, 24.0f);
    }

    void setInstrumentEnvelope(DrumInstrument instrument, int attackSamples, int releaseSamples) noexcept {
        auto& set = instruments_[index(instrument)];
        set.attackSamples = std::max(0, attackSamples);
        set.releaseSamples = std::max(0, releaseSamples);
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
                voice.position = 0.0;
                voice.playbackRate = std::pow(2.0, static_cast<double>(instrument.tuningSemitones) / 12.0);
                voice.leftGain = velocity * instrument.gain * leftPan;
                voice.rightGain = velocity * instrument.gain * rightPan;
                voice.attackSamples = instrument.attackSamples;
                voice.releaseSamples = instrument.releaseSamples;
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
            const double sampleLength = static_cast<double>(sample.left.size());
            for (int n = 0; n < numSamples && voice.position < sampleLength; ++n) {
                const float envelope = envelopeGain(voice, sampleLength);
                const float left = interpolate(sample.left, voice.position) * voice.leftGain * envelope;
                const float rightSource = sample.right.empty()
                    ? interpolate(sample.left, voice.position)
                    : interpolate(sample.right, voice.position);

                outputs[0][n] += left;
                if (channels > 1 && outputs[1])
                    outputs[1][n] += rightSource * voice.rightGain * envelope;

                voice.position += voice.playbackRate;
            }

            if (voice.position >= sampleLength)
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
        float tuningSemitones{0.0f};
        int attackSamples{0};
        int releaseSamples{0};
    };

    struct Voice {
        bool active{false};
        const Sample* sample{nullptr};
        double position{0.0};
        double playbackRate{1.0};
        float leftGain{1.0f};
        float rightGain{1.0f};
        int attackSamples{0};
        int releaseSamples{0};
        int chokeGroup{0};
    };

    [[nodiscard]] static constexpr std::size_t index(DrumInstrument instrument) noexcept {
        return static_cast<std::size_t>(instrument);
    }

    [[nodiscard]] static float interpolate(const std::vector<float>& data, double position) noexcept {
        if (data.empty())
            return 0.0f;
        const auto i0 = static_cast<std::size_t>(position);
        if (i0 >= data.size())
            return 0.0f;
        const auto i1 = std::min(i0 + 1, data.size() - 1);
        const float fraction = static_cast<float>(position - static_cast<double>(i0));
        return data[i0] + (data[i1] - data[i0]) * fraction;
    }

    [[nodiscard]] static float envelopeGain(const Voice& voice, double sampleLength) noexcept {
        float gain = 1.0f;
        if (voice.attackSamples > 0) {
            gain = std::min(gain, static_cast<float>((voice.position + 1.0) / static_cast<double>(voice.attackSamples)));
        }
        if (voice.releaseSamples > 0) {
            const double remaining = std::max(0.0, sampleLength - voice.position);
            gain = std::min(gain, static_cast<float>(remaining / static_cast<double>(voice.releaseSamples)));
        }
        return std::clamp(gain, 0.0f, 1.0f);
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
