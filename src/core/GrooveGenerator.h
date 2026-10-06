#pragma once
#include "DrumEvent.h"
#include "MeterProfile.h"
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

        auto chance = [&](float probability) {
            return next01(context.seed) < std::clamp(probability, 0.0f, 1.0f);
        };

        auto addHat = [&](double beatPos, float velocity) {
            if (!chance(style.closedHatProbability)) return;
            if (chance(style.rideProbability)) {
                add(DrumInstrument::Ride, beatPos, velocity + 0.04f);
            } else if (chance(style.openHatProbability)) {
                add(DrumInstrument::OpenHat, beatPos, velocity + 0.02f);
            } else {
                add(DrumInstrument::ClosedHat, beatPos, velocity);
            }
        };

        if (context.sectionStart && chance(style.crashOnSectionStart))
            add(DrumInstrument::Crash, 0.0, 0.9f);

        const auto meter = MeterProfile::forMeter(context.numerator, context.denominator);
        const int hatHits = context.denominator >= 8
            ? 1
            : std::clamp(style.hatHitsPerBeat, 1, 4);

        for (int beat = 0; beat < context.numerator; ++beat) {
            const auto beatIndex = static_cast<std::size_t>(beat);

            const bool oneDropAnchor = style.oneDrop && context.numerator == 4 && context.denominator == 4 && beat == 2;
            const bool kickAnchor = style.oneDrop
                ? oneDropAnchor
                : (meter.kickAnchor[beatIndex] || style.fourOnFloorKick);
            if (kickAnchor) {
                const float probability = beat == 0 ? style.kickBeat1 : style.kickBeat3;
                if (chance(probability))
                    add(DrumInstrument::Kick, beat, (beat == 0 ? 0.75f : 0.70f) + 0.2f * context.intensity);
            }

            bool snareAnchor = meter.backbeat[beatIndex];
            if (style.oneDrop && context.numerator == 4 && context.denominator == 4)
                snareAnchor = oneDropAnchor;
            else if (style.halfTimeBackbeat && context.numerator == 4 && context.denominator == 4)
                snareAnchor = beat == 2;

            if (snareAnchor && chance(style.snareBackbeat))
                add(DrumInstrument::Snare, beat, 0.78f + 0.18f * context.intensity);

            const float extraKick = style.extraKickProbability * (0.25f + 1.5f * context.intensity);
            if (chance(extraKick))
                add(DrumInstrument::Kick, beat + 0.5, 0.55f + 0.3f * context.intensity);

            if (meter.backbeat[beatIndex] && chance(style.ghostSnareProbability * (0.4f + context.intensity)))
                add(DrumInstrument::Snare, beat + 0.75, 0.28f + 0.18f * context.intensity);

            for (int hit = 0; hit < hatHits; ++hit) {
                double position = static_cast<double>(hit) / static_cast<double>(hatHits);
                if (hatHits == 2 && hit == 1)
                    position += std::clamp(static_cast<double>(style.swing), 0.0, 0.45) * 0.5;

                const bool groupAccent = meter.kickAnchor[beatIndex];
                const float accent = hit == 0 ? (groupAccent ? 0.14f : 0.08f) : -0.04f;
                addHat(static_cast<double>(beat) + position,
                       0.52f + accent + 0.25f * context.intensity);
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
