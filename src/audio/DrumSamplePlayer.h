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
    static constexpr std::size_t MaxBuses = 5;
    static constexpr std::size_t MaxVelocityLayers = 4;
    static constexpr std::size_t MaxRoundRobinVariants = 4;
    // Float files may contain arbitrary IEEE values. Keep +24 dBFS headroom
    // while bounding interpolation and the fixed 64-voice/4x-gain mix.
    static constexpr float MaxSampleMagnitude = 16.0f;

    [[nodiscard]] static float sanitiseSampleValue(float value) noexcept {
        return std::isfinite(value) ? std::clamp(value, -MaxSampleMagnitude, MaxSampleMagnitude) : 0.0f;
    }

    struct Sample {
        std::vector<float> left;
        std::vector<float> right;
    };

    // Mixer changes crossfade existing voices over 5 ms of output time.
    // Configure off the render thread when preparing a sampler instance.
    void setSampleRate(double sampleRate) noexcept {
        if (!std::isfinite(sampleRate)) sampleRate = 44100.0;
        mixRampSamples_ = static_cast<int>(std::ceil(std::clamp(sampleRate, 8000.0, 768000.0) * 0.005));
    }

    // Loader-side accounting only; includes mono/stereo storage in every slot.
    [[nodiscard]] std::size_t sampleStorageBytes() const noexcept {
        std::size_t bytes = 0;
        for (const auto& instrument : instruments_)
            for (const auto& layer : instrument.layers)
                for (const auto& sample : layer.variants)
                    bytes += (sample.left.capacity() + sample.right.capacity()) * sizeof(float);
        return bytes;
    }

    void setSample(DrumInstrument instrument, Sample sample) {
        sanitiseSample(sample);
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

        sanitiseSample(sample);

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

        sanitiseSample(sample);
        layer.variants[layer.variantCount++] = std::move(sample);
        return true;
    }

    void setChokeGroup(DrumInstrument instrument, int group) noexcept {
        instruments_[index(instrument)].chokeGroup = std::max(0, group);
    }

    void setInstrumentGainPan(DrumInstrument instrument, float gain, float pan) noexcept {
        if (!std::isfinite(gain)) gain = 1.0f;
        if (!std::isfinite(pan)) pan = 0.0f;
        auto& set = instruments_[index(instrument)];
        gain = std::clamp(gain, 0.0f, 4.0f);
        pan = std::clamp(pan, -1.0f, 1.0f);
        if (set.gain == gain && set.pan == pan) return;
        set.gain = gain;
        set.pan = pan;
        const float leftPan = pan > 0.0f ? 1.0f - pan : 1.0f;
        const float rightPan = pan < 0.0f ? 1.0f + pan : 1.0f;
        for (auto& voice : voices_) {
            if (!voice.active || voice.instrument != instrument) continue;
            voice.targetLeftGain = voice.velocity * gain * leftPan;
            voice.targetRightGain = voice.velocity * gain * rightPan;
            voice.leftGainStep = (voice.targetLeftGain - voice.leftGain) / static_cast<float>(mixRampSamples_);
            voice.rightGainStep = (voice.targetRightGain - voice.rightGain) / static_cast<float>(mixRampSamples_);
            voice.mixSamplesRemaining = mixRampSamples_;
        }
    }

    // Output bus for this instrument's voices (0 = main mix; 1+ = aux outputs that fall back to main when not rendered).
    void setInstrumentBus(DrumInstrument instrument, int bus) noexcept {
        instruments_[index(instrument)].bus = std::clamp(bus, 0, static_cast<int>(MaxBuses) - 1);
    }

    void setInstrumentTuning(DrumInstrument instrument, float semitones) noexcept {
        if (!std::isfinite(semitones)) semitones = 0.0f;
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

        // Use a free voice, otherwise steal the one furthest into its sample (the most decayed hit),
        // so a new hit is never silently dropped when the pool is full.
        Voice* target = nullptr;
        for (auto& voice : voices_) {
            if (!voice.active) { target = &voice; break; }
            if (target == nullptr || voice.position > target->position) target = &voice;
        }
        if (target == nullptr) return;
        auto& voice = *target;
        const float leftPan = instrument.pan > 0.0f ? 1.0f - instrument.pan : 1.0f;
        const float rightPan = instrument.pan < 0.0f ? 1.0f + instrument.pan : 1.0f;
        voice.active = true;
        voice.sample = &sample;
        voice.position = 0.0;
        voice.playbackRate = std::pow(2.0, static_cast<double>(instrument.tuningSemitones) / 12.0);
        voice.leftGain = velocity * instrument.gain * leftPan;
        voice.rightGain = velocity * instrument.gain * rightPan;
        voice.instrument = event.instrument;
        voice.velocity = velocity;
        voice.mixSamplesRemaining = 0;
        voice.attackSamples = instrument.attackSamples;
        voice.releaseSamples = instrument.releaseSamples;
        voice.chokeGroup = instrument.chokeGroup;
        voice.bus = instrument.bus;
    }

    void render(float** outputs, int channels, int numSamples) noexcept {
        if (!outputs || !outputs[0] || channels <= 0 || numSamples <= 0)
            return;
        float* left[1] { outputs[0] };
        float* right[1] { channels > 1 ? outputs[1] : nullptr };
        renderBuses(left, right, 1, numSamples);
    }

    // Renders into per-bus stereo pairs. A null left pointer means that bus is unavailable and its voices
    // fall back to bus 0, so disabled aux outputs never lose audio.
    // sendLeft/sendRight, when given, additionally receive every voice (the feed for a room/reverb return that
    // must hear the whole kit even when instruments are routed to aux outputs).
    void renderBuses(float* const* busLeft, float* const* busRight, int numBuses, int numSamples,
                     float* sendLeft = nullptr, float* sendRight = nullptr) noexcept {
        if (!busLeft || numBuses <= 0 || !busLeft[0] || numSamples <= 0)
            return;

        for (auto& voice : voices_) {
            if (!voice.active || voice.sample == nullptr)
                continue;

            int bus = voice.bus;
            if (bus >= numBuses || busLeft[bus] == nullptr)
                bus = 0;
            float* outLeft = busLeft[bus];
            float* outRight = busRight ? busRight[bus] : nullptr;

            const auto& sample = *voice.sample;
            const double sampleLength = static_cast<double>(sample.left.size());
            for (int n = 0; n < numSamples && voice.position < sampleLength; ++n) {
                if (voice.mixSamplesRemaining > 0) {
                    if (--voice.mixSamplesRemaining == 0) {
                        voice.leftGain = voice.targetLeftGain;
                        voice.rightGain = voice.targetRightGain;
                    } else {
                        voice.leftGain += voice.leftGainStep;
                        voice.rightGain += voice.rightGainStep;
                    }
                }
                const float envelope = envelopeGain(voice, sampleLength);
                const float left = interpolate(sample.left, voice.position) * voice.leftGain * envelope;
                const float rightSource = sample.right.empty()
                    ? interpolate(sample.left, voice.position)
                    : interpolate(sample.right, voice.position);

                const float right = rightSource * voice.rightGain * envelope;
                outLeft[n] += left;
                if (outRight)
                    outRight[n] += right;
                if (sendLeft) sendLeft[n] += left;
                if (sendRight) sendRight[n] += right;

                voice.position += voice.playbackRate;
            }

            if (voice.position >= sampleLength)
                voice.active = false;
        }
    }

private:
    static void sanitiseSample(Sample& sample) noexcept {
        for (auto* channel : { &sample.left, &sample.right })
            for (auto& value : *channel) value = sanitiseSampleValue(value);
    }

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
        int bus{0};
    };

    struct Voice {
        bool active{false};
        const Sample* sample{nullptr};
        double position{0.0};
        double playbackRate{1.0};
        float leftGain{1.0f};
        float rightGain{1.0f};
        DrumInstrument instrument{DrumInstrument::Kick};
        float velocity{1.0f};
        float targetLeftGain{1.0f}, targetRightGain{1.0f};
        float leftGainStep{0.0f}, rightGainStep{0.0f};
        int mixSamplesRemaining{0};
        int attackSamples{0};
        int releaseSamples{0};
        int chokeGroup{0};
        int bus{0};
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
    int mixRampSamples_{221}; // ceil(44100 * 0.005)
};

} // namespace robodrummer
