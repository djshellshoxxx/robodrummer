// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "core/FillGenerator.h"
#include "core/GrooveGenerator.h"
#include "core/JamState.h"
#include "core/Style.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace robodrummer {
class JamEngine {
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; resetPhase(); }
    void resetPhase() noexcept { sampleCursor_ = 0; clearBarTargets(); }
    void setTempo(double bpm) noexcept { bpm_ = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 20.0, 400.0); }
    void setMeter(int n, int d) noexcept { numerator_ = std::clamp(n, 1, MeterProfile::MaxBeats); denominator_ = (d == 1 || d == 2 || d == 4 || d == 8 || d == 16) ? d : 4; }
    void setIntensity(float value) noexcept { state_.intensity = std::clamp(value, 0.0f, 1.0f); }
    void setStyle(const Style& style) noexcept { style_ = style; }
    void setFillSettings(const FillSettings& settings) noexcept { fillSettings_ = settings; }
    void setHumanize(float amount) noexcept { humanize_ = std::clamp(amount, 0.0f, 1.0f); }
    void setFeelMs(float ms) noexcept { feelMs_ = std::clamp(ms, -30.0f, 30.0f); }
    void setComplexity(float value) noexcept { complexity_ = std::clamp(value, 0.0f, 1.0f); }
    void setCrashScale(float value) noexcept { crashScale_ = std::clamp(value, 0.0f, 2.0f); }
    // Sets the half/normal/double feel directly (used by programmed sections); MIDI commands toggle it instead.
    void setTimeScale(double scale) noexcept {
        const double clamped = scale < 0.75 ? 0.5 : (scale > 1.5 ? 2.0 : 1.0);
        if (std::abs(clamped - state_.timeScale) > 1.0e-9) { state_.timeScale = clamped; clearBarTargets(); }
    }
    void syncToPpq(double ppqPosition) noexcept {
        if (!std::isfinite(ppqPosition)) return;
        if (!(bpm_ > 0.0) || !(sampleRate_ > 0.0)) return;
        // The cursor is a real-time sample position; half/double time only changes bar and beat sizes.
        const double samplesPerQuarter = sampleRate_ * 60.0 / bpm_;
        const auto target = static_cast<long long>(std::llround(std::max(0.0, ppqPosition) * samplesPerQuarter));
        // A backwards move or a jump of more than a quarter note is a host seek/loop: pending bar targets
        // computed on the old timeline would otherwise silence or stall the groove.
        if (target < sampleCursor_ - 1 || target - sampleCursor_ > static_cast<long long>(samplesPerQuarter))
            clearBarTargets();
        sampleCursor_ = target;
    }
    void nudgePhaseSamples(long long deltaSamples) noexcept {
        sampleCursor_ = std::max<long long>(0, sampleCursor_ + deltaSamples);
    }
    [[nodiscard]] long long currentBarIndex() const noexcept {
        const double effectiveBpm = bpm_ * state_.timeScale;
        if (!(effectiveBpm > 0.0) || !(sampleRate_ > 0.0)) return 0;
        const double spq = sampleRate_ * 60.0 / effectiveBpm;
        const double spb = spq * (4.0 / static_cast<double>(denominator_));
        const long long barSamples = std::max<long long>(1, static_cast<long long>(std::llround(spb * numerator_)));
        return sampleCursor_ / barSamples;
    }

    // Phase of the unscaled pulse, so half/double-time feels still line up with the guitarist's beat.
    [[nodiscard]] double currentBeatPhase() const noexcept {
        if (!(bpm_ > 0.0) || !(sampleRate_ > 0.0)) return 0.0;
        const double spq = sampleRate_ * 60.0 / bpm_;
        const double spb = spq * (4.0 / static_cast<double>(denominator_));
        if (!(spb > 0.0)) return 0.0;
        double phase = std::fmod(static_cast<double>(sampleCursor_) / spb, 1.0);
        if (phase < 0.0) phase += 1.0;
        return phase;
    }
    void requestFill(float strength) noexcept {
        state_.fillRequested = true;
        state_.fillImmediate = false;
        state_.fillStrength = std::clamp(strength, 0.0f, 1.0f);
        fillStartBeat_ = -1;
    }
    void apply(MidiCommand command) noexcept {
        const double previousScale = state_.timeScale;
        applyMidiCommand(state_, command);
        // Bar indices change meaning when the feel changes; re-target any pending break/fill.
        if (std::abs(state_.timeScale - previousScale) > 1.0e-9) clearBarTargets();
        if (command == MidiCommand::Fill || command == MidiCommand::ImmediateFill) fillStartBeat_ = -1;
        if (command == MidiCommand::ResetListening) {
            resetPhase();
            state_.resetListeningRequested = false;
        }
    }
    [[nodiscard]] const JamState& state() const noexcept { return state_; }
    [[nodiscard]] std::size_t processBlock(int numSamples, DrumEvent* out, std::size_t capacity) noexcept {
        if (numSamples <= 0 || out == nullptr || capacity == 0 || state_.stopped) { if (numSamples > 0) sampleCursor_ += numSamples; return 0; }
        const double effectiveBpm = bpm_ * state_.timeScale;
        const double spq = sampleRate_ * 60.0 / effectiveBpm;
        const double spb = spq * (4.0 / static_cast<double>(denominator_));
        const long long barSamples = std::max<long long>(1, static_cast<long long>(std::llround(spb * numerator_)));
        const long long blockStart = sampleCursor_;
        const long long blockEnd = blockStart + numSamples;
        const long long firstBar = blockStart / barSamples;
        const long long lastBar = (blockEnd - 1) / barSamples;
        const auto beatSample = [spb](long long beat) { return static_cast<long long>(std::llround(static_cast<double>(beat) * spb)); };

        if (state_.breakRequested) {
            breakUntilBar_ = std::max(breakUntilBar_, firstBar + 1);
            state_.breakRequested = false;
        }
        if (breakUntilBar_ >= 0 && firstBar >= breakUntilBar_)
            breakUntilBar_ = -1;

        // Plan a requested fill once; positions are stored as beat indices so tempo changes keep it aligned.
        if (state_.fillRequested && fillStartBeat_ < 0) {
            fillTotalBeats_ = FillGenerator::lengthBeats(fillSettings_.length, numerator_);
            const long long baseBar = breakUntilBar_ >= 0 ? std::max(firstBar, breakUntilBar_) : firstBar;
            if (state_.fillImmediate && breakUntilBar_ < 0) {
                fillStartBeat_ = (blockStart + static_cast<long long>(spb) - 1) / std::max<long long>(1, static_cast<long long>(spb));
                if (beatSample(fillStartBeat_) < blockStart) ++fillStartBeat_;
            } else {
                const long long barStartBeat = baseBar * numerator_;
                fillStartBeat_ = fillTotalBeats_ >= numerator_ ? barStartBeat : barStartBeat + (numerator_ - fillTotalBeats_);
                if (beatSample(fillStartBeat_) < blockStart) fillStartBeat_ += numerator_;
            }
            fillHitCount_ = FillGenerator::generate(fillSettings_, state_.fillStrength, fillTotalBeats_,
                                                    static_cast<std::uint32_t>(0xF111u + static_cast<std::uint32_t>(fillStartBeat_)), fillHits_);
        }
        const bool fillPlanned = state_.fillRequested && fillStartBeat_ >= 0;
        const long long fillStartSample = fillPlanned ? beatSample(fillStartBeat_) : 0;
        const long long fillEndSample = fillPlanned ? beatSample(fillStartBeat_ + fillTotalBeats_) : 0;

        std::size_t count = 0;
        for (long long bar = firstBar; bar <= lastBar && count < capacity; ++bar) {
            if (breakUntilBar_ >= 0 && bar < breakUntilBar_)
                continue;
            std::array<DrumEvent, 64> events{};
            GrooveContext ctx;
            ctx.sampleRate = sampleRate_;
            ctx.bpm = effectiveBpm;
            ctx.numerator = numerator_;
            ctx.denominator = denominator_;
            ctx.intensity = state_.intensity;
            ctx.sectionStart = (bar == 0);
            ctx.seed = static_cast<std::uint32_t>(0x5244424Du + static_cast<std::uint32_t>(bar));
            ctx.humanize = humanize_;
            ctx.feelMs = feelMs_;
            ctx.complexity = complexity_;
            ctx.crashScale = crashScale_;
            const auto eventCount = generator_.generateBar(style_, ctx, events);
            const long long barStart = bar * barSamples;
            for (std::size_t i = 0; i < eventCount && count < capacity; ++i) {
                const long long absolute = barStart + events[i].sampleOffset;
                if (absolute >= blockStart && absolute < blockEnd) {
                    // The fill replaces the groove it covers (cymbal crashes aside).
                    if (fillPlanned && absolute >= fillStartSample && absolute < fillEndSample &&
                        events[i].instrument != DrumInstrument::Crash)
                        continue;
                    auto e = events[i];
                    e.sampleOffset = static_cast<int>(absolute - blockStart);
                    out[count++] = e;
                }
            }
        }

        if (fillPlanned) {
            for (std::size_t i = 0; i < fillHitCount_ && count < capacity; ++i) {
                const long long absolute = fillStartSample + static_cast<long long>(std::llround(spb * fillHits_[i].beatOffset));
                if (absolute >= blockStart && absolute < blockEnd)
                    out[count++] = { fillHits_[i].instrument, static_cast<int>(absolute - blockStart), fillHits_[i].velocity, static_cast<std::uint32_t>(count) };
            }
            if (FillGenerator::endsWithCrash(fillTotalBeats_) && fillEndSample >= blockStart && fillEndSample < blockEnd && count < capacity)
                out[count++] = { DrumInstrument::Crash, static_cast<int>(fillEndSample - blockStart), 0.95f, static_cast<std::uint32_t>(count) };
            if (blockEnd >= fillEndSample) {
                state_.fillRequested = false;
                state_.fillImmediate = false;
                fillStartBeat_ = -1;
            }
        }

        if (state_.crashRequested && count < capacity) { out[count++] = {DrumInstrument::Crash, 0, 1.0f, 0}; state_.crashRequested = false; }
        sampleCursor_ = blockEnd;
        std::sort(out, out + count, [](const DrumEvent& a, const DrumEvent& b){ return a.sampleOffset < b.sampleOffset; });
        return count;
    }
private:
    void clearBarTargets() noexcept { fillStartBeat_ = -1; breakUntilBar_ = -1; }

    double sampleRate_{48000.0};
    double bpm_{120.0};
    int numerator_{4};
    int denominator_{4};
    long long sampleCursor_{0};
    long long fillStartBeat_{-1};
    int fillTotalBeats_{1};
    std::size_t fillHitCount_{0};
    std::array<FillHit, FillGenerator::MaxHits> fillHits_{};
    long long breakUntilBar_{-1};
    FillSettings fillSettings_{};
    float humanize_{0.0f};
    float feelMs_{0.0f};
    float complexity_{0.5f};
    float crashScale_{1.0f};
    Style style_{Style::basicRock()};
    GrooveGenerator generator_{};
    JamState state_{};
};
}
