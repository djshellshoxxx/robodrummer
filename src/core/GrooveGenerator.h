#pragma once
#include "DrumEvent.h"
#include "Style.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace robodrummer {
class GrooveGenerator {
public:
    template <std::size_t Capacity>
    [[nodiscard]] std::size_t generateBar(const Style& style, GrooveContext context, std::array<DrumEvent, Capacity>& out) const noexcept {
        if (!(context.sampleRate > 0.0) || !(context.bpm > 0.0) || context.numerator <= 0 || Capacity == 0) return 0;
        context.intensity = std::clamp(context.intensity, 0.0f, 1.0f);
        const double samplesPerQuarter = context.sampleRate * 60.0 / context.bpm;
        const double samplesPerBeat = samplesPerQuarter * (4.0 / static_cast<double>(context.denominator));
        const int barSamples = static_cast<int>(std::llround(samplesPerBeat * context.numerator));
        std::size_t count = 0;
        auto add = [&](DrumInstrument instrument, double beatPos, float velocity) {
            if (count >= Capacity) return;
            const int offset = static_cast<int>(std::llround(beatPos * samplesPerBeat));
            if (offset < 0 || offset >= barSamples) return;
            out[count++] = {instrument, offset, std::clamp(velocity, 0.0f, 1.0f), static_cast<std::uint32_t>(count - 1)};
        };
        auto chance = [&](float probability) { return next01(context.seed) < std::clamp(probability, 0.0f, 1.0f); };
        if (context.sectionStart && chance(style.crashOnSectionStart)) add(DrumInstrument::Crash, 0.0, 0.9f);
        for (int beat = 0; beat < context.numerator; ++beat) {
            if (chance(style.closedHatProbability)) add(DrumInstrument::ClosedHat, static_cast<double>(beat), 0.55f + 0.25f * context.intensity);
            if (beat == 0 && chance(style.kickBeat1)) add(DrumInstrument::Kick, beat, 0.75f + 0.2f * context.intensity);
            if (beat == 2 && context.numerator >= 4 && chance(style.kickBeat3)) add(DrumInstrument::Kick, beat, 0.7f + 0.2f * context.intensity);
            if ((beat == 1 || beat == 3) && chance(style.snareBackbeat)) add(DrumInstrument::Snare, beat, 0.78f + 0.18f * context.intensity);
            const float extraKick = style.extraKickProbability * (0.25f + 1.5f * context.intensity);
            if (chance(extraKick)) add(DrumInstrument::Kick, beat + 0.5, 0.55f + 0.3f * context.intensity);
        }
        return count;
    }
private:
    static float next01(std::uint32_t& state) noexcept {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>((state >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }
};
}
