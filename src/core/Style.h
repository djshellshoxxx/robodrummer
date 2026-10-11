#pragma once
#include <cstdint>

namespace robodrummer {

// Fill vocabulary a style prefers (SPEC section 23).
enum class FillVocabulary : std::uint8_t { TomRun, SnareRoll, KickCrash, Mixed };

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

    // Groove-feel extensions (all neutral by default so the original six styles are unchanged).
    bool fourOnFloor{false};          // kick on every beat (disco, electronic rock)
    bool oneDrop{false};              // kick + snare only on the middle beat of a bar (reggae)
    bool halfTimeSnare{false};        // single backbeat on beat 3 (HalfTime style; not the half-time command)
    bool jazzRide{false};             // swung ride pattern, feathered kick, hat on 2 & 4
    bool trainBeat{false};            // country boom-chick with light snare eighths
    float offbeatOpenHat{0.0f};       // probability of an open hat on the "and" of each beat
    float syncopation{0.0f};          // 0-1 extra off-beat / 16th-note kick placement
    float hatVariation{0.0f};         // 0-1 hat accent / 16th-note variation
    float sidestickProbability{0.0f}; // backbeat played as sidestick instead of snare

    // Microtiming tendencies in milliseconds (positive = behind the beat) and per-limb tightness.
    float snareLaybackMs{0.0f};
    float kickLaybackMs{0.0f};
    float hatLaybackMs{0.0f};
    float hatTightness{0.6f};         // jitter multiplier for hats/ride (lower = tighter)
    float snareTightness{1.0f};
    float kickTightness{0.8f};

    // Fill behavior.
    FillVocabulary fillVocabulary{FillVocabulary::TomRun};
    float fillFrequency{1.0f};         // style multiplier on how often fills are requested
    float fillCrashProbability{0.85f}; // chance of a crash landing after a fill (when enabled in the engine)

    static Style basicRock() noexcept { return {}; }

