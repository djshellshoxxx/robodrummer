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

// SPEC §29 mixer channels. Kick..Cymbals are dry stems fed by drum voices; Room is the
// ambience return (fed by per-channel room sends); Master is the final stage.
enum class MixerChannel : std::uint8_t { Kick, Snare, Toms, HiHat, Ride, Cymbals, Room, Master };

// Mixer state with smoothed (zipper-free) gain/pan/send. Not thread-safe: set parameters from
// the audio thread (e.g. copying atomics at block start) and call advance() once per sample.
// Defaults: 0 dB, centre, unmuted, no solo, room send 0 (so the player is transparent).
class DrumMixer {
public:
    static constexpr std::size_t ChannelCount = 8;
    static constexpr std::size_t DryChannelCount = 6; // Kick..Cymbals
    static constexpr float MinGainDb = -60.0f;        // <= this is silence
    static constexpr float MaxGainDb = 12.0f;

    // Documented DrumInstrument -> channel mapping:
    //   Kick -> Kick; Snare, Rimshot, Sidestick, Clap -> Snare;
    //   HighTom, MidTom, FloorTom -> Toms; ClosedHat, OpenHat -> HiHat; Ride -> Ride;
    //   Crash, China, Splash -> Cymbals; Cowbell, Tambourine (percussion) -> Cymbals.
    [[nodiscard]] static constexpr MixerChannel channelFor(DrumInstrument i) noexcept {
        switch (i) {
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

    DrumMixer() { prepare(44100.0); }

    // Sets smoothing time (~5 ms) and jumps all smoothers to their targets.
    void prepare(double sampleRate) noexcept {
        const double sr = sampleRate > 8000.0 ? sampleRate : 44100.0;
        coeff_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sr)));
        snap();
    }

    void setGainDb(MixerChannel c, float db) noexcept {
        ch_[idx(c)].gainDb = std::clamp(std::isfinite(db) ? db : 0.0f, MinGainDb, MaxGainDb);
        retarget();
    }
    void setPan(MixerChannel c, float pan) noexcept {
        ch_[idx(c)].pan = std::clamp(std::isfinite(pan) ? pan : 0.0f, -1.0f, 1.0f);
        retarget();
    }
    void setMute(MixerChannel c, bool m) noexcept { ch_[idx(c)].mute = m; retarget(); }
    void setSolo(MixerChannel c, bool s) noexcept {
        if (c != MixerChannel::Master) ch_[idx(c)].solo = s;
        retarget();
    }
    // Ambience send 0..1 for dry channels (ignored for Room/Master). Pre-fader, follows mute/solo.
    void setRoomSend(MixerChannel c, float amount) noexcept {
        if (idx(c) < DryChannelCount)
            ch_[idx(c)].roomSend = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
        retarget();
    }

    [[nodiscard]] float gainDb(MixerChannel c) const noexcept { return ch_[idx(c)].gainDb; }
    [[nodiscard]] float pan(MixerChannel c) const noexcept { return ch_[idx(c)].pan; }
    [[nodiscard]] bool muted(MixerChannel c) const noexcept { return ch_[idx(c)].mute; }
    [[nodiscard]] bool soloed(MixerChannel c) const noexcept { return ch_[idx(c)].solo; }
    [[nodiscard]] float roomSend(MixerChannel c) const noexcept { return ch_[idx(c)].roomSend; }

    // Solo semantics: solo applies to the six dry channels. If any dry channel is soloed, other
    // dry channels are silenced and only soloed channels feed the room. Soloing Room silences
    // the dry channels (unless soloed) and leaves the room return audible. Room is only
    // silenced by its own mute. Master ignores solo.
    [[nodiscard]] bool audible(MixerChannel c) const noexcept { return targetAudible(idx(c)); }

    // Jump smoothers to current targets (no ramp).
    void snap() noexcept {
        retarget();
        for (std::size_t i = 0; i < ChannelCount; ++i) {
            fader_[i].cur = fader_[i].tgt;
            pan_[i].cur = pan_[i].tgt;
            send_[i].cur = send_[i].tgt;
        }
    }

    // Advance all smoothers by one sample. Call once per output sample, then read currents.
    void advance() noexcept {
        for (std::size_t i = 0; i < ChannelCount; ++i) {
            fader_[i].step(coeff_);
            pan_[i].step(coeff_);
            if (i < DryChannelCount) send_[i].step(coeff_);
        }
    }

    [[nodiscard]] float faderGain(MixerChannel c) const noexcept { return fader_[idx(c)].cur; }
    [[nodiscard]] float sendGain(MixerChannel c) const noexcept { return send_[idx(c)].cur; }
    // Balance-style pan law matching DrumSamplePlayer's per-instrument pan (centre = unity).
    [[nodiscard]] float leftGain(MixerChannel c) const noexcept { const float p = pan_[idx(c)].cur; return p > 0.0f ? 1.0f - p : 1.0f; }
    [[nodiscard]] float rightGain(MixerChannel c) const noexcept { const float p = pan_[idx(c)].cur; return p < 0.0f ? 1.0f + p : 1.0f; }
    // True when any send target or current value is non-zero.
    [[nodiscard]] bool anySendActive() const noexcept {
        for (std::size_t i = 0; i < DryChannelCount; ++i)
            if (send_[i].cur != 0.0f || send_[i].tgt != 0.0f) return true;
        return false;
    }

private:
    struct Settings { float gainDb{0.0f}; float pan{0.0f}; bool mute{false}; bool solo{false}; float roomSend{0.0f}; };
    struct Smooth {
        float cur{0.0f}, tgt{0.0f};
        void step(float k) noexcept {
            if (cur == tgt) return;
            cur += (tgt - cur) * k;
            if (std::abs(tgt - cur) < 1e-5f) cur = tgt;
        }
    };

    [[nodiscard]] static constexpr std::size_t idx(MixerChannel c) noexcept { return static_cast<std::size_t>(c); }
    [[nodiscard]] static float dbToLin(float db) noexcept { return db <= MinGainDb ? 0.0f : std::pow(10.0f, db / 20.0f); }

    [[nodiscard]] bool anyDrySolo() const noexcept {
        for (std::size_t i = 0; i < DryChannelCount; ++i) if (ch_[i].solo) return true;
        return false;
    }
    [[nodiscard]] bool targetAudible(std::size_t i) const noexcept {
        if (i >= DryChannelCount) return !ch_[i].mute;
        if (ch_[i].mute) return false;
        const bool soloActive = anyDrySolo() || ch_[idx(MixerChannel::Room)].solo;
        return !soloActive || ch_[i].solo;
    }

    void retarget() noexcept {
        const bool drySolo = anyDrySolo();
        for (std::size_t i = 0; i < ChannelCount; ++i) {
            fader_[i].tgt = targetAudible(i) ? dbToLin(ch_[i].gainDb) : 0.0f;
            pan_[i].tgt = ch_[i].pan;
            if (i < DryChannelCount) {
                const bool sendOk = !ch_[i].mute && (!drySolo || ch_[i].solo);
                send_[i].tgt = sendOk ? ch_[i].roomSend : 0.0f;
            }
        }
    }

    std::array<Settings, ChannelCount> ch_{};
    std::array<Smooth, ChannelCount> fader_{}, pan_{}, send_{};
    float coeff_{0.004f};
};

} // namespace robodrummer
