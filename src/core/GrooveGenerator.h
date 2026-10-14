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

        const float complexityScale = 0.5f + std::clamp(context.complexity, 0.0f, 1.0f);
        const float intensity = context.intensity;
        const bool fourFour = context.numerator == 4 && context.denominator == 4;
        const GroovePattern pattern = fourFour ? style.pattern : GroovePattern::Standard;

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

        if (context.sectionStart && chance(style.crashOnSectionStart * std::clamp(context.crashScale, 0.0f, 2.0f)))
            add(DrumInstrument::Crash, 0.0, 0.9f);

        // Sixteenth-note kick syncopation shared by every pattern; drawn only when the style asks for it so
        // styles without it keep their exact random sequence.
        auto sixteenthKicks = [&](int beats) {
            if (style.sixteenthKick <= 0.0f) return;
            for (int beat = 0; beat < beats; ++beat) {
                const float p = style.sixteenthKick * complexityScale * (0.25f + 1.5f * intensity);
                if (chance(p)) add(DrumInstrument::Kick, beat + 0.25, 0.50f + 0.25f * intensity);
                if (chance(p)) add(DrumInstrument::Kick, beat + 0.75, 0.50f + 0.25f * intensity);
            }
        };

        const auto swingOffset = [&](int hit, int hits) {
            double position = static_cast<double>(hit) / static_cast<double>(hits);
            if (hits == 2 && hit == 1)
                position += std::clamp(static_cast<double>(style.swing), 0.0, 0.45) * 0.5;
            return position;
        };

        switch (pattern) {
        case GroovePattern::FourOnFloor: {
            for (int beat = 0; beat < 4; ++beat) {
                const float p = beat == 0 ? style.kickBeat1 : std::max(style.kickBeat3, 0.9f);
                if (chance(p)) add(DrumInstrument::Kick, beat, 0.78f + 0.18f * intensity);
                if ((beat == 1 || beat == 3) && chance(style.snareBackbeat))
                    add(DrumInstrument::Snare, beat, 0.78f + 0.18f * intensity);
                if (chance(style.closedHatProbability))
                    add(DrumInstrument::ClosedHat, beat, 0.50f + 0.20f * intensity);
                // The disco offbeat open hat.
                if (chance(style.openHatProbability))
                    add(DrumInstrument::OpenHat, beat + 0.5, 0.58f + 0.2f * intensity);
                else if (chance(style.closedHatProbability))
                    add(DrumInstrument::ClosedHat, beat + 0.5, 0.42f + 0.2f * intensity);
            }
            sixteenthKicks(4);
            break;
        }
        case GroovePattern::OneDrop: {
            // Reggae one drop: the weight (kick + cross-stick) lands on beat 3 only.
            if (chance(style.kickBeat3)) add(DrumInstrument::Kick, 2.0, 0.72f + 0.2f * intensity);
            if (chance(style.snareBackbeat)) add(DrumInstrument::Rimshot, 2.0, 0.68f + 0.2f * intensity);
            for (int beat = 0; beat < 4; ++beat)
                for (int hit = 0; hit < 2; ++hit)
                    addHat(beat + swingOffset(hit, 2), (hit == 0 ? 0.50f : 0.40f) + 0.22f * intensity);
            break;
        }
        case GroovePattern::HalfTime: {
            if (chance(style.kickBeat1)) add(DrumInstrument::Kick, 0.0, 0.78f + 0.2f * intensity);
            for (int beat = 0; beat < 4; ++beat) {
                if (chance(style.extraKickProbability * complexityScale * (0.25f + 1.5f * intensity)))
                    add(DrumInstrument::Kick, beat + (beat == 2 ? 0.75 : 0.5), 0.55f + 0.25f * intensity);
                for (int hit = 0; hit < 2; ++hit)
                    addHat(beat + swingOffset(hit, 2), (hit == 0 ? 0.52f : 0.44f) + 0.22f * intensity);
            }
            if (chance(style.snareBackbeat)) add(DrumInstrument::Snare, 2.0, 0.82f + 0.16f * intensity);
            if (chance(style.ghostSnareProbability * complexityScale * (0.4f + intensity)))
                add(DrumInstrument::Snare, 2.75, 0.28f + 0.16f * intensity);
            sixteenthKicks(4);
            break;
        }
        case GroovePattern::Breakbeat: {
            struct KickSpot { double pos; float probability; };
            const std::array<KickSpot, 4> spots {{ {0.0, style.kickBeat1}, {1.5, 0.50f * complexityScale},
                                                    {2.25, 0.60f * complexityScale}, {3.5, 0.40f * complexityScale} }};
            for (const auto& spot : spots)
                if (chance(spot.probability)) add(DrumInstrument::Kick, spot.pos, 0.62f + 0.3f * intensity);
            for (int beat = 1; beat < 4; beat += 2)
                if (chance(style.snareBackbeat)) add(DrumInstrument::Snare, beat, 0.80f + 0.16f * intensity);
            for (const double pos : { 0.75, 1.75, 2.75, 3.25 })
                if (chance(style.ghostSnareProbability * complexityScale * (0.4f + intensity)))
                    add(DrumInstrument::Snare, pos, 0.26f + 0.16f * intensity);
            for (int beat = 0; beat < 4; ++beat)
                for (int hit = 0; hit < 2; ++hit)
                    addHat(beat + swingOffset(hit, 2), (hit == 0 ? 0.54f : 0.42f) + 0.22f * intensity);
            sixteenthKicks(4);
            break;
        }
        case GroovePattern::Experimental: {
            for (int step = 0; step < 16; ++step) {
                const double pos = static_cast<double>(step) * 0.25;
                const float downbeat = step % 4 == 0 ? 0.30f : 0.0f;
                if (chance((0.10f + downbeat + style.sixteenthKick * 0.5f) * complexityScale * (0.5f + intensity)))
                    add(DrumInstrument::Kick, pos, 0.55f + 0.3f * intensity);
                if (step % 2 == 0 && chance(style.closedHatProbability * 0.7f)) {
                    add(chance(style.openHatProbability) ? DrumInstrument::OpenHat : DrumInstrument::ClosedHat, pos,
                        0.40f + 0.25f * intensity);
                }
            }
            if (chance(0.65f)) add(DrumInstrument::Snare, 1.0, 0.78f + 0.18f * intensity);
            else add(DrumInstrument::Snare, 1.25, 0.70f + 0.18f * intensity);
            if (chance(0.65f)) add(DrumInstrument::Snare, 3.0, 0.78f + 0.18f * intensity);
            else add(DrumInstrument::Snare, 3.5, 0.70f + 0.18f * intensity);
            for (int i = 0; i < 4; ++i)
                if (chance(0.06f * complexityScale * (0.5f + intensity)))
                    add(i % 2 == 0 ? DrumInstrument::HighTom : DrumInstrument::FloorTom, 0.5 + i, 0.5f + 0.3f * intensity);
            if (chance(style.ghostSnareProbability * complexityScale))
                add(DrumInstrument::Snare, 2.75, 0.26f);
            break;
        }
        case GroovePattern::Jazz: {
            // Ride pattern ("ding, ding-a, ding, ding-a"), foot hat on 2 and 4, feathered kick, sparse comping.
            const double skip = 0.5 + std::clamp(static_cast<double>(style.swing), 0.0, 0.45) * 0.5 + 0.17;
            for (int beat = 0; beat < 4; ++beat) {
                add(DrumInstrument::Ride, beat, 0.52f + 0.22f * intensity + (beat % 2 == 1 ? 0.05f : 0.0f));
                if (beat % 2 == 1) {
                    add(DrumInstrument::Ride, beat + std::min(0.95, skip), 0.40f + 0.2f * intensity);
                    add(DrumInstrument::ClosedHat, beat, 0.34f + 0.1f * intensity);
                }
                if (chance(style.kickBeat1 * 0.8f)) add(DrumInstrument::Kick, beat, 0.30f + 0.2f * intensity);
                if (chance(style.ghostSnareProbability * complexityScale * (0.3f + intensity)))
                    add(DrumInstrument::Snare, beat + 0.67, 0.30f + 0.18f * intensity);
            }
            if (chance(style.snareBackbeat * complexityScale))
                add(DrumInstrument::Snare, chance(0.5f) ? 1.0 : 3.0, 0.50f + 0.2f * intensity);
            break;
        }
        case GroovePattern::Standard: {
            const auto meter = MeterProfile::forMeter(context.numerator, context.denominator);
            const int hatHits = context.denominator >= 8
                ? 1
                : std::clamp(style.hatHitsPerBeat, 1, 4);

            for (int beat = 0; beat < context.numerator; ++beat) {
                const auto beatIndex = static_cast<std::size_t>(beat);

                if (meter.kickAnchor[beatIndex]) {
                    const float probability = beat == 0 ? style.kickBeat1 : style.kickBeat3;
                    if (chance(probability))
                        add(DrumInstrument::Kick, beat, (beat == 0 ? 0.75f : 0.70f) + 0.2f * intensity);
                }

                if (meter.backbeat[beatIndex] && chance(style.snareBackbeat))
                    add(DrumInstrument::Snare, beat, 0.78f + 0.18f * intensity);

                const float extraKick = style.extraKickProbability * complexityScale * (0.25f + 1.5f * intensity);
                if (chance(extraKick))
                    add(DrumInstrument::Kick, beat + 0.5, 0.55f + 0.3f * intensity);

                if (meter.backbeat[beatIndex] && chance(style.ghostSnareProbability * complexityScale * (0.4f + intensity)))
                    add(DrumInstrument::Snare, beat + 0.75, 0.28f + 0.18f * intensity);

                for (int hit = 0; hit < hatHits; ++hit) {
                    const double position = swingOffset(hit, hatHits);
                    const bool groupAccent = meter.kickAnchor[beatIndex];
                    const float accent = hit == 0 ? (groupAccent ? 0.14f : 0.08f) : -0.04f;
                    addHat(static_cast<double>(beat) + position, 0.52f + accent + 0.25f * intensity);
                }
            }
            sixteenthKicks(context.numerator);
            break;
        }
        }

        humanize(style, context, barSamples, out, count);
        return count;
    }

