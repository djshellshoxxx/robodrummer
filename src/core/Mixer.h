// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include "core/DrumEvent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

enum class MixerChannel : std::uint8_t { Kick, Snare, Toms, HiHat, Ride, Cymbals, Room, Master };
inline constexpr std::size_t kMixerChannelCount = 8;
inline constexpr std::array<const char*, kMixerChannelCount> kMixerChannelNames {
    "Kick", "Snare", "Toms", "Hi-Hat", "Ride", "Cymbals", "Room", "Master"
};
// Channels that carry drum voices (Room is a reverb return and Master the final stage).
inline constexpr std::size_t kMixerSourceChannelCount = 6;

[[nodiscard]] inline MixerChannel mixerChannelFor(DrumInstrument instrument) noexcept {
    switch (instrument) {
        case DrumInstrument::Kick: return MixerChannel::Kick;
        case DrumInstrument::Snare:
        case DrumInstrument::Rimshot:
        case DrumInstrument::Sidestick:
        case DrumInstrument::Clap: return MixerChannel::Snare;
        case DrumInstrument::HighTom:
        case DrumInstrument::MidTom:
        case DrumInstrument::FloorTom: return MixerChannel::Toms;
        case DrumInstrument::ClosedHat:
        case DrumInstrument::OpenHat: return MixerChannel::HiHat;
        case DrumInstrument::Ride: return MixerChannel::Ride;
        case DrumInstrument::Crash:
        case DrumInstrument::China:
        case DrumInstrument::Splash:
        case DrumInstrument::Cowbell:
        case DrumInstrument::Tambourine: return MixerChannel::Cymbals;
    }
    return MixerChannel::Cymbals;
}

// Multi-output routing: bus 0 is the main stereo mix, buses 1-4 are optional stereo aux outputs.
inline constexpr int kAuxBusCount = 4;
inline constexpr std::array<const char*, kAuxBusCount> kAuxBusNames { "Kick", "Snare", "Toms", "Hats + Cymbals" };

[[nodiscard]] inline int auxBusFor(MixerChannel channel) noexcept {
    switch (channel) {
        case MixerChannel::Kick: return 1;
        case MixerChannel::Snare: return 2;
        case MixerChannel::Toms: return 3;
        case MixerChannel::HiHat:
        case MixerChannel::Ride:
        case MixerChannel::Cymbals: return 4;
        default: return 0;
    }
}

[[nodiscard]] inline float dbToGain(float db) noexcept { return db <= -60.0f ? 0.0f : std::pow(10.0f, db / 20.0f); }

struct MixerSettings {
    std::array<float, kMixerChannelCount> gainDb{ 0, 0, 0, 0, 0, 0, -12.0f, 0 };
    std::array<float, kMixerChannelCount> pan{ 0, 0, 0, 0, 0, 0, 0, 0 };
    std::array<bool, kMixerChannelCount> mute{};
    std::array<bool, kMixerChannelCount> solo{};

    [[nodiscard]] bool anySolo() const noexcept {
        for (std::size_t i = 0; i < kMixerSourceChannelCount; ++i) if (solo[i]) return true;
        return false;
    }
};

struct InstrumentMix { float gain{1.0f}; float pan{0.0f}; };

// Gain and pan applied to a drum voice: channel fader, mute and solo (any solo silences non-soloed channels).
[[nodiscard]] inline InstrumentMix resolveInstrumentMix(DrumInstrument instrument, const MixerSettings& m) noexcept {
    const auto channel = static_cast<std::size_t>(mixerChannelFor(instrument));
    const bool audible = m.anySolo() ? m.solo[channel] : !m.mute[channel];
    return { audible ? dbToGain(m.gainDb[channel]) : 0.0f, std::clamp(m.pan[channel], -1.0f, 1.0f) };
}

} // namespace robodrummer
