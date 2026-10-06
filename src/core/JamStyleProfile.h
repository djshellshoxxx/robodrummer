#pragma once
#include "analysis/JamBrain.h"

namespace robodrummer {

enum class JamStyle {
    Rock,
    Blues,
    Funk,
    Punk,
    Metal,
    Shuffle,
    HardRock,
    ClassicRock,
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
    Experimental
};

inline constexpr int JamStyleCount = 21;

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
            case JamStyle::HardRock:
                return {style, 4, 2, 0.15f, 0.07f, 0.09f, 0.06f};
            case JamStyle::ClassicRock:
                return {style, 8, 5, 0.02f, 0.09f, 0.08f, 0.08f};
            case JamStyle::Alternative:
                return {style, 4, 3, 0.05f, 0.08f, 0.08f, 0.07f};
            case JamStyle::Grunge:
                return {style, 4, 3, 0.10f, 0.08f, 0.10f, 0.06f};
            case JamStyle::Soul:
                return {style, 8, 5, -0.03f, 0.08f, 0.07f, 0.07f};
            case JamStyle::Pop:
                return {style, 4, 4, -0.02f, 0.09f, 0.08f, 0.08f};
            case JamStyle::Indie:
                return {style, 4, 4, 0.01f, 0.09f, 0.08f, 0.07f};
            case JamStyle::GarageRock:
                return {style, 4, 2, 0.12f, 0.07f, 0.09f, 0.06f};
            case JamStyle::Country:
                return {style, 8, 6, -0.04f, 0.10f, 0.07f, 0.08f};
            case JamStyle::Reggae:
                return {style, 8, 6, -0.08f, 0.11f, 0.08f, 0.06f};
            case JamStyle::Disco:
                return {style, 4, 4, 0.00f, 0.08f, 0.08f, 0.07f};
            case JamStyle::ElectronicRock:
                return {style, 4, 3, 0.08f, 0.07f, 0.09f, 0.06f};
            case JamStyle::Breakbeat:
                return {style, 4, 3, 0.10f, 0.07f, 0.08f, 0.06f};
            case JamStyle::HalfTime:
                return {style, 4, 4, 0.04f, 0.09f, 0.09f, 0.07f};
            case JamStyle::Experimental:
                return {style, 4, 2, 0.22f, 0.05f, 0.06f, 0.05f};
        }
        return {};
    }
};

} // namespace robodrummer
