// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace robodrummer {

enum class DrummerPersonality : std::uint8_t { Balanced, Tight, LaidBack, Pushing, Aggressive, Busy, Minimal };

inline constexpr int kDrummerPersonalityCount = 7;
inline constexpr std::array<const char*, kDrummerPersonalityCount> kDrummerPersonalityNames {
    "Balanced", "Tight", "Laid-back", "Pushing", "Aggressive", "Busy", "Minimal"
};

// The drummer-character controls shared by the DRUMMER tab, programmed sections and the groove engine.
struct DrummerControls {
    float complexity{0.5f};      // 0-1
    float humanize{0.5f};        // 0-1
    float feelMs{0.0f};          // -30..+30; + lays back, - pushes
    float fillFrequency{0.5f};   // 0-1 (0.5 = style nominal)
    float surprise{0.0f};        // 0-1
    float crashTendency{1.0f};   // 0-2 multiplier on the style's crash probability
    float intensityDelta{0.0f};  // additive offset on the effective intensity
};

// A personality is a set of offsets applied on top of the user's own settings, so the sliders stay meaningful.
[[nodiscard]] inline DrummerControls applyPersonality(DrummerControls base, DrummerPersonality personality) noexcept {
    switch (personality) {
        case DrummerPersonality::Balanced: break;
        case DrummerPersonality::Tight:
            base.humanize -= 0.30f; base.surprise -= 0.10f; base.feelMs -= 0.0f; break;
        case DrummerPersonality::LaidBack:
            base.feelMs += 9.0f; base.humanize += 0.15f; base.complexity -= 0.10f; base.crashTendency -= 0.20f; break;
        case DrummerPersonality::Pushing:
            base.feelMs -= 7.0f; base.intensityDelta += 0.05f; base.fillFrequency += 0.10f; break;
        case DrummerPersonality::Aggressive:
            base.intensityDelta += 0.12f; base.complexity += 0.10f; base.crashTendency += 0.40f; base.fillFrequency += 0.15f; break;
        case DrummerPersonality::Busy:
            base.complexity += 0.30f; base.fillFrequency += 0.25f; base.surprise += 0.20f; break;
        case DrummerPersonality::Minimal:
            base.complexity -= 0.35f; base.fillFrequency -= 0.30f; base.surprise -= 0.20f; base.crashTendency -= 0.40f;
            base.intensityDelta -= 0.05f; break;
    }
    base.complexity = std::clamp(base.complexity, 0.0f, 1.0f);
    base.humanize = std::clamp(base.humanize, 0.0f, 1.0f);
    base.feelMs = std::clamp(base.feelMs, -30.0f, 30.0f);
    base.fillFrequency = std::clamp(base.fillFrequency, 0.0f, 1.0f);
    base.surprise = std::clamp(base.surprise, 0.0f, 1.0f);
    base.crashTendency = std::clamp(base.crashTendency, 0.0f, 2.0f);
    base.intensityDelta = std::clamp(base.intensityDelta, -0.5f, 0.5f);
    return base;
}

} // namespace robodrummer
