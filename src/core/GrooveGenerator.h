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
        const bool straightBeats = context.denominator < 8;
        const int midBeat = context.numerator / 2;
        // Complexity: 0.5 is neutral (legacy behavior); d in [-1, 1] thins or enriches the groove.
        const float d = std::clamp(context.complexity, 0.0f, 1.0f) * 2.0f - 1.0f;
        const float dPos = std::max(0.0f, d);
        const bool complexityActive = std::abs(d) > 1.0e-4f;
        const float hatVariation = std::clamp(style.hatVariation + 0.5f * dPos, 0.0f, 1.0f);
        const float syncopation = std::clamp(style.syncopation + 0.6f * dPos, 0.0f, 1.0f);

        for (int beat = 0; beat < context.numerator; ++beat) {
            const auto beatIndex = static_cast<std::size_t>(beat);
            const bool snareBeat = (style.oneDrop || style.halfTimeSnare) && context.numerator >= 2
                ? beat == midBeat
                : meter.backbeat[beatIndex];

            if (style.jazzRide) {
                // Feathered kick: soft quarter-note pulse.
                if (chance(0.55f))
                    add(DrumInstrument::Kick, beat, 0.24f + 0.12f * context.intensity);
            } else if (style.fourOnFloor && straightBeats) {
                if (chance(beat == 0 ? style.kickBeat1 : style.kickBeat3))
                    add(DrumInstrument::Kick, beat, (beat == 0 ? 0.78f : 0.72f) + 0.2f * context.intensity);
            } else if (meter.kickAnchor[beatIndex]) {
                const float probability = beat == 0 ? style.kickBeat1 : style.kickBeat3;
                if (chance(probability))
                    add(DrumInstrument::Kick, beat, (beat == 0 ? 0.75f : 0.70f) + 0.2f * context.intensity);
            }

            if (snareBeat && chance(style.snareBackbeat)) {
                float vel = 0.78f + 0.18f * context.intensity;
                if (style.jazzRide) vel *= 0.55f;
                if (style.sidestickProbability > 0.0f && chance(style.sidestickProbability))
                    add(DrumInstrument::Sidestick, beat, vel - 0.08f);
                else
                    add(DrumInstrument::Snare, beat, vel);
            }

            float extraKick = style.extraKickProbability * (0.25f + 1.5f * context.intensity);
            if (complexityActive && d < 0.0f) extraKick *= 1.0f + 0.8f * d;
            if (chance(extraKick))
                add(DrumInstrument::Kick, beat + 0.5, 0.55f + 0.3f * context.intensity);

            if (syncopation > 0.0f && !style.jazzRide && chance(syncopation * 0.30f * (0.4f + context.intensity)))
                add(DrumInstrument::Kick, beat + (beat % 2 == 0 ? 0.75 : 0.25), 0.50f + 0.25f * context.intensity);

            float ghost = style.ghostSnareProbability * (0.4f + context.intensity);
            if (complexityActive) ghost = ghost * (1.0f + d) + 0.06f * dPos;
            if (snareBeat && chance(ghost))
                add(DrumInstrument::Snare, beat + 0.75, 0.28f + 0.18f * context.intensity);

            if (style.trainBeat && !snareBeat && chance(0.7f))
                add(DrumInstrument::Snare, beat + 0.5, 0.30f + 0.15f * context.intensity);

            if (style.jazzRide) {
                const double swingPos = 0.5 + std::clamp(static_cast<double>(style.swing), 0.0, 0.45) * 0.5;
                add(DrumInstrument::Ride, beat, 0.55f + 0.22f * context.intensity);
                if (beat % 2 == 1) {
                    add(DrumInstrument::Ride, beat + swingPos, 0.40f + 0.18f * context.intensity);
                    add(DrumInstrument::ClosedHat, beat, 0.30f + 0.12f * context.intensity);
                }
                continue;
            }

            for (int hit = 0; hit < hatHits; ++hit) {
                double position = static_cast<double>(hit) / static_cast<double>(hatHits);
                if (hatHits == 2 && hit == 1)
                    position += std::clamp(static_cast<double>(style.swing), 0.0, 0.45) * 0.5;

                const bool groupAccent = meter.kickAnchor[beatIndex];
                const float accent = hit == 0 ? (groupAccent ? 0.14f : 0.08f) : -0.04f;
                const float velocity = 0.52f + accent + 0.25f * context.intensity;

                if (style.offbeatOpenHat > 0.0f && hatHits == 2 && hit == 1 && chance(style.offbeatOpenHat)) {
                    add(DrumInstrument::OpenHat, static_cast<double>(beat) + position, velocity + 0.06f);
                    continue;
                }
                if (complexityActive && d < 0.0f && !chance(1.0f + 0.35f * d) && hit == 1)
                    continue; // simpler: occasionally drop weak off-beat hats
                float v = velocity;
                if (hatVariation > 0.0f && chance(hatVariation * 0.5f))
                    v += hit == 0 ? -0.05f : 0.09f;
                addHat(static_cast<double>(beat) + position, v);
                if (hatVariation > 0.0f && hatHits == 2 && chance(hatVariation * 0.35f))
                    addHat(static_cast<double>(beat) + position + 0.25, velocity - 0.16f);
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
