#pragma once
#include "audio/DrumKit.h"
#include "audio/DrumMixer.h"
#include "audio/RoomAmbience.h"
#include "core/DrumEvent.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace robodrummer {

class DrumSamplePlayer {
public:
    static constexpr std::size_t InstrumentCount = 16;
    static constexpr std::size_t MaxVoices = 64;
    static constexpr std::size_t MaxVelocityLayers = 4;
    static constexpr std::size_t MaxRoundRobinVariants = 4;

    DrumSamplePlayer() { resizeScratch(1024); }

    using Sample = DrumSample;

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

    // Use samples/gain/pan/tune/choke from a user kit (DrumKit.h) for every instrument the kit
    // defines; other instruments keep the built-in (starter) samples. nullptr = starter only.
    // The kit must outlive its use (see KitHandoff). Voices still reading the previous kit are
    // stopped. Audio-thread safe (no allocation).
    void useKit(const DrumKit* kit) noexcept {
        if (kit == kit_) return;
        for (auto& voice : voices_)
            if (voice.fromKit) voice.active = false;
        kit_ = kit;
        kitRoundRobin_ = {};
    }
    [[nodiscard]] const DrumKit* kit() const noexcept { return kit_; }

    void trigger(const DrumEvent& event) noexcept {
        const auto instrumentIndex = index(event.instrument);
        if (instrumentIndex >= InstrumentCount)
            return;

        auto& instrument = instruments_[instrumentIndex];
        const float velocity = std::clamp(event.velocity, 0.0f, 1.0f);

        const Sample* samplePtr = nullptr;
        float gain = instrument.gain, pan = instrument.pan, tune = instrument.tuningSemitones;
        int choke = instrument.chokeGroup;
        bool fromKit = false;

        if (kit_ != nullptr && kit_->instruments[instrumentIndex].layerCount > 0) {
            const auto& ki = kit_->instruments[instrumentIndex];
            std::size_t li = 0;
            for (std::size_t i = 0; i < ki.layerCount; ++i) {
                if (velocity >= ki.layers[i].minVelocity && velocity <= ki.layers[i].maxVelocity) { li = i; break; }
            }
            const auto& layer = ki.layers[li];
            if (layer.variantCount == 0)
                return;
            samplePtr = &layer.variants[kitRoundRobin_[instrumentIndex][li]++ % layer.variantCount];
            gain = ki.gain;
            pan = ki.pan;
            tune = ki.tuneSemitones;
            choke = ki.chokeGroup;
            fromKit = true;
        } else {
            const auto layerIndex = findLayer(instrument, velocity);
            if (layerIndex >= MaxVelocityLayers)
                return;
            auto& layer = instrument.layers[layerIndex];
            if (layer.variantCount == 0)
                return;
            const std::size_t variantIndex =
                instrument.roundRobinCounters[layerIndex]++ % layer.variantCount;
            samplePtr = &layer.variants[variantIndex];
        }
        const auto& sample = *samplePtr;
        if (sample.left.empty())
            return;

        if (choke > 0) {
            for (auto& voice : voices_) {
                if (voice.active && voice.chokeGroup == choke)
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
        const float leftPan = pan > 0.0f ? 1.0f - pan : 1.0f;
        const float rightPan = pan < 0.0f ? 1.0f + pan : 1.0f;
        voice.active = true;
        voice.sample = &sample;
        voice.position = 0.0;
        voice.playbackRate = std::pow(2.0, static_cast<double>(tune) / 12.0);
        voice.leftGain = velocity * gain * leftPan;
        voice.rightGain = velocity * gain * rightPan;
        voice.attackSamples = instrument.attackSamples;
        voice.releaseSamples = instrument.releaseSamples;
        voice.chokeGroup = choke;
        voice.fromKit = fromKit;
        voice.channel = static_cast<std::uint8_t>(DrumMixer::channelFor(event.instrument));
    }

    // Mixer (SPEC §29): per-channel gain/pan/mute/solo and room send. Parameter changes are
    // smoothed; call from the audio thread (or while not rendering).
    [[nodiscard]] DrumMixer& mixer() noexcept { return mixer_; }
    [[nodiscard]] const DrumMixer& mixer() const noexcept { return mixer_; }
    [[nodiscard]] RoomAmbience& room() noexcept { return room_; }

    // Not realtime-safe: sizes scratch stems (maxBlockSize) and the room buffers.
    void prepare(double sampleRate, int maxBlockSize) {
        mixer_.prepare(sampleRate);
        room_.prepare(sampleRate);
        roomTailSamples_ = static_cast<long long>(room_.tailSeconds() * (sampleRate > 8000.0 ? sampleRate : 44100.0));
        roomIdle_ = roomTailSamples_;
        resizeScratch(static_cast<std::size_t>(std::max(16, maxBlockSize)));
    }

    // Sum render: voices -> dry channel stems (per-instrument gain/pan, then mixer channel
    // fader/pan/mute/solo) + room return, scaled by the Master channel, ADDED to outputs[0..1].
    // Any block size is accepted (internally chunked). No allocation.
    void render(float** outputs, int channels, int numSamples) noexcept {
        if (!outputs || !outputs[0] || channels <= 0 || numSamples <= 0)
            return;
        for (int offset = 0; offset < numSamples; offset += static_cast<int>(blockCap_)) {
            const int len = std::min<int>(numSamples - offset, static_cast<int>(blockCap_));
            renderChunk(len);
            float* l = outputs[0] + offset;
            float* r = (channels > 1 && outputs[1]) ? outputs[1] + offset : nullptr;
            for (int n = 0; n < len; ++n) {
                const auto m = MixerChannel::Master;
                const float mg = mixer_.faderGain(m);
                const float ml = mg * mixer_.leftGain(m), mr = mg * mixer_.rightGain(m);
                l[n] += mixL_[static_cast<std::size_t>(n)] * ml;
                if (r) r[n] += mixR_[static_cast<std::size_t>(n)] * mr;
            }
        }
    }

    // Multi-output render (future multi-out): `stems` holds 2 pointers (L,R) per bus in order
    // Kick, Snare, Toms, HiHat, Ride, Cymbals, Room => StemBusCount*2 = 14 pointers. Post-fader,
    // pre-Master; ADDED to the buffers. Null pointers are skipped.
    static constexpr int StemBusCount = 7;
    void renderStems(float** stems, int numSamples) noexcept {
        if (!stems || numSamples <= 0)
            return;
        for (int offset = 0; offset < numSamples; offset += static_cast<int>(blockCap_)) {
            const int len = std::min<int>(numSamples - offset, static_cast<int>(blockCap_));
            renderChunk(len);
            for (std::size_t b = 0; b < StemBusCount; ++b) {
                float* l = stems[2 * b];
                float* r = stems[2 * b + 1];
                const auto& sl = b < DrumMixer::DryChannelCount ? stemL_[b] : roomL_;
                const auto& sr = b < DrumMixer::DryChannelCount ? stemR_[b] : roomR_;
                for (int n = 0; n < len; ++n) {
                    if (l) l[offset + n] += sl[static_cast<std::size_t>(n)];
                    if (r) r[offset + n] += sr[static_cast<std::size_t>(n)];
                }
            }
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
        std::uint8_t channel{0};
        bool fromKit{false};
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

    void resizeScratch(std::size_t cap) {
        blockCap_ = cap;
        for (auto& v : stemL_) v.assign(cap, 0.0f);
        for (auto& v : stemR_) v.assign(cap, 0.0f);
        roomL_.assign(cap, 0.0f);
        roomR_.assign(cap, 0.0f);
        mixL_.assign(cap, 0.0f);
        mixR_.assign(cap, 0.0f);
    }

    // Renders `len` (<= blockCap_) samples into stemL_/stemR_ (post-fader), roomL_/roomR_
    // (post-fader room return) and mixL_/mixR_ (sum of all, pre-Master).
    void renderChunk(int len, void* = nullptr) noexcept {
        constexpr std::size_t Dry = DrumMixer::DryChannelCount;
        const auto count = static_cast<std::size_t>(len);
        std::array<bool, Dry> dirty{};
        for (std::size_t c = 0; c < Dry; ++c) {
            std::fill_n(stemL_[c].begin(), count, 0.0f);
            std::fill_n(stemR_[c].begin(), count, 0.0f);
        }

        for (auto& voice : voices_) {
            if (!voice.active || voice.sample == nullptr)
                continue;
            const auto& sample = *voice.sample;
            const std::size_t c = std::min<std::size_t>(voice.channel, Dry - 1);
            float* outL = stemL_[c].data();
            float* outR = stemR_[c].data();
            dirty[c] = true;
            const double sampleLength = static_cast<double>(sample.left.size());
            for (int n = 0; n < len && voice.position < sampleLength; ++n) {
                const float envelope = envelopeGain(voice, sampleLength);
                const float left = interpolate(sample.left, voice.position) * voice.leftGain * envelope;
                const float rightSource = sample.right.empty()
                    ? interpolate(sample.left, voice.position)
                    : interpolate(sample.right, voice.position);
                outL[n] += left;
                outR[n] += rightSource * voice.rightGain * envelope;
                voice.position += voice.playbackRate;
            }
            if (voice.position >= sampleLength)
                voice.active = false;
        }

        const bool roomOn = mixer_.anySendActive() || roomIdle_ < roomTailSamples_;
        for (int n = 0; n < len; ++n) {
            const auto i = static_cast<std::size_t>(n);
            mixer_.advance();
            float sumL = 0.0f, sumR = 0.0f, sendL = 0.0f, sendR = 0.0f;
            for (std::size_t c = 0; c < Dry; ++c) {
                if (!dirty[c]) continue;
                const auto ch = static_cast<MixerChannel>(c);
                const float dl = stemL_[c][i], dr = stemR_[c][i];
                const float send = mixer_.sendGain(ch);
                sendL += dl * send;
                sendR += dr * send;
                const float f = mixer_.faderGain(ch);
                const float l = dl * f * mixer_.leftGain(ch);
                const float r = dr * f * mixer_.rightGain(ch);
                stemL_[c][i] = l;
                stemR_[c][i] = r;
                sumL += l;
                sumR += r;
            }
            float wl = 0.0f, wr = 0.0f;
            if (roomOn) {
                room_.process(sendL, sendR, wl, wr);
                const auto rc = MixerChannel::Room;
                const float f = mixer_.faderGain(rc);
                wl *= f * mixer_.leftGain(rc);
                wr *= f * mixer_.rightGain(rc);
            }
            roomL_[i] = wl;
            roomR_[i] = wr;
            mixL_[i] = sumL + wl;
            mixR_[i] = sumR + wr;
        }
        if (mixer_.anySendActive()) roomIdle_ = 0;
        else if (roomIdle_ < roomTailSamples_) roomIdle_ += len;
    }

    const DrumKit* kit_{nullptr};
    std::array<std::array<std::size_t, 4>, InstrumentCount> kitRoundRobin_{};
    std::size_t blockCap_{1024};
    std::array<std::vector<float>, DrumMixer::DryChannelCount> stemL_{}, stemR_{};
    std::vector<float> roomL_, roomR_, mixL_, mixR_;
    DrumMixer mixer_;
    RoomAmbience room_;
    long long roomTailSamples_{44100 * 3};
    long long roomIdle_{44100 * 3};
    std::array<InstrumentSet, InstrumentCount> instruments_{};
    std::array<Voice, MaxVoices> voices_{};
};

} // namespace robodrummer
