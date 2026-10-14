#pragma once
#include <cstdint>

namespace robodrummer {

// Structural family of a groove. Anything other than Standard is only used in 4/4; other meters fall back
// to Standard so the MeterProfile-driven grouping still applies.
enum class GroovePattern : std::uint8_t { Standard, FourOnFloor, OneDrop, HalfTime, Breakbeat, Experimental, Jazz };

struct Style {
    float kickBeat1{0.95f};
    float kickBeat3{0.70f};
    float extraKickProbability{0.08f};
    float snareBackbeat{1.0f};
    float closedHatProbability{0.96f};
    float crashOnSectionStart{0.80f};
    float humanizeMs{4.0f};
    int hatHitsPerBeat{2};
    float openHatProbability{0.02f};
    float rideProbability{0.0f};
    float ghostSnareProbability{0.04f};
    float swing{0.0f};
    GroovePattern pattern{GroovePattern::Standard};
    float sixteenthKick{0.0f};
    // Soft tempo preference (informational; never limits the clock).
    float tempoMinBpm{80.0f};
    float tempoMaxBpm{160.0f};

    static Style basicRock() noexcept { return {}; }

    static Style blues() noexcept {
        Style s;
        s.kickBeat3 = 0.52f;
        s.extraKickProbability = 0.04f;
        s.snareBackbeat = 0.94f;
        s.closedHatProbability = 0.78f;
        s.crashOnSectionStart = 0.58f;
        s.humanizeMs = 8.0f;
        s.openHatProbability = 0.06f;
        s.rideProbability = 0.18f;
        s.ghostSnareProbability = 0.10f;
        return s;
    }

    static Style funk() noexcept {
        Style s;
        s.kickBeat1 = 0.90f;
        s.kickBeat3 = 0.42f;
        s.extraKickProbability = 0.24f;
        s.snareBackbeat = 0.96f;
        s.closedHatProbability = 0.93f;
        s.crashOnSectionStart = 0.50f;
        s.humanizeMs = 5.0f;
        s.openHatProbability = 0.10f;
        s.ghostSnareProbability = 0.28f;
        return s;
    }

    static Style punk() noexcept {
        Style s;
        s.kickBeat1 = 0.99f;
        s.kickBeat3 = 0.88f;
        s.extraKickProbability = 0.18f;
        s.snareBackbeat = 1.0f;
        s.closedHatProbability = 0.99f;
        s.crashOnSectionStart = 0.95f;
        s.humanizeMs = 2.0f;
        s.openHatProbability = 0.08f;
        s.ghostSnareProbability = 0.02f;
        return s;
    }

    static Style metal() noexcept {
        Style s;
        s.kickBeat1 = 1.0f;
        s.kickBeat3 = 0.94f;
        s.extraKickProbability = 0.32f;
        s.snareBackbeat = 0.99f;
        s.closedHatProbability = 0.94f;
        s.crashOnSectionStart = 0.96f;
        s.humanizeMs = 2.5f;
        s.openHatProbability = 0.04f;
        s.rideProbability = 0.12f;
        s.ghostSnareProbability = 0.04f;
        return s;
    }

    static Style shuffle() noexcept {
        Style s = blues();
        s.kickBeat3 = 0.64f;
        s.extraKickProbability = 0.08f;
        s.closedHatProbability = 1.0f;
        s.openHatProbability = 0.0f;
        s.rideProbability = 0.0f;
        s.crashOnSectionStart = 0.68f;
        s.swing = 0.33f;
        return s;
    }

