#pragma once
#include "core/DrumEvent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace robodrummer {

enum class FillLength {
    OneBeat,
    TwoBeats,
    OneBar,
    TwoBars,
    LongTransition
};

[[nodiscard]] inline double fillLengthBeats(FillLength length, int numerator) noexcept {
    const double barBeats = static_cast<double>(std::max(1, numerator));
    switch (length) {
        case FillLength::OneBeat: return 1.0;
        case FillLength::TwoBeats: return 2.0;
        case FillLength::OneBar: return barBeats;
        case FillLength::TwoBars: return barBeats * 2.0;
        case FillLength::LongTransition: return barBeats * 2.0;
    }
    return 1.0;
}

struct FillContext {
    double sampleRate{48000.0};
    double bpm{120.0};
    int numerator{4};
    int denominator{4};
    float strength{0.75f};
    FillLength length{FillLength::OneBeat};
};

class FillGenerator {
public:
    template <std::size_t Capacity>
    [[nodiscard]] std::size_t generate(const FillContext& context,
                                       std::array<DrumEvent, Capacity>& out) const noexcept {
        if (!(context.sampleRate > 0.0) || !(context.bpm > 0.0) || Capacity == 0)
            return 0;

        const double samplesPerQuarter = context.sampleRate * 60.0 / context.bpm;
        const double samplesPerBeat = samplesPerQuarter * (4.0 / static_cast<double>(std::max(1, context.denominator)));
        const double durationBeats = fillLengthBeats(context.length, context.numerator);
        const float strength = std::clamp(context.strength, 0.0f, 1.0f);
        const int stepsPerBeat = std::clamp(2 + static_cast<int>(std::lround(strength * 2.0f)), 2, 4);
        const int totalSteps = std::max(1, static_cast<int>(std::lround(durationBeats * stepsPerBeat)));

        static constexpr std::array<DrumInstrument, 8> voices {
            DrumInstrument::Snare,
            DrumInstrument::HighTom,
            DrumInstrument::MidTom,
            DrumInstrument::FloorTom,
            DrumInstrument::MidTom,
            DrumInstrument::HighTom,
            DrumInstrument::Snare,
            DrumInstrument::FloorTom
        };

        std::size_t count = 0;
        for (int step = 0; step < totalSteps && count < Capacity; ++step) {
            const double beatPosition = static_cast<double>(step) / static_cast<double>(stepsPerBeat);
            const int offset = static_cast<int>(std::llround(beatPosition * samplesPerBeat));
            const float progress = totalSteps > 1
                ? static_cast<float>(step) / static_cast<float>(totalSteps - 1)
                : 1.0f;
            const float velocity = std::clamp(0.50f + strength * 0.28f + progress * 0.18f, 0.0f, 1.0f);
            out[count++] = {
                voices[static_cast<std::size_t>(step) % voices.size()],
                offset,
                velocity,
                static_cast<std::uint32_t>(count - 1)
            };
        }

        if (context.length == FillLength::LongTransition && count < Capacity) {
            const int offset = static_cast<int>(std::llround(
                std::max(0.0, durationBeats - 0.125) * samplesPerBeat));
            out[count++] = {
                DrumInstrument::Crash,
                offset,
                std::clamp(0.85f + strength * 0.15f, 0.0f, 1.0f),
                static_cast<std::uint32_t>(count - 1)
            };
        }

        return count;
    }
};

} // namespace robodrummer
