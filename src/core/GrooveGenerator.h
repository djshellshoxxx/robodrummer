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

            const auto sequence = static_cast<std::uint32_t>(count);
            DrumEvent event{instrument, offset, std::clamp(velocity, 0.0f, 1.0f), sequence};
            humanizeEvent(event, style, context, barSamples);
            out[count] = event;
            ++count;
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

    static std::uint32_t mixHash(std::uint32_t value) noexcept {
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }

    static float hash01(std::uint32_t seed, std::uint32_t sequence, std::uint32_t salt) noexcept {
        const auto value = mixHash(seed ^ (sequence * 0x9e3779b9u) ^ salt);
        return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    static float triangularNoise(std::uint32_t seed, std::uint32_t sequence, std::uint32_t salt) noexcept {
        const float a = hash01(seed, sequence, salt);
        const float b = hash01(seed, sequence, salt ^ 0xa5a5a5a5u);
        return (a + b) - 1.0f;
    }

    static float timingScaleFor(DrumInstrument instrument) noexcept {
        switch (instrument) {
            case DrumInstrument::ClosedHat:
            case DrumInstrument::OpenHat:
            case DrumInstrument::Ride:
                return 0.72f;
            case DrumInstrument::Snare:
            case DrumInstrument::Rimshot:
            case DrumInstrument::Sidestick:
            case DrumInstrument::Clap:
                return 0.58f;
            case DrumInstrument::Kick:
                return 0.42f;
            case DrumInstrument::HighTom:
            case DrumInstrument::MidTom:
            case DrumInstrument::FloorTom:
                return 0.82f;
            default:
                return 0.62f;
        }
    }

    static float limbBiasFor(DrumInstrument instrument) noexcept {
        switch (instrument) {
            case DrumInstrument::ClosedHat:
            case DrumInstrument::OpenHat:
            case DrumInstrument::Ride:
                return -0.08f;
            case DrumInstrument::Snare:
            case DrumInstrument::Rimshot:
            case DrumInstrument::Sidestick:
            case DrumInstrument::Clap:
                return 0.10f;
            case DrumInstrument::Kick:
                return 0.02f;
            default:
                return 0.0f;
        }
    }

    static float velocityScaleFor(DrumInstrument instrument) noexcept {
        switch (instrument) {
            case DrumInstrument::ClosedHat:
            case DrumInstrument::OpenHat:
            case DrumInstrument::Ride:
                return 1.0f;
            case DrumInstrument::Snare:
            case DrumInstrument::Rimshot:
            case DrumInstrument::Sidestick:
                return 0.72f;
            case DrumInstrument::Kick:
                return 0.55f;
            default:
                return 0.82f;
        }
    }

    static void humanizeEvent(DrumEvent& event,
                              const Style& style,
                              const GrooveContext& context,
                              int barSamples) noexcept {
        const float amountMs = std::clamp(style.humanizeMs, 0.0f, 30.0f);
        if (amountMs <= 0.0f || !(context.sampleRate > 0.0))
            return;

        const float normalizedAmount = std::clamp(amountMs / 12.0f, 0.0f, 1.0f);
        const int maxTimingSamples = std::max(0, static_cast<int>(std::llround(context.sampleRate * amountMs / 1000.0)));

        // Beat one is a structural anchor. Other events receive deterministic,
        // triangular microtiming with limb-family bias rather than uniform jitter.
        if (event.sampleOffset > 0 && maxTimingSamples > 0) {
            const float random = triangularNoise(context.seed, event.sequence, 0x31415926u);
            const float shaped = random * timingScaleFor(event.instrument) + limbBiasFor(event.instrument);
            const int delta = static_cast<int>(std::llround(
                std::clamp(shaped, -1.0f, 1.0f) * static_cast<float>(maxTimingSamples)));
            event.sampleOffset = std::clamp(event.sampleOffset + delta, 0, std::max(0, barSamples - 1));
        }

        const float velocityNoise = triangularNoise(context.seed, event.sequence, 0x27182818u);
        const float velocityDelta = velocityNoise * (0.035f + 0.025f * normalizedAmount)
            * velocityScaleFor(event.instrument);
        event.velocity = std::clamp(event.velocity + velocityDelta, 0.0f, 1.0f);
    }
};

} // namespace robodrummer
