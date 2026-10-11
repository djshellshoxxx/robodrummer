// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "core/DrumEvent.h"
#include "core/Style.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

enum class FillLength : std::uint8_t { OneBeat, TwoBeats, OneBar, TwoBars };

inline constexpr std::size_t MaxFillNotes = 96;

struct FillNote {
    DrumInstrument instrument{DrumInstrument::Snare};
    double beat{0.0}; // beats from the start of the fill
    float velocity{0.7f};
};

struct FillParams {
    FillLength length{FillLength::OneBeat};
    int numerator{4};
    float strength{0.75f};    // 0-1 loudness / density when complexity is automatic
    float complexity{-1.0f};  // 0-1; negative = automatic (follows strength)
    FillVocabulary vocabulary{FillVocabulary::TomRun};
    std::uint32_t seed{1};
};

[[nodiscard]] inline int fillLengthBeats(FillLength length, int numerator) noexcept {
    const int bar = std::max(1, numerator);
    switch (length) {
        case FillLength::OneBeat: return 1;
        case FillLength::TwoBeats: return std::min(2, bar);
        case FillLength::OneBar: return bar;
        case FillLength::TwoBars: return bar * 2;
    }
    return 1;
}

[[nodiscard]] inline int fillLengthBars(FillLength length, int numerator) noexcept {
    const int beats = fillLengthBeats(length, numerator);
    return std::max(1, (beats + std::max(1, numerator) - 1) / std::max(1, numerator));
}

class FillGenerator {
public:
    // Writes the fill's notes (sorted by beat) and returns how many were produced. Deterministic.
    [[nodiscard]] static std::size_t generate(const FillParams& p, std::array<FillNote, MaxFillNotes>& out) noexcept {
        const float strength = std::clamp(p.strength, 0.0f, 1.0f);
        const bool automatic = !(p.complexity >= 0.0f);
        const float c = automatic ? strength : std::clamp(p.complexity, 0.0f, 1.0f);
        const int beats = fillLengthBeats(p.length, p.numerator);
        const bool legacy = automatic && beats == 1;

        int perBeat = std::clamp(2 + static_cast<int>(std::lround(c * 4.0f)), 2, 6);
        if (!legacy && perBeat == 5) perBeat = 4;
        const int total = std::min(beats * perBeat, static_cast<int>(MaxFillNotes));

        static constexpr std::array<DrumInstrument, 6> tomRun {
            DrumInstrument::Snare, DrumInstrument::HighTom, DrumInstrument::MidTom,
            DrumInstrument::FloorTom, DrumInstrument::MidTom, DrumInstrument::Snare
        };
        std::uint32_t rng = p.seed * 2246822519u + 0x9E3779B9u;
        const float base = 0.58f + strength * 0.30f;
        std::size_t count = 0;

        for (int i = 0; i < total; ++i) {
            const bool last = i == total - 1;
            const float progress = total > 1 ? static_cast<float>(i) / static_cast<float>(total - 1) : 1.0f;
            if (!automatic && i > 0 && !last && next01(rng) < (1.0f - c) * 0.4f) continue;

            DrumInstrument voice = DrumInstrument::Snare;
            switch (p.vocabulary) {
                case FillVocabulary::TomRun:
                    voice = legacy ? tomRun[static_cast<std::size_t>(std::min(i, 5))]
                                   : tomRun[static_cast<std::size_t>(std::min(5, i * 6 / total))];
                    break;
                case FillVocabulary::SnareRoll:
                    voice = (last && c > 0.65f && beats > 1) ? DrumInstrument::FloorTom : DrumInstrument::Snare;
                    break;
                case FillVocabulary::KickCrash:
                    voice = (i % 2 == 0) ? DrumInstrument::Snare : DrumInstrument::Kick;
                    if (i >= total - 2 && total >= 4) voice = DrumInstrument::Kick;
                    break;
                case FillVocabulary::Mixed: {
                    static constexpr std::array<DrumInstrument, 4> cell {
                        DrumInstrument::Snare, DrumInstrument::HighTom, DrumInstrument::Snare, DrumInstrument::MidTom
                    };
                    voice = cell[static_cast<std::size_t>(i % 4)];
                    if (i % perBeat == 0 && progress < 0.5f && beats > 1) voice = DrumInstrument::Kick;
                    if (i == total - 1 && total >= 3) voice = DrumInstrument::FloorTom;
                    break;
                }
            }

            float velocity;
            if (legacy) {
                velocity = base + 0.02f * static_cast<float>(i);
            } else if (p.vocabulary == FillVocabulary::SnareRoll) {
                velocity = base * 0.72f + 0.30f * progress; // crescendo roll
            } else {
                velocity = base + 0.16f * progress;
                if (voice == DrumInstrument::Kick) velocity -= 0.06f;
            }
            if (count >= out.size()) break;
            out[count++] = {voice, static_cast<double>(i) / static_cast<double>(perBeat), std::clamp(velocity, 0.0f, 1.0f)};
        }
        return count;
    }

private:
    static float next01(std::uint32_t& state) noexcept {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>((state >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }
};

} // namespace robodrummer
