#pragma once
#include <cstdint>

namespace robodrummer {

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
    bool fourOnFloorKick{false};
    bool halfTimeBackbeat{false};
    bool oneDrop{false};

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

    static Style hardRock() noexcept {
        Style s = basicRock();
        s.kickBeat3 = 0.86f;
        s.extraKickProbability = 0.16f;
        s.crashOnSectionStart = 0.93f;
        s.openHatProbability = 0.06f;
        s.ghostSnareProbability = 0.03f;
        s.humanizeMs = 3.0f;
        return s;
    }

    static Style classicRock() noexcept {
        Style s = basicRock();
        s.kickBeat3 = 0.62f;
        s.extraKickProbability = 0.06f;
        s.crashOnSectionStart = 0.72f;
        s.openHatProbability = 0.05f;
        s.rideProbability = 0.12f;
        s.humanizeMs = 6.0f;
        return s;
    }

    static Style alternative() noexcept {
        Style s = basicRock();
        s.kickBeat3 = 0.68f;
        s.extraKickProbability = 0.13f;
        s.openHatProbability = 0.09f;
        s.ghostSnareProbability = 0.09f;
        s.humanizeMs = 5.0f;
        return s;
    }

    static Style grunge() noexcept {
        Style s = hardRock();
        s.kickBeat3 = 0.78f;
        s.closedHatProbability = 0.88f;
        s.openHatProbability = 0.14f;
        s.humanizeMs = 7.0f;
        return s;
    }

    static Style soul() noexcept {
        Style s = funk();
        s.kickBeat3 = 0.50f;
        s.extraKickProbability = 0.14f;
        s.ghostSnareProbability = 0.20f;
        s.rideProbability = 0.08f;
        s.humanizeMs = 7.0f;
        return s;
    }

    static Style pop() noexcept {
        Style s = basicRock();
        s.kickBeat1 = 1.0f;
        s.kickBeat3 = 0.82f;
        s.extraKickProbability = 0.07f;
        s.crashOnSectionStart = 0.70f;
        s.humanizeMs = 2.5f;
        s.openHatProbability = 0.04f;
        return s;
    }

    static Style indie() noexcept {
        Style s = alternative();
        s.kickBeat3 = 0.60f;
        s.openHatProbability = 0.13f;
        s.rideProbability = 0.10f;
        s.ghostSnareProbability = 0.12f;
        s.humanizeMs = 6.5f;
        return s;
    }

    static Style garageRock() noexcept {
        Style s = punk();
        s.kickBeat3 = 0.74f;
        s.closedHatProbability = 0.90f;
        s.crashOnSectionStart = 0.84f;
        s.humanizeMs = 9.0f;
        return s;
    }

    static Style country() noexcept {
        Style s = basicRock();
        s.kickBeat3 = 0.58f;
        s.extraKickProbability = 0.04f;
        s.openHatProbability = 0.03f;
        s.rideProbability = 0.26f;
        s.ghostSnareProbability = 0.06f;
        s.humanizeMs = 5.0f;
        return s;
    }

    static Style reggae() noexcept {
        Style s = basicRock();
        s.kickBeat1 = 1.0f;
        s.kickBeat3 = 1.0f;
        s.extraKickProbability = 0.02f;
        s.snareBackbeat = 1.0f;
        s.closedHatProbability = 0.82f;
        s.crashOnSectionStart = 0.34f;
        s.openHatProbability = 0.08f;
        s.rideProbability = 0.04f;
        s.ghostSnareProbability = 0.02f;
        s.humanizeMs = 7.0f;
        s.oneDrop = true;
        return s;
    }

    static Style disco() noexcept {
        Style s = pop();
        s.kickBeat1 = 1.0f;
        s.kickBeat3 = 1.0f;
        s.extraKickProbability = 0.02f;
        s.closedHatProbability = 1.0f;
        s.openHatProbability = 0.16f;
        s.crashOnSectionStart = 0.60f;
        s.fourOnFloorKick = true;
        s.humanizeMs = 2.0f;
        return s;
    }

    static Style electronicRock() noexcept {
        Style s = hardRock();
        s.kickBeat1 = 1.0f;
        s.kickBeat3 = 0.96f;
        s.extraKickProbability = 0.18f;
        s.hatHitsPerBeat = 4;
        s.closedHatProbability = 0.95f;
        s.openHatProbability = 0.08f;
        s.fourOnFloorKick = true;
        s.humanizeMs = 1.5f;
        return s;
    }

    static Style breakbeat() noexcept {
        Style s = funk();
        s.kickBeat3 = 0.60f;
        s.extraKickProbability = 0.38f;
        s.hatHitsPerBeat = 4;
        s.openHatProbability = 0.06f;
        s.ghostSnareProbability = 0.32f;
        s.crashOnSectionStart = 0.42f;
        s.humanizeMs = 4.5f;
        return s;
    }

    static Style halfTime() noexcept {
        Style s = hardRock();
        s.kickBeat3 = 0.78f;
        s.extraKickProbability = 0.11f;
        s.halfTimeBackbeat = true;
        s.openHatProbability = 0.07f;
        s.humanizeMs = 4.5f;
        return s;
    }

    static Style experimental() noexcept {
        Style s = funk();
        s.kickBeat1 = 0.72f;
        s.kickBeat3 = 0.48f;
        s.extraKickProbability = 0.42f;
        s.snareBackbeat = 0.82f;
        s.closedHatProbability = 0.78f;
        s.openHatProbability = 0.22f;
        s.rideProbability = 0.18f;
        s.ghostSnareProbability = 0.36f;
        s.crashOnSectionStart = 0.55f;
        s.humanizeMs = 12.0f;
        s.hatHitsPerBeat = 4;
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
};

} // namespace robodrummer
