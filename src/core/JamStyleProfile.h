#pragma once
#include "analysis/JamBrain.h"
#include <array>

namespace robodrummer {

// Indices 0-5 predate the full style set and are persisted in saved sessions: only ever append.
enum class JamStyle {
    Rock,
    Blues,
    Funk,
    Punk,
    Metal,
    Shuffle,
    ClassicRock,
    HardRock,
    Alternative,
    Grunge,
    Soul,
    Pop,
    Indie,
    GarageRock,
    Country,
    Reggae,
    Disco,
    ElectronicRock,
    Breakbeat,
    HalfTime,
    Experimental,
    JazzSwing
};

inline constexpr int kJamStyleCount = 22;
inline constexpr std::array<const char*, kJamStyleCount> kJamStyleNames {
    "Rock", "Blues", "Funk", "Punk", "Metal", "Shuffle", "Classic Rock", "Hard Rock", "Alternative", "Grunge", "Soul",
    "Pop", "Indie", "Garage Rock", "Country", "Reggae", "Disco", "Electronic Rock", "Breakbeat", "Half-Time",
    "Experimental", "Jazz / Swing"
};

struct JamStyleProfile {
    JamStyle style{JamStyle::Rock};
    int phraseBars{4};
    int minBarsBeforeFill{3};
    float fillBias{0.0f};
    float buildThreshold{0.08f};
    float releaseThreshold{0.08f};
    float breakActivityThreshold{0.08f};

    [[nodiscard]] JamBrainSettings toBrainSettings() const noexcept {
        JamBrainSettings settings;
        settings.phraseBars = phraseBars;
        settings.minBarsBeforeFill = minBarsBeforeFill;
        settings.fillBias = fillBias;
        settings.buildThreshold = buildThreshold;
        settings.releaseThreshold = releaseThreshold;
        settings.breakActivityThreshold = breakActivityThreshold;
        return settings;
    }

    [[nodiscard]] static JamStyleProfile forStyle(JamStyle style) noexcept {
        switch (style) {
            case JamStyle::Rock:
                return {style, 4, 3, 0.08f, 0.08f, 0.08f, 0.08f};
            case JamStyle::Blues:
                return {style, 8, 6, -0.05f, 0.10f, 0.07f, 0.07f};
            case JamStyle::Funk:
                return {style, 4, 3, -0.08f, 0.07f, 0.07f, 0.06f};
            case JamStyle::Punk:
                return {style, 4, 2, 0.18f, 0.06f, 0.08f, 0.06f};
            case JamStyle::Metal:
                return {style, 4, 2, 0.20f, 0.06f, 0.10f, 0.05f};
            case JamStyle::Shuffle:
                return {style, 8, 5, 0.00f, 0.09f, 0.08f, 0.07f};
            case JamStyle::ClassicRock:
                return {style, 4, 3, 0.06f, 0.08f, 0.08f, 0.08f};
            case JamStyle::HardRock:
                return {style, 4, 3, 0.12f, 0.07f, 0.09f, 0.07f};
            case JamStyle::Alternative:
                return {style, 4, 4, 0.02f, 0.08f, 0.08f, 0.07f};
            case JamStyle::Grunge:
                return {style, 4, 4, 0.04f, 0.07f, 0.10f, 0.07f};
            case JamStyle::Soul:
                return {style, 8, 5, -0.04f, 0.09f, 0.07f, 0.07f};
            case JamStyle::Pop:
                return {style, 4, 4, 0.05f, 0.08f, 0.08f, 0.08f};
            case JamStyle::Indie:
                return {style, 4, 4, 0.00f, 0.08f, 0.08f, 0.07f};
            case JamStyle::GarageRock:
                return {style, 4, 3, 0.10f, 0.07f, 0.08f, 0.06f};
            case JamStyle::Country:
                return {style, 8, 5, -0.02f, 0.09f, 0.08f, 0.07f};
            case JamStyle::Reggae:
                return {style, 8, 6, -0.10f, 0.10f, 0.08f, 0.07f};
            case JamStyle::Disco:
                return {style, 8, 4, 0.02f, 0.08f, 0.08f, 0.07f};
            case JamStyle::ElectronicRock:
                return {style, 8, 4, 0.04f, 0.08f, 0.08f, 0.07f};
            case JamStyle::Breakbeat:
                return {style, 4, 3, 0.08f, 0.07f, 0.07f, 0.06f};
            case JamStyle::HalfTime:
                return {style, 8, 5, 0.06f, 0.08f, 0.09f, 0.07f};
            case JamStyle::Experimental:
                return {style, 4, 2, 0.14f, 0.06f, 0.06f, 0.05f};
            case JamStyle::JazzSwing:
                return {style, 8, 6, -0.06f, 0.10f, 0.07f, 0.07f};
        }
        return {};
    }
};

} // namespace robodrummer
