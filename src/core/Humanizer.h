// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "core/DrumEvent.h"
#include "core/Style.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace robodrummer {

// Style- and tempo-aware microtiming and velocity shaping (SPEC section 22).
// Deterministic: the same seed, event index and parameters always give the same result.
struct Humanizer {
    double sampleRate{48000.0};
    double bpm{120.0};
    double samplesPerBeat{24000.0};
    float amount{0.0f}; // 0 = off (events untouched), 1 = full style humanization
    std::uint32_t seed{1};

    // Jitter and layback shrink as the tempo rises (a fast player has less room to be loose).
    [[nodiscard]] float tempoScale() const noexcept {
        const double b = std::isfinite(bpm) && bpm > 0.0 ? bpm : 120.0;
        return static_cast<float>(std::clamp(1.0 / (1.0 + std::max(0.0, b - 100.0) / 100.0), 0.25, 1.0));
    }

    [[nodiscard]] static float tightness(const Style& style, DrumInstrument instrument) noexcept {
        switch (instrument) {
            case DrumInstrument::Kick: return style.kickTightness;
            case DrumInstrument::Snare:
            case DrumInstrument::Rimshot:
            case DrumInstrument::Sidestick:
            case DrumInstrument::Clap: return style.snareTightness;
            case DrumInstrument::ClosedHat:
            case DrumInstrument::OpenHat:
            case DrumInstrument::Ride:
            case DrumInstrument::Tambourine: return style.hatTightness;
            case DrumInstrument::HighTom:
            case DrumInstrument::MidTom:
            case DrumInstrument::FloorTom: return 1.1f;
            default: return 0.9f; // cymbals, percussion
        }
    }

    [[nodiscard]] static float laybackMs(const Style& style, DrumInstrument instrument) noexcept {
        switch (instrument) {
            case DrumInstrument::Kick: return style.kickLaybackMs;
            case DrumInstrument::Snare:
            case DrumInstrument::Rimshot:
            case DrumInstrument::Sidestick:
            case DrumInstrument::Clap: return style.snareLaybackMs;
            case DrumInstrument::ClosedHat:
            case DrumInstrument::OpenHat:
            case DrumInstrument::Ride:
            case DrumInstrument::Tambourine: return style.hatLaybackMs;
            default: return 0.0f;
        }
    }

    // Shape one event. sampleOffset is relative to a beat-aligned origin and is clamped to
    // [minOffset, maxOffset] so humanization can never leave the bar, fill or block bounds.
    void apply(const Style& style, DrumEvent& e, std::uint32_t index, int minOffset, int maxOffset) const noexcept {
        if (!(amount > 0.0f) || !(sampleRate > 0.0)) return;
        const float a = std::clamp(amount, 0.0f, 1.0f);
        const float scale = tempoScale();
        std::uint32_t state = seed ^ (index * 2654435761u) ^ (static_cast<std::uint32_t>(e.instrument) * 40503u);
        const float r1 = next01(state), r2 = next01(state), r3 = next01(state);

        // Triangular distribution in [-1, 1]: clusters near the intended time instead of uniform scatter.
        const float tri = r1 - r2;
        const float jitterMs = tri * style.humanizeMs * tightness(style, e.instrument) * a * scale;
        const float layMs = laybackMs(style, e.instrument) * a * scale;
        const double shift = (static_cast<double>(jitterMs) + static_cast<double>(layMs)) * sampleRate * 0.001;
        const int lo = std::min(minOffset, maxOffset);
        const int hi = std::max(minOffset, maxOffset);
        e.sampleOffset = std::clamp(static_cast<int>(std::llround(static_cast<double>(e.sampleOffset) + shift)), lo, hi);

        // Velocity follows the accent structure: downbeats stay firm, off-beats vary more, ghosts barely move.
        double beats = samplesPerBeat > 0.0 ? static_cast<double>(e.sampleOffset) / samplesPerBeat : 0.0;
        double frac = beats - std::floor(beats);
        const double distToBeat = std::min(frac, 1.0 - frac);
        const float structure = distToBeat < 0.05 ? 0.35f : (std::abs(frac - 0.5) < 0.06 ? 0.7f : 1.0f);
        const float ghostDamp = e.velocity < 0.4f ? 0.5f : 1.0f;
        const float variation = (r3 * 2.0f - 1.0f) * 0.10f * a * structure * ghostDamp;
        const float offbeatSoftening = distToBeat < 0.05 ? 0.0f : -0.03f * a;
        e.velocity = std::clamp(e.velocity * (1.0f + variation) + offbeatSoftening, 0.01f, 1.0f);
    }

private:
    static float next01(std::uint32_t& state) noexcept {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>((state >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }
};

} // namespace robodrummer
