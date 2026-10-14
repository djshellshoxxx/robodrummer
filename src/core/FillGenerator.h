// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "core/DrumEvent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

enum class FillLength : std::uint8_t { OneBeat, TwoBeats, OneBar, TwoBars };

struct FillSettings {
    FillLength length{FillLength::OneBeat};
    float complexity{0.5f};  // 0 = simple snare/tom runs, 1 = dense with kick doubles
    float surprise{0.0f};    // 0 = classic descending run, 1 = unpredictable voice choice
};

struct FillHit {
    double beatOffset{0.0};  // beats from the start of the fill
    DrumInstrument instrument{DrumInstrument::Snare};
    float velocity{0.7f};
};

class FillGenerator {
public:
    static constexpr std::size_t MaxHits = 256;

    [[nodiscard]] static int lengthBeats(FillLength length, int beatsPerBar) noexcept {
        beatsPerBar = std::clamp(beatsPerBar, 1, 12);
        switch (length) {
            case FillLength::OneBeat: return 1;
            case FillLength::TwoBeats: return std::min(2, beatsPerBar);
            case FillLength::OneBar: return beatsPerBar;
            case FillLength::TwoBars: return beatsPerBar * 2;
        }
        return 1;
    }

    // Longer fills end with a crash that lands on the following downbeat.
    [[nodiscard]] static bool endsWithCrash(int totalBeats) noexcept { return totalBeats >= 2; }

    // The final beat uses the full density for the requested strength (matching the classic one-beat fill);
    // earlier beats of a longer fill build towards it.
    [[nodiscard]] static std::size_t generate(const FillSettings& settings, float strength, int totalBeats,
                                              std::uint32_t seed, std::array<FillHit, MaxHits>& out) noexcept {
        strength = std::clamp(strength, 0.0f, 1.0f);
        totalBeats = std::clamp(totalBeats, 1, 24);
        const float complexity = std::clamp(settings.complexity, 0.0f, 1.0f);
        const float surprise = std::clamp(settings.surprise, 0.0f, 1.0f);

        static constexpr std::array<DrumInstrument, 8> run {
            DrumInstrument::Snare, DrumInstrument::HighTom, DrumInstrument::HighTom, DrumInstrument::MidTom,
            DrumInstrument::MidTom, DrumInstrument::FloorTom, DrumInstrument::FloorTom, DrumInstrument::Snare
        };
        static constexpr std::array<DrumInstrument, 6> classic {
            DrumInstrument::Snare, DrumInstrument::HighTom, DrumInstrument::MidTom,
            DrumInstrument::FloorTom, DrumInstrument::MidTom, DrumInstrument::Snare
        };
        static constexpr std::array<DrumInstrument, 4> wild {
            DrumInstrument::Snare, DrumInstrument::HighTom, DrumInstrument::MidTom, DrumInstrument::FloorTom
        };

        const int fullSteps = std::clamp(2 + static_cast<int>(std::lround(strength * 4.0f * (0.5f + complexity))), 2, 8);
        std::size_t count = 0;
        for (int beat = 0; beat < totalBeats; ++beat) {
            const int remaining = totalBeats - 1 - beat;
            const int steps = std::max(2, fullSteps - remaining * 2);
            const double stepBeats = 1.0 / static_cast<double>(steps);
            for (int step = 0; step < steps && count < out.size(); ++step) {
                DrumInstrument voice = steps <= 6 ? classic[static_cast<std::size_t>(step)]
                                                  : run[static_cast<std::size_t>(step)];
                if (surprise > 0.0f && next01(seed) < surprise * 0.7f)
                    voice = wild[static_cast<std::size_t>(next01(seed) * static_cast<float>(wild.size())) % wild.size()];
                const float build = totalBeats > 1 ? 0.08f * static_cast<float>(beat) / static_cast<float>(totalBeats - 1) : 0.0f;
                const float velocity = std::clamp(0.58f + strength * 0.30f + 0.02f * static_cast<float>(step) + build, 0.0f, 1.0f);
                out[count++] = {beat + stepBeats * step, voice, velocity};

                // Dense fills double the kick under the odd-numbered hits of the last half of the run.
                if (complexity > 0.7f && step % 2 == 1 && step * 2 >= steps && count < out.size())
                    out[count++] = {beat + stepBeats * step, DrumInstrument::Kick, std::clamp(velocity - 0.18f, 0.0f, 1.0f)};
            }
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
