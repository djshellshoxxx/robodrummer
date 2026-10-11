// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "core/FillGenerator.h"
#include "core/GrooveGenerator.h"
#include "core/Humanizer.h"
#include "core/JamState.h"
#include "core/Style.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace robodrummer {

// Fill controls (SPEC section 23). Defaults reproduce the original one-beat fill.
struct FillSettings {
    float frequency{1.0f};     // 0-2 user fill-frequency scale (1 = style default); consumed via fillFrequencyScale()
    float complexity{-1.0f};   // 0-1; negative = automatic (follows fill strength)
    FillLength length{FillLength::OneBeat};
    bool crashLanding{false};  // land a crash on the downbeat after the fill (probability from Style)
};

class JamEngine {
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; resetPhase(); }
    void resetPhase() noexcept { sampleCursor_ = 0; fillTargetBar_ = -1; breakUntilBar_ = -1; pendingCrash_ = -1; }
    void setTempo(double bpm) noexcept { bpm_ = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 20.0, 400.0); }
    void setMeter(int n, int d) noexcept { numerator_ = std::clamp(n, 1, MeterProfile::MaxBeats); denominator_ = (d == 1 || d == 2 || d == 4 || d == 8 || d == 16) ? d : 4; }
    void setIntensity(float value) noexcept { state_.intensity = std::clamp(value, 0.0f, 1.0f); }
    void setStyle(const Style& style) noexcept { style_ = style; }
    // Groove complexity 0-1 (default 0.5 = the style's own density).
    void setComplexity(float value) noexcept { complexity_ = std::clamp(std::isfinite(value) ? value : 0.5f, 0.0f, 1.0f); }
    // Humanization amount 0-1 (default 0 = off, events land exactly on the grid).
    void setHumanize(float value) noexcept { humanize_ = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f); }
    void setFillSettings(const FillSettings& settings) noexcept {
        const auto previous = fillSettings_;
        fillSettings_ = settings;
        fillSettings_.frequency = std::clamp(std::isfinite(settings.frequency) ? settings.frequency : 1.0f, 0.0f, 2.0f);
        if (!(settings.complexity < 0.0f)) fillSettings_.complexity = std::clamp(settings.complexity, 0.0f, 1.0f);
        else fillSettings_.complexity = -1.0f;
        // Only a change of length can move the fill window; per-block re-application must not disturb it.
        if (previous.length != fillSettings_.length) clearPendingFillTarget();
    }
    void setFillLength(FillLength length) noexcept { auto s = fillSettings_; s.length = length; setFillSettings(s); }
    void setFillComplexity(float value) noexcept { auto s = fillSettings_; s.complexity = value; setFillSettings(s); }
    void setFillFrequency(float value) noexcept { auto s = fillSettings_; s.frequency = value; setFillSettings(s); }
    void setFillCrashLanding(bool enabled) noexcept { auto s = fillSettings_; s.crashLanding = enabled; setFillSettings(s); }
    [[nodiscard]] const FillSettings& fillSettings() const noexcept { return fillSettings_; }
    // User frequency times the current style's own multiplier, for JamStyleProfile::toBrainSettings().
    [[nodiscard]] float fillFrequencyScale() const noexcept { return std::clamp(fillSettings_.frequency * style_.fillFrequency, 0.0f, 2.0f); }
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
        state_.fillStrength = std::clamp(strength, 0.0f, 1.0f);
        fillTargetBar_ = -1;
    }
    void apply(MidiCommand command) noexcept {
        const double previousScale = state_.timeScale;
        applyMidiCommand(state_, command);
        // Bar indices change meaning when the feel changes; re-target any pending break/fill.
        if (std::abs(state_.timeScale - previousScale) > 1.0e-9) clearBarTargets();
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

        // Resolve the fill window first so longer fills can thin the groove underneath them.
        const int fillBeats = fillLengthBeats(fillSettings_.length, numerator_);
        const int fillBars = fillLengthBars(fillSettings_.length, numerator_);
        long long fillStart = 0;
        long long fillEnd = 0;
        const bool fillActive = state_.fillRequested;
        if (fillActive) {
            if (fillTargetBar_ < 0) {
                const long long earliest = breakUntilBar_ >= 0 ? std::max(firstBar, breakUntilBar_) : firstBar;
                fillTargetBar_ = earliest + fillBars - 1;
                for (int guard = 0; guard < 8; ++guard) {
                    const long long start = fillTargetBar_ * barSamples + static_cast<long long>(std::llround(spb * (numerator_ - fillBeats)));
                    if (start >= blockStart) break;
                    ++fillTargetBar_;
                }
            }
            fillStart = fillTargetBar_ * barSamples + static_cast<long long>(std::llround(spb * (numerator_ - fillBeats)));
            fillEnd = (fillTargetBar_ + 1) * barSamples;
        }
        const bool thinGroove = fillActive && fillBeats > 1;

        Humanizer humanizer;
        humanizer.sampleRate = sampleRate_;
        humanizer.bpm = effectiveBpm;
        humanizer.samplesPerBeat = spb;
        humanizer.amount = humanize_;

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
            ctx.complexity = complexity_;
            ctx.sectionStart = (bar == 0);
            ctx.seed = static_cast<std::uint32_t>(0x5244424Du + static_cast<std::uint32_t>(bar));
            const auto eventCount = generator_.generateBar(style_, ctx, events);
            const long long barStart = bar * barSamples;
            humanizer.seed = ctx.seed ^ 0x48554D41u;
            for (std::size_t i = 0; i < eventCount && count < capacity; ++i) {
                humanizer.apply(style_, events[i], static_cast<std::uint32_t>(i), 0, static_cast<int>(barSamples - 1));
                const long long absolute = barStart + events[i].sampleOffset;
                if (thinGroove && absolute >= fillStart && absolute < fillEnd && isFillReplaced(events[i].instrument))
                    continue;
                if (absolute >= blockStart && absolute < blockEnd) {
                    auto e = events[i];
                    e.sampleOffset = static_cast<int>(absolute - blockStart);
                    out[count++] = e;
                }
            }
        }

        if (fillActive) {
            FillParams params;
            params.length = fillSettings_.length;
            params.numerator = numerator_;
            params.strength = state_.fillStrength;
            params.complexity = fillSettings_.complexity;
            params.vocabulary = style_.fillVocabulary;
            params.seed = 0xF1110000u ^ static_cast<std::uint32_t>(fillTargetBar_);
            std::array<FillNote, MaxFillNotes> notes{};
            const auto noteCount = FillGenerator::generate(params, notes);
            humanizer.seed = params.seed ^ 0x48554D41u;
            const int regionLast = static_cast<int>(std::max<long long>(0, fillEnd - fillStart - 1));
            for (std::size_t i = 0; i < noteCount && count < capacity; ++i) {
                DrumEvent e{notes[i].instrument,
                            static_cast<int>(std::llround(spb * notes[i].beat)),
                            notes[i].velocity, 0};
                humanizer.apply(style_, e, static_cast<std::uint32_t>(i), 0, regionLast);
                const long long absolute = fillStart + e.sampleOffset;
                if (absolute >= blockStart && absolute < blockEnd) {
                    out[count] = { e.instrument, static_cast<int>(absolute - blockStart), e.velocity, static_cast<std::uint32_t>(count) };
                    ++count;
                }
            }
            if (blockEnd >= fillEnd) {
                state_.fillRequested = false;
                const bool breakFollows = breakUntilBar_ > fillTargetBar_ + 1;
                std::uint32_t crashState = params.seed * 1664525u + 1013904223u;
                const float roll = static_cast<float>((crashState >> 8) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
                if (fillSettings_.crashLanding && !breakFollows && roll < std::clamp(style_.fillCrashProbability, 0.0f, 1.0f))
                    pendingCrash_ = fillEnd;
                fillTargetBar_ = -1;
            }
        }

        if (pendingCrash_ >= 0) {
            if (pendingCrash_ >= blockStart && pendingCrash_ < blockEnd && count < capacity) {
                out[count] = {DrumInstrument::Crash, static_cast<int>(pendingCrash_ - blockStart), 0.92f, static_cast<std::uint32_t>(count)};
                ++count;
                pendingCrash_ = -1;
            } else if (pendingCrash_ < blockStart) {
                pendingCrash_ = -1;
            }
        }

        if (state_.crashRequested && count < capacity) { out[count++] = {DrumInstrument::Crash, 0, 1.0f, 0}; state_.crashRequested = false; }
        sampleCursor_ = blockEnd;
        std::sort(out, out + count, [](const DrumEvent& a, const DrumEvent& b){ return a.sampleOffset < b.sampleOffset; });
        return count;
    }
private:
    void clearBarTargets() noexcept { fillTargetBar_ = -1; breakUntilBar_ = -1; pendingCrash_ = -1; }
    void clearPendingFillTarget() noexcept { if (state_.fillRequested) fillTargetBar_ = -1; }
    static bool isFillReplaced(DrumInstrument i) noexcept {
        return i == DrumInstrument::ClosedHat || i == DrumInstrument::OpenHat || i == DrumInstrument::Ride ||
               i == DrumInstrument::Snare || i == DrumInstrument::Sidestick || i == DrumInstrument::Rimshot;
    }

    double sampleRate_{48000.0};
    double bpm_{120.0};
    int numerator_{4};
    int denominator_{4};
    long long sampleCursor_{0};
    long long fillTargetBar_{-1};
    long long breakUntilBar_{-1};
    long long pendingCrash_{-1};
    float complexity_{0.5f};
    float humanize_{0.0f};
    FillSettings fillSettings_{};
    Style style_{Style::basicRock()};
    GrooveGenerator generator_{};
    JamState state_{};
};
}