    static Style blues() noexcept {
        Style s;
        s.kickBeat3 = 0.52f;
        s.extraKickProbability = 0.04f;
        s.snareBackbeat = 0.94f;
        s.closedHatProbability = 0.78f;
        s.crashOnSectionStart = 0.58f;
        s.humanizeMs = 8.0f;
        s.snareLaybackMs = 6.0f; s.hatLaybackMs = 2.0f; s.fillVocabulary = FillVocabulary::SnareRoll; s.fillFrequency = 0.8f;
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
        s.snareLaybackMs = 2.0f; s.hatTightness = 0.4f; s.fillVocabulary = FillVocabulary::Mixed; s.fillFrequency = 0.9f;
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
        s.snareTightness = 0.7f; s.fillVocabulary = FillVocabulary::KickCrash; s.fillFrequency = 1.2f;
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
        s.snareTightness = 0.6f; s.kickTightness = 0.5f; s.fillVocabulary = FillVocabulary::KickCrash; s.fillFrequency = 1.1f;
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

    // ---- Additional styles (SPEC section 20) ----
    static Style hardRock() noexcept {
        Style s;
        s.kickBeat3 = 0.82f; s.extraKickProbability = 0.16f; s.closedHatProbability = 0.97f;
        s.crashOnSectionStart = 0.90f; s.humanizeMs = 3.0f; s.openHatProbability = 0.05f;
        s.snareLaybackMs = 2.0f; s.fillVocabulary = FillVocabulary::TomRun; s.fillFrequency = 1.1f;
        return s;
    }
    static Style classicRock() noexcept {
        Style s;
        s.kickBeat3 = 0.62f; s.extraKickProbability = 0.10f; s.closedHatProbability = 0.90f;
        s.crashOnSectionStart = 0.70f; s.humanizeMs = 6.0f; s.openHatProbability = 0.04f;
        s.rideProbability = 0.05f; s.ghostSnareProbability = 0.05f;
        s.snareLaybackMs = 4.0f; s.hatLaybackMs = 1.0f; s.fillVocabulary = FillVocabulary::TomRun;
        return s;
    }
    static Style alternative() noexcept {
        Style s;
        s.kickBeat3 = 0.60f; s.extraKickProbability = 0.14f; s.closedHatProbability = 0.88f;
        s.crashOnSectionStart = 0.62f; s.humanizeMs = 6.0f; s.openHatProbability = 0.08f;
        s.rideProbability = 0.06f; s.ghostSnareProbability = 0.07f; s.syncopation = 0.20f;
        s.snareLaybackMs = 2.0f; s.hatVariation = 0.15f; s.fillVocabulary = FillVocabulary::Mixed;
        s.fillFrequency = 0.9f;
        return s;
    }
    static Style grunge() noexcept {
        Style s;
        s.kickBeat1 = 0.97f; s.kickBeat3 = 0.66f; s.extraKickProbability = 0.10f;
        s.closedHatProbability = 0.80f; s.crashOnSectionStart = 0.85f; s.humanizeMs = 8.0f;
        s.openHatProbability = 0.14f; s.rideProbability = 0.04f; s.ghostSnareProbability = 0.03f;
        s.snareLaybackMs = 6.0f; s.kickLaybackMs = 2.0f; s.hatLaybackMs = 2.0f;
        s.hatTightness = 0.9f; s.fillVocabulary = FillVocabulary::SnareRoll; s.fillFrequency = 0.8f;
        return s;
    }
    static Style soul() noexcept {
        Style s;
        s.kickBeat3 = 0.40f; s.extraKickProbability = 0.14f; s.snareBackbeat = 0.98f;
        s.closedHatProbability = 0.95f; s.crashOnSectionStart = 0.45f; s.humanizeMs = 6.0f;
        s.openHatProbability = 0.03f; s.ghostSnareProbability = 0.16f; s.swing = 0.12f;
        s.sidestickProbability = 0.15f; s.snareLaybackMs = 6.0f; s.hatLaybackMs = 1.0f;
        s.hatTightness = 0.5f; s.fillVocabulary = FillVocabulary::SnareRoll; s.fillFrequency = 0.8f;
        return s;
    }
    static Style pop() noexcept {
        Style s;
        s.kickBeat1 = 0.98f; s.kickBeat3 = 0.55f; s.extraKickProbability = 0.12f;
        s.closedHatProbability = 0.98f; s.crashOnSectionStart = 0.85f; s.humanizeMs = 2.5f;
        s.openHatProbability = 0.05f; s.ghostSnareProbability = 0.02f; s.sidestickProbability = 0.10f;
        s.hatTightness = 0.4f; s.snareTightness = 0.7f; s.fillVocabulary = FillVocabulary::SnareRoll;
        s.fillFrequency = 0.7f;
        return s;
    }
    static Style indie() noexcept {
        Style s;
        s.kickBeat3 = 0.55f; s.extraKickProbability = 0.12f; s.closedHatProbability = 0.85f;
        s.crashOnSectionStart = 0.55f; s.humanizeMs = 7.0f; s.openHatProbability = 0.07f;
        s.rideProbability = 0.08f; s.ghostSnareProbability = 0.08f; s.syncopation = 0.12f;
        s.sidestickProbability = 0.08f; s.snareLaybackMs = 3.0f; s.hatVariation = 0.10f;
        s.fillVocabulary = FillVocabulary::Mixed; s.fillFrequency = 0.8f;
        return s;
    }
    static Style garageRock() noexcept {
        Style s;
        s.kickBeat3 = 0.80f; s.extraKickProbability = 0.12f; s.closedHatProbability = 0.90f;
        s.crashOnSectionStart = 0.80f; s.humanizeMs = 10.0f; s.openHatProbability = 0.10f;
        s.ghostSnareProbability = 0.03f; s.snareLaybackMs = -2.0f; s.hatTightness = 1.0f;
        s.snareTightness = 1.3f; s.kickTightness = 1.2f; s.fillVocabulary = FillVocabulary::TomRun;
        s.fillFrequency = 1.1f;
        return s;
    }
    static Style country() noexcept {
        Style s;
        s.kickBeat3 = 0.90f; s.extraKickProbability = 0.04f; s.snareBackbeat = 0.96f;
        s.closedHatProbability = 0.92f; s.crashOnSectionStart = 0.60f; s.humanizeMs = 5.0f;
        s.openHatProbability = 0.02f; s.ghostSnareProbability = 0.10f; s.trainBeat = true;
        s.sidestickProbability = 0.10f; s.snareLaybackMs = 1.0f; s.fillVocabulary = FillVocabulary::SnareRoll;
        s.fillFrequency = 0.9f;
        return s;
    }
    static Style reggae() noexcept {
        Style s;
        s.kickBeat1 = 0.10f; s.kickBeat3 = 1.0f; s.extraKickProbability = 0.02f;
        s.snareBackbeat = 1.0f; s.closedHatProbability = 0.97f; s.crashOnSectionStart = 0.30f;
        s.humanizeMs = 6.0f; s.openHatProbability = 0.0f; s.ghostSnareProbability = 0.0f;
        s.oneDrop = true; s.sidestickProbability = 0.70f; s.swing = 0.10f;
        s.snareLaybackMs = 8.0f; s.kickLaybackMs = 5.0f; s.hatLaybackMs = 3.0f; s.hatTightness = 0.5f;
        s.fillVocabulary = FillVocabulary::SnareRoll; s.fillFrequency = 0.5f; s.fillCrashProbability = 0.4f;
        return s;
    }
    static Style disco() noexcept {
        Style s;
        s.kickBeat1 = 1.0f; s.kickBeat3 = 1.0f; s.extraKickProbability = 0.0f;
        s.closedHatProbability = 0.95f; s.crashOnSectionStart = 0.75f; s.humanizeMs = 1.5f;
        s.openHatProbability = 0.0f; s.ghostSnareProbability = 0.04f; s.fourOnFloor = true;
        s.offbeatOpenHat = 0.90f; s.hatTightness = 0.3f; s.snareTightness = 0.5f; s.kickTightness = 0.4f;
        s.fillVocabulary = FillVocabulary::SnareRoll; s.fillFrequency = 0.8f;
        return s;
    }
    static Style electronicRock() noexcept {
        Style s;
        s.kickBeat1 = 1.0f; s.kickBeat3 = 1.0f; s.extraKickProbability = 0.08f;
        s.snareBackbeat = 1.0f; s.closedHatProbability = 0.99f; s.crashOnSectionStart = 0.90f;
        s.humanizeMs = 1.0f; s.openHatProbability = 0.0f; s.ghostSnareProbability = 0.0f;
        s.fourOnFloor = true; s.offbeatOpenHat = 0.35f; s.hatTightness = 0.2f; s.snareTightness = 0.4f;
        s.kickTightness = 0.3f; s.fillVocabulary = FillVocabulary::KickCrash; s.fillFrequency = 0.9f;
        return s;
    }
    static Style breakbeat() noexcept {
        Style s;
        s.kickBeat1 = 0.98f; s.kickBeat3 = 0.30f; s.extraKickProbability = 0.28f;
        s.snareBackbeat = 0.96f; s.closedHatProbability = 0.92f; s.crashOnSectionStart = 0.55f;
        s.humanizeMs = 4.0f; s.openHatProbability = 0.06f; s.ghostSnareProbability = 0.22f;
        s.syncopation = 0.60f; s.hatVariation = 0.30f;
        s.fillVocabulary = FillVocabulary::Mixed; s.fillFrequency = 1.2f;
        return s;
    }
    static Style halfTime() noexcept {
        Style s;
        s.kickBeat1 = 1.0f; s.kickBeat3 = 0.12f; s.extraKickProbability = 0.14f;
        s.snareBackbeat = 1.0f; s.closedHatProbability = 0.92f; s.crashOnSectionStart = 0.85f;
        s.humanizeMs = 5.0f; s.openHatProbability = 0.05f; s.ghostSnareProbability = 0.08f;
        s.halfTimeSnare = true; s.syncopation = 0.15f; s.snareLaybackMs = 5.0f;
        s.fillVocabulary = FillVocabulary::TomRun; s.fillFrequency = 0.8f;
        return s;
    }
    static Style experimental() noexcept {
        Style s;
        s.kickBeat1 = 0.80f; s.kickBeat3 = 0.50f; s.extraKickProbability = 0.22f;
        s.snareBackbeat = 0.85f; s.closedHatProbability = 0.70f; s.crashOnSectionStart = 0.50f;
        s.humanizeMs = 9.0f; s.openHatProbability = 0.15f; s.rideProbability = 0.15f;
        s.ghostSnareProbability = 0.18f; s.syncopation = 0.70f; s.hatVariation = 0.60f;
        s.hatTightness = 1.2f; s.snareTightness = 1.3f;
        s.fillVocabulary = FillVocabulary::Mixed; s.fillFrequency = 1.3f; s.fillCrashProbability = 0.5f;
        return s;
    }
    static Style jazzSwing() noexcept {
        Style s;
        s.kickBeat1 = 0.45f; s.kickBeat3 = 0.35f; s.extraKickProbability = 0.05f;
        s.snareBackbeat = 0.30f; s.closedHatProbability = 1.0f; s.crashOnSectionStart = 0.25f;
        s.humanizeMs = 7.0f; s.openHatProbability = 0.0f; s.ghostSnareProbability = 0.22f;
        s.swing = 0.33f; s.jazzRide = true;
        s.snareLaybackMs = 5.0f; s.kickLaybackMs = 2.0f; s.hatLaybackMs = 3.0f; s.hatTightness = 0.7f;
        s.fillVocabulary = FillVocabulary::SnareRoll; s.fillFrequency = 0.8f; s.fillCrashProbability = 0.3f;
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
    float complexity{0.5f}; // 0-1; 0.5 = the style's own density, lower thins, higher adds syncopation/ghosts/hat variation
};

} // namespace robodrummer