    static Style classicRock() noexcept {
        Style s;
        s.kickBeat3 = 0.75f; s.extraKickProbability = 0.10f; s.crashOnSectionStart = 0.70f;
        s.humanizeMs = 6.0f; s.rideProbability = 0.05f; s.tempoMinBpm = 90.0f; s.tempoMaxBpm = 150.0f;
        return s;
    }
    static Style hardRock() noexcept {
        Style s;
        s.kickBeat1 = 0.98f; s.kickBeat3 = 0.85f; s.extraKickProbability = 0.16f; s.crashOnSectionStart = 0.90f;
        s.humanizeMs = 3.5f; s.openHatProbability = 0.05f; s.tempoMinBpm = 100.0f; s.tempoMaxBpm = 160.0f;
        return s;
    }
    static Style alternative() noexcept {
        Style s;
        s.kickBeat1 = 0.92f; s.kickBeat3 = 0.62f; s.extraKickProbability = 0.14f; s.ghostSnareProbability = 0.10f;
        s.closedHatProbability = 0.90f; s.crashOnSectionStart = 0.60f; s.humanizeMs = 5.0f;
        s.openHatProbability = 0.05f; s.tempoMinBpm = 85.0f; s.tempoMaxBpm = 150.0f;
        return s;
    }
    static Style grunge() noexcept {
        Style s;
        s.kickBeat3 = 0.80f; s.extraKickProbability = 0.12f; s.closedHatProbability = 0.85f; s.openHatProbability = 0.12f;
        s.crashOnSectionStart = 0.85f; s.humanizeMs = 8.0f; s.ghostSnareProbability = 0.05f;
        s.tempoMinBpm = 70.0f; s.tempoMaxBpm = 130.0f;
        return s;
    }
    static Style soul() noexcept {
        Style s;
        s.kickBeat1 = 0.90f; s.kickBeat3 = 0.55f; s.extraKickProbability = 0.12f; s.ghostSnareProbability = 0.22f;
        s.closedHatProbability = 0.90f; s.crashOnSectionStart = 0.40f; s.humanizeMs = 7.0f; s.swing = 0.12f;
        s.rideProbability = 0.05f; s.tempoMinBpm = 70.0f; s.tempoMaxBpm = 115.0f;
        return s;
    }
    static Style pop() noexcept {
        Style s;
        s.kickBeat1 = 1.0f; s.kickBeat3 = 0.80f; s.extraKickProbability = 0.08f; s.closedHatProbability = 1.0f;
        s.openHatProbability = 0.04f; s.crashOnSectionStart = 0.90f; s.humanizeMs = 2.0f; s.ghostSnareProbability = 0.02f;
        s.tempoMinBpm = 95.0f; s.tempoMaxBpm = 130.0f;
        return s;
    }
    static Style indie() noexcept {
        Style s;
        s.kickBeat1 = 0.90f; s.kickBeat3 = 0.60f; s.extraKickProbability = 0.12f; s.ghostSnareProbability = 0.10f;
        s.closedHatProbability = 0.88f; s.rideProbability = 0.10f; s.crashOnSectionStart = 0.60f; s.humanizeMs = 6.0f;
        s.tempoMinBpm = 100.0f; s.tempoMaxBpm = 150.0f;
        return s;
    }
    static Style garageRock() noexcept {
        Style s;
        s.kickBeat3 = 0.80f; s.extraKickProbability = 0.14f; s.closedHatProbability = 0.90f; s.openHatProbability = 0.10f;
        s.crashOnSectionStart = 0.75f; s.humanizeMs = 9.0f; s.ghostSnareProbability = 0.03f;
        s.tempoMinBpm = 110.0f; s.tempoMaxBpm = 170.0f;
        return s;
    }
    static Style country() noexcept {
        Style s;
        s.kickBeat1 = 0.98f; s.kickBeat3 = 0.90f; s.extraKickProbability = 0.06f; s.ghostSnareProbability = 0.06f;
        s.crashOnSectionStart = 0.50f; s.humanizeMs = 6.0f; s.swing = 0.10f; s.rideProbability = 0.10f;
        s.tempoMinBpm = 90.0f; s.tempoMaxBpm = 170.0f;
        return s;
    }
    static Style reggae() noexcept {
        Style s;
        s.pattern = GroovePattern::OneDrop;
        s.kickBeat1 = 0.0f; s.kickBeat3 = 0.98f; s.extraKickProbability = 0.0f; s.closedHatProbability = 0.95f;
        s.openHatProbability = 0.04f; s.crashOnSectionStart = 0.30f; s.humanizeMs = 8.0f; s.swing = 0.20f;
        s.ghostSnareProbability = 0.02f; s.tempoMinBpm = 65.0f; s.tempoMaxBpm = 95.0f;
        return s;
    }
    static Style disco() noexcept {
        Style s;
        s.pattern = GroovePattern::FourOnFloor;
        s.kickBeat1 = 1.0f; s.kickBeat3 = 1.0f; s.extraKickProbability = 0.0f; s.closedHatProbability = 1.0f;
        s.openHatProbability = 0.80f; s.crashOnSectionStart = 0.80f; s.humanizeMs = 2.0f; s.ghostSnareProbability = 0.0f;
        s.tempoMinBpm = 110.0f; s.tempoMaxBpm = 130.0f;
        return s;
    }
    static Style electronicRock() noexcept {
        Style s;
        s.kickBeat1 = 1.0f; s.kickBeat3 = 0.95f; s.extraKickProbability = 0.22f; s.sixteenthKick = 0.12f;
        s.closedHatProbability = 1.0f; s.crashOnSectionStart = 0.95f; s.humanizeMs = 1.0f; s.ghostSnareProbability = 0.0f;
        s.tempoMinBpm = 100.0f; s.tempoMaxBpm = 150.0f;
        return s;
    }
    static Style breakbeat() noexcept {
        Style s;
        s.pattern = GroovePattern::Breakbeat;
        s.hatHitsPerBeat = 2; s.ghostSnareProbability = 0.30f; s.sixteenthKick = 0.15f; s.humanizeMs = 4.0f;
        s.crashOnSectionStart = 0.60f; s.tempoMinBpm = 130.0f; s.tempoMaxBpm = 180.0f;
        return s;
    }
    static Style halfTime() noexcept {
        Style s;
        s.pattern = GroovePattern::HalfTime;
        s.kickBeat1 = 0.98f; s.extraKickProbability = 0.18f; s.sixteenthKick = 0.10f; s.crashOnSectionStart = 0.80f;
        s.humanizeMs = 4.0f; s.ghostSnareProbability = 0.06f; s.tempoMinBpm = 60.0f; s.tempoMaxBpm = 110.0f;
        return s;
    }
    static Style experimental() noexcept {
        Style s;
        s.pattern = GroovePattern::Experimental;
        s.humanizeMs = 10.0f; s.ghostSnareProbability = 0.20f; s.sixteenthKick = 0.25f; s.openHatProbability = 0.15f;
        s.closedHatProbability = 0.70f; s.crashOnSectionStart = 0.50f; s.tempoMinBpm = 50.0f; s.tempoMaxBpm = 200.0f;
        return s;
    }
    static Style jazzSwing() noexcept {
        Style s;
        s.pattern = GroovePattern::Jazz;
        s.swing = 0.33f; s.snareBackbeat = 0.35f; s.kickBeat1 = 0.70f; s.kickBeat3 = 0.70f; s.extraKickProbability = 0.0f;
        s.closedHatProbability = 1.0f; s.rideProbability = 1.0f; s.openHatProbability = 0.0f; s.crashOnSectionStart = 0.25f;
        s.humanizeMs = 9.0f; s.ghostSnareProbability = 0.25f; s.tempoMinBpm = 90.0f; s.tempoMaxBpm = 260.0f;
        return s;
    }
};

struct GrooveContext {
    double bpm{120.0};
    double sampleRate{48000.0};
    int numerator{4};
    int denominator{4};
    float intensity{0.5f};
    bool sectionStart{false};
    std::uint32_t seed{1};
    // Drummer controls. Defaults reproduce the plain deterministic groove.
    float humanize{0.0f};     // 0 = machine-exact, 0.5 = style nominal, 1 = loose
    float feelMs{0.0f};       // + lays back (late), - pushes (early)
    float complexity{0.5f};   // scales syncopation, ghost notes and sixteenth kicks (0.5 = style nominal)
    float crashScale{1.0f};   // multiplies the style's crash tendency
};

} // namespace robodrummer
