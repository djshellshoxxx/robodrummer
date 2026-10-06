// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "core/GrooveGenerator.h"
#include "core/FillGenerator.h"
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
    void resetPhase() noexcept { sampleCursor_ = 0; fillTargetBar_ = -1; breakUntilBar_ = -1; }
    void setTempo(double bpm) noexcept { bpm_ = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 20.0, 400.0); }
    void setMeter(int n, int d) noexcept { numerator_ = std::max(1, n); denominator_ = (d == 1 || d == 2 || d == 4 || d == 8 || d == 16) ? d : 4; }
    void setIntensity(float value) noexcept { state_.intensity = std::clamp(value, 0.0f, 1.0f); }
    void setStyle(const Style& style) noexcept { style_ = style; }
    void syncToPpq(double ppqPosition) noexcept {
        if (!std::isfinite(ppqPosition)) return;
        const double effectiveBpm = bpm_ * state_.timeScale;
        if (!(effectiveBpm > 0.0) || !(sampleRate_ > 0.0)) return;
        const double samplesPerQuarter = sampleRate_ * 60.0 / effectiveBpm;
        sampleCursor_ = static_cast<long long>(std::llround(ppqPosition * samplesPerQuarter));
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

    [[nodiscard]] double currentBeatPhase() const noexcept {
        const double effectiveBpm = bpm_ * state_.timeScale;
        if (!(effectiveBpm > 0.0) || !(sampleRate_ > 0.0)) return 0.0;
        const double spq = sampleRate_ * 60.0 / effectiveBpm;
        const double spb = spq * (4.0 / static_cast<double>(denominator_));
        if (!(spb > 0.0)) return 0.0;
        double phase = std::fmod(static_cast<double>(sampleCursor_) / spb, 1.0);
        if (phase < 0.0) phase += 1.0;
        return phase;
    }
    void setFillLength(FillLength length) noexcept { fillLength_ = length; }
    [[nodiscard]] FillLength fillLength() const noexcept { return fillLength_; }
    void requestFill(float strength, FillLength length = FillLength::OneBeat) noexcept {
        fillLength_ = length;
        state_.fillRequested = true;
        state_.fillStrength = std::clamp(strength, 0.0f, 1.0f);
        fillTargetBar_ = -1;
    }
    void apply(MidiCommand command) noexcept {
        applyMidiCommand(state_, command);
        if (command == MidiCommand::Fill) fillTargetBar_ = -1;
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

        if (state_.breakRequested) {
            breakUntilBar_ = std::max(breakUntilBar_, firstBar + 1);
            state_.breakRequested = false;
        }
        if (breakUntilBar_ >= 0 && firstBar >= breakUntilBar_)
            breakUntilBar_ = -1;

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
            const auto eventCount = generator_.generateBar(style_, ctx, events);
            const long long barStart = bar * barSamples;
            for (std::size_t i = 0; i < eventCount && count < capacity; ++i) {
                const long long absolute = barStart + events[i].sampleOffset;
                if (absolute >= blockStart && absolute < blockEnd) {
                    auto e = events[i];
                    e.sampleOffset = static_cast<int>(absolute - blockStart);
                    out[count++] = e;
                }
            }
        }

        if (state_.fillRequested) {
            const double durationBeats = fillLengthBeats(fillLength_, numerator_);
            auto fillStartForBar = [&](long long targetBar) {
                const long long targetStart = targetBar * barSamples;
                if (durationBeats <= static_cast<double>(numerator_)) {
                    return targetStart + static_cast<long long>(
                        std::llround(spb * (static_cast<double>(numerator_) - durationBeats)));
                }
                return targetStart;
            };

            if (fillTargetBar_ < 0) {
                fillTargetBar_ = breakUntilBar_ >= 0 ? std::max(firstBar, breakUntilBar_) : firstBar;
                if (fillStartForBar(fillTargetBar_) < blockStart)
                    ++fillTargetBar_;
            }

            const long long fillStart = fillStartForBar(fillTargetBar_);
            const long long fillEnd = fillStart + static_cast<long long>(std::llround(spb * durationBeats));

            FillContext fillContext;
            fillContext.sampleRate = sampleRate_;
            fillContext.bpm = effectiveBpm;
            fillContext.numerator = numerator_;
            fillContext.denominator = denominator_;
            fillContext.strength = state_.fillStrength;
            fillContext.length = fillLength_;

            std::array<DrumEvent, 64> fillEvents{};
            const auto generated = fillGenerator_.generate(fillContext, fillEvents);
            for (std::size_t i = 0; i < generated && count < capacity; ++i) {
                const long long absolute = fillStart + fillEvents[i].sampleOffset;
                if (absolute >= blockStart && absolute < blockEnd) {
                    auto event = fillEvents[i];
                    event.sampleOffset = static_cast<int>(absolute - blockStart);
                    event.sequence = static_cast<std::uint32_t>(count);
                    out[count++] = event;
                }
            }

            if (blockEnd >= fillEnd) {
                state_.fillRequested = false;
                fillTargetBar_ = -1;
            }
        }

        if (state_.crashRequested && count < capacity) { out[count++] = {DrumInstrument::Crash, 0, 1.0f, 0}; state_.crashRequested = false; }
        sampleCursor_ = blockEnd;
        std::sort(out, out + count, [](const DrumEvent& a, const DrumEvent& b){ return a.sampleOffset < b.sampleOffset; });
        return count;
    }
private:
    double sampleRate_{48000.0};
    double bpm_{120.0};
    int numerator_{4};
    int denominator_{4};
    long long sampleCursor_{0};
    long long fillTargetBar_{-1};
    long long breakUntilBar_{-1};
    Style style_{Style::basicRock()};
    FillLength fillLength_{FillLength::OneBeat};
    GrooveGenerator generator_{};
    FillGenerator fillGenerator_{};
    JamState state_{};
};
}