private:
    // Style-aware microtiming: triangular (not uniform) jitter scaled by the style's looseness and by tempo,
    // plus a limb-specific bias (snare sits behind the kick, hats slightly ahead) and a global push/lay-back feel.
    template <std::size_t Capacity>
    static void humanize(const Style& style, const GrooveContext& context, int barSamples,
                         std::array<DrumEvent, Capacity>& out, std::size_t count) noexcept {
        const float amount = std::clamp(context.humanize, 0.0f, 1.0f);
        const double feelMs = std::clamp(static_cast<double>(context.feelMs), -30.0, 30.0);
        if (amount <= 0.0f && feelMs == 0.0) return;

        std::uint32_t state = context.seed * 2654435761u + 0x68E31DA4u;
        const double samplesPerMs = context.sampleRate / 1000.0;
        const double tempoScale = std::clamp(120.0 / context.bpm, 0.5, 1.5);
        const double maxMs = static_cast<double>(style.humanizeMs) * 2.0 * amount * tempoScale;

        for (std::size_t i = 0; i < count; ++i) {
            auto& e = out[i];
            double bias = 0.0;
            double feelWeight = 1.0;
            switch (e.instrument) {
                case DrumInstrument::Kick: bias = 0.10; break;
                case DrumInstrument::Snare:
                case DrumInstrument::Rimshot:
                case DrumInstrument::Sidestick:
                case DrumInstrument::Clap: bias = 0.30; break;
                case DrumInstrument::ClosedHat:
                case DrumInstrument::OpenHat: bias = -0.10; feelWeight = 0.5; break;
                case DrumInstrument::Ride: feelWeight = 0.5; break;
                default: bias = 0.2; feelWeight = 0.5; break;
            }
            const double jitter = (static_cast<double>(next01(state)) + static_cast<double>(next01(state)) - 1.0) * maxMs;
            const double ms = jitter + bias * maxMs + feelMs * feelWeight;
            const int shifted = e.sampleOffset + static_cast<int>(std::llround(ms * samplesPerMs));
            e.sampleOffset = std::clamp(shifted, 0, std::max(0, barSamples - 1));
            const float velocityJitter = (next01(state) * 2.0f - 1.0f) * 0.06f * amount;
            e.velocity = std::clamp(e.velocity + velocityJitter, 0.0f, 1.0f);
        }
    }

    static float next01(std::uint32_t& state) noexcept {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>((state >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }
};

} // namespace robodrummer
