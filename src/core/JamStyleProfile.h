#pragma once
#include "analysis/JamBrain.h"
#include "core/Style.h"
#include <algorithm>
#include <cmath>

namespace robodrummer {

enum class JamStyle {
    Rock,
    Blues,
    Funk,
    Punk,
    Metal,
    Shuffle,
    // Appended in SPEC section 20 order; never reorder (host parameter indices depend on it).
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
    Experimental,
    JazzSwing
};

inline constexpr int JamStyleCount = static_cast<int>(JamStyle::JazzSwing) + 1;

[[nodiscard]] inline const char* jamStyleName(JamStyle style) noexcept {
    switch (style) {
        case JamStyle::Rock: return "Rock";
        case JamStyle::Blues: return "Blues";
        case JamStyle::Funk: return "Funk";
        case JamStyle::Punk: return "Punk";
        case JamStyle::Metal: return "Metal";
        case JamStyle::Shuffle: return "Shuffle";
        case JamStyle::HardRock: return "Hard Rock";
        case JamStyle::ClassicRock: return "Classic Rock";
        case JamStyle::Alternative: return "Alternative";
        case JamStyle::Grunge: return "Grunge";
        case JamStyle::Soul: return "Soul";
        case JamStyle::Pop: return "Pop";
        case JamStyle::Indie: return "Indie";
        case JamStyle::GarageRock: return "Garage Rock";
        case JamStyle::Country: return "Country";
        case JamStyle::Reggae: return "Reggae";
        case JamStyle::Disco: return "Disco";
        case JamStyle::ElectronicRock: return "Electronic Rock";
        case JamStyle::Breakbeat: return "Breakbeat";
        case JamStyle::HalfTime: return "Half-Time";
        case JamStyle::Experimental: return "Experimental";
        case JamStyle::JazzSwing: return "Jazz/Swing";
    }
    return "Rock";
}

[[nodiscard]] inline Style styleForJamStyle(JamStyle style) noexcept {
    switch (style) {
        case JamStyle::Rock: return Style::basicRock();
        case JamStyle::Blues: return Style::blues();
        case JamStyle::Funk: return Style::funk();
        case JamStyle::Punk: return Style::punk();
        case JamStyle::Metal: return Style::metal();
        case JamStyle::Shuffle: return Style::shuffle();
        case JamStyle::HardRock: return Style::hardRock();
        case JamStyle::ClassicRock: return Style::classicRock();
        case JamStyle::Alternative: return Style::alternative();
        case JamStyle::Grunge: return Style::grunge();
        case JamStyle::Soul: return Style::soul();
        case JamStyle::Pop: return Style::pop();
        case JamStyle::Indie: return Style::indie();
        case JamStyle::GarageRock: return Style::garageRock();
        case JamStyle::Country: return Style::country();
        case JamStyle::Reggae: return Style::reggae();
        case JamStyle::Disco: return Style::disco();
        case JamStyle::ElectronicRock: return Style::electronicRock();
        case JamStyle::Breakbeat: return Style::breakbeat();
        case JamStyle::HalfTime: return Style::halfTime();
        case JamStyle::Experimental: return Style::experimental();
        case JamStyle::JazzSwing: return Style::jazzSwing();
    }
    return Style::basicRock();
}

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

    // fillFrequencyScale: user fill-frequency control, 0-2 (1 = style default; 0 = no automatic fills).
    // The style's own fillFrequency multiplier is NOT applied here; pass styleForJamStyle(s).fillFrequency * user.
    [[nodiscard]] JamBrainSettings toBrainSettings(float fillFrequencyScale) const noexcept {
        auto settings = toBrainSettings();
        const float f = std::clamp(std::isfinite(fillFrequencyScale) ? fillFrequencyScale : 1.0f, 0.0f, 2.0f);
        if (f <= 0.01f) {
            settings.minBarsBeforeFill = 1 << 20;
            settings.fillBias = -1.0f;
        } else {
            settings.minBarsBeforeFill = std::max(1, static_cast<int>(std::lround(minBarsBeforeFill / f)));
            settings.fillBias = fillBias + 0.25f * (f - 1.0f);
        }
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
            case JamStyle::HardRock:       return {style, 4, 3, 0.12f, 0.07f, 0.09f, 0.07f};
            case JamStyle::ClassicRock:    return {style, 4, 4, 0.04f, 0.08f, 0.08f, 0.08f};
            case JamStyle::Alternative:    return {style, 4, 3, 0.02f, 0.08f, 0.08f, 0.07f};
            case JamStyle::Grunge:         return {style, 4, 4, -0.02f, 0.09f, 0.09f, 0.07f};
            case JamStyle::Soul:           return {style, 4, 4, -0.06f, 0.09f, 0.07f, 0.07f};
            case JamStyle::Pop:            return {style, 4, 4, -0.10f, 0.07f, 0.08f, 0.08f};
            case JamStyle::Indie:          return {style, 4, 4, -0.04f, 0.09f, 0.08f, 0.07f};
            case JamStyle::GarageRock:     return {style, 4, 3, 0.10f, 0.07f, 0.08f, 0.07f};
            case JamStyle::Country:        return {style, 8, 5, -0.04f, 0.09f, 0.08f, 0.07f};
            case JamStyle::Reggae:         return {style, 8, 7, -0.15f, 0.10f, 0.07f, 0.06f};
            case JamStyle::Disco:          return {style, 8, 6, -0.06f, 0.08f, 0.08f, 0.08f};
            case JamStyle::ElectronicRock: return {style, 8, 5, 0.00f, 0.07f, 0.08f, 0.07f};
            case JamStyle::Breakbeat:      return {style, 4, 2, 0.14f, 0.06f, 0.08f, 0.06f};
            case JamStyle::HalfTime:       return {style, 8, 5, 0.04f, 0.08f, 0.08f, 0.07f};
            case JamStyle::Experimental:   return {style, 4, 2, 0.16f, 0.06f, 0.07f, 0.05f};
            case JamStyle::JazzSwing:      return {style, 8, 6, -0.08f, 0.10f, 0.07f, 0.06f};
        }
        return {};
    }
};

} // namespace robodrummer
