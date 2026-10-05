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
