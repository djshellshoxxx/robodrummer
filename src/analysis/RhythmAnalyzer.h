#pragma once
#include "analysis/DownbeatTracker.h"
#include "analysis/OnsetDetector.h"
#include "analysis/TempoMovement.h"
#include "analysis/TempoTracker.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace robodrummer {

struct RhythmState {
    double tempoBpm{120.0};
    double tempoVelocity{0.0};
    double beatPhase{0.0};
    double predictedNextBeatSeconds{0.0};
    int beatInBar{1};
    int meterNumerator{4};
    float tempoConfidence{0.0f};
    float beatConfidence{0.0f};
    float downbeatConfidence{0.0f};
    float meterConfidence{0.0f};
    bool locked{false};
    TempoMovement movement{TempoMovement::Steady};
};

class LiveRhythmAnalyzer {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        detector_.prepare(sampleRate_);
        reset();
    }

    void reset() noexcept {
        detector_.reset();
        tempo_.reset();
        downbeat_.reset(4);
        sampleCursor_ = 0;
        phaseAnchorSeconds_ = std::numeric_limits<double>::quiet_NaN();
        lastOnsetSeconds_ = std::numeric_limits<double>::quiet_NaN();
        lastUpdateSeconds_ = 0.0;
        previousTempo_ = 120.0;
        lastObservedBeatIndex_ = std::numeric_limits<long long>::min();
        pllPeriod_ = 0.0;
        intervalCount_ = 0;
        intervalHead_ = 0;
        beatCounter_ = 0;
        pllConsistency_ = 0.0f;
        pllUpdates_ = 0;
        lockLatched_ = false;
        movement_.reset();
        state_ = {};
    }

    void setMeterNumerator(int meterNumerator) noexcept {
        const int meter = std::clamp(meterNumerator, 2, 12);
        if (meter != state_.meterNumerator) {
            downbeat_.reset(meter);
            lastObservedBeatIndex_ = std::numeric_limits<long long>::min();
            state_.meterNumerator = meter;
            state_.beatInBar = 1;
            state_.downbeatConfidence = 0.0f;
            state_.meterConfidence = 0.0f;
        }
    }

    void processBlock(const float* mono, int numSamples) noexcept {
        blockOnsetCount_ = 0;
        if (mono == nullptr || numSamples <= 0) return;
        const auto count = detector_.processBlock(mono, numSamples, blockOnsets_.data(), blockOnsets_.size());
        blockOnsetCount_ = count;
        blockStartSample_ = sampleCursor_;
        for (std::size_t i = 0; i < count; ++i) {
            const double eventSeconds = (static_cast<double>(sampleCursor_) + blockOnsets_[i].sampleOffset) / sampleRate_;
            acceptOnset(eventSeconds, blockOnsets_[i].strength);
        }
        sampleCursor_ += numSamples;
        updateState(static_cast<double>(sampleCursor_) / sampleRate_);
    }

    [[nodiscard]] RhythmState state() const noexcept { return state_; }
    [[nodiscard]] const TempoTracker& tempoTracker() const noexcept { return tempo_; }
    [[nodiscard]] std::array<RichTempoHypothesis, 3> richHypotheses() const noexcept { return tempo_.richHypotheses(lastUpdateSeconds_); }
    [[nodiscard]] double loopTempoBpm() const noexcept { return pllPeriod_ > 0.0 ? 60.0 / pllPeriod_ : 0.0; }
    [[nodiscard]] float loopConsistency() const noexcept { return pllConsistency_; }
    // Strictly future accepted PLL grid point, relative to the end of the most recent analysis block.
    // A rejected subdivision onset cannot move this forecast. Negative means the loop has no anchor yet.
    [[nodiscard]] double samplesUntilNextLoopBeat() const noexcept {
        if (!(pllPeriod_ > 0.0) || !std::isfinite(phaseAnchorSeconds_)) return -1.0;
        double phaseSeconds = std::fmod(static_cast<double>(sampleCursor_) / sampleRate_ - phaseAnchorSeconds_, pllPeriod_);
        if (phaseSeconds < 0.0) phaseSeconds += pllPeriod_;
        return (pllPeriod_ - phaseSeconds) * sampleRate_;
    }
    // Onsets found by the most recent processBlock call (for the analysis display).
    [[nodiscard]] std::size_t lastBlockOnsetCount() const noexcept { return blockOnsetCount_; }
    [[nodiscard]] const OnsetEvent& lastBlockOnset(std::size_t index) const noexcept { return blockOnsets_[index]; }
    [[nodiscard]] long long lastBlockStartSample() const noexcept { return blockStartSample_; }

private:
    // A run of mutually consistent inter-onset intervals that contradicts the current tempo (and is not just a
    // musical subdivision of it) is a real tempo change, so the loop and the histogram re-seed from it.
    bool detectTempoChange(double seconds, double& newPeriod) noexcept {
        if (std::isfinite(lastOnsetSeconds_)) {
            const double interval = seconds - lastOnsetSeconds_;
            if (interval > 0.08) {
                intervals_[intervalHead_] = interval;
                intervalHead_ = (intervalHead_ + 1) % intervals_.size();
                intervalCount_ = std::min(intervalCount_ + 1, intervals_.size());
            }
        }
        if (intervalCount_ < intervals_.size() || pllPeriod_ <= 0.0) return false;
        auto sorted = intervals_;
        std::sort(sorted.begin(), sorted.end());
        const double median = 0.5 * (sorted[1] + sorted[2]);
        for (const double v : intervals_)
            if (std::abs(v - median) > 0.08 * median) return false;
        const double ratio = median / pllPeriod_;
        for (const double related : { 1.0, 2.0, 0.5, 3.0, 1.0 / 3.0, 1.5, 2.0 / 3.0, 4.0, 0.25 })
            if (std::abs(ratio - related) < 0.045 * related) return false;
        newPeriod = median;
        return true;
    }

    void acceptOnset(double seconds, float strength) noexcept {
        tempo_.addOnset(seconds, std::max(0.15f, strength));
        double changedPeriod = 0.0;
        if (detectTempoChange(seconds, changedPeriod)) {
            pllPeriod_ = std::clamp(changedPeriod, 60.0 / 240.0, 60.0 / 40.0);
            phaseAnchorSeconds_ = seconds;
            beatCounter_ = 0;
            pllConsistency_ = 0.35f;
            pllUpdates_ = 0;
            lastObservedBeatIndex_ = std::numeric_limits<long long>::min();
            tempo_.forgetOlderEvidence(0.25f);
            intervalCount_ = 0;
        }
        const auto best = tempo_.best();
        const double histogramPeriod = 60.0 / std::max(45.0, best.bpm);

        // The inter-onset histogram is good at acquiring a tempo but slow to follow a drifting one, so a
        // phase-locked loop on the beat period tracks gradual acceleration; the histogram re-seeds it whenever the
        // loop is not locking onto anything.
        if (pllPeriod_ <= 0.0 && best.confidence >= 0.30f) {
            pllPeriod_ = histogramPeriod;
            pllConsistency_ = 0.3f;
            pllUpdates_ = 0;
        } else if (pllPeriod_ > 0.0 && pllConsistency_ < 0.30f && best.confidence >= 0.50f &&
                   std::abs(pllPeriod_ - histogramPeriod) > histogramPeriod * 0.04) {
            pllPeriod_ = histogramPeriod;
            pllConsistency_ = 0.3f;
            pllUpdates_ = 0;
        }
        const double period = pllPeriod_ > 0.0 ? pllPeriod_ : histogramPeriod;

        if (!std::isfinite(phaseAnchorSeconds_) || (best.confidence < 0.30f && pllConsistency_ < 0.30f)) {
            phaseAnchorSeconds_ = seconds;
            beatCounter_ = 0;
        } else {
            const double beatPosition = (seconds - phaseAnchorSeconds_) / period;
            const long long nearestBeat = static_cast<long long>(std::llround(beatPosition));
            const double predicted = phaseAnchorSeconds_ + static_cast<double>(nearestBeat) * period;
            const double error = seconds - predicted;
            if (std::abs(error) <= period * 0.22) {
                const long long absoluteBeat = beatCounter_ + nearestBeat;
                // Re-anchor on the grid point this onset belongs to (phase-corrected), so the next period error is
                // measured over a short horizon and the loop can follow a drifting tempo.
                phaseAnchorSeconds_ = predicted + std::clamp(error * 0.25, -0.020, 0.020);
                beatCounter_ = absoluteBeat;
                if (nearestBeat >= 1 && pllPeriod_ > 0.0) {
                    const double correction = std::clamp(0.35 * error / static_cast<double>(nearestBeat), -0.004, 0.004);
                    pllPeriod_ = std::clamp(pllPeriod_ + correction, 60.0 / 240.0, 60.0 / 40.0);
                    ++pllUpdates_;
                }
                const float onGrid = std::abs(error) < period * 0.08 ? 1.0f : 0.45f;
                pllConsistency_ += (onGrid - pllConsistency_) * 0.15f;
                if ((best.confidence >= 0.30f || pllConsistency_ >= 0.30f) && absoluteBeat != lastObservedBeatIndex_) {
                    downbeat_.observeBeatAt(absoluteBeat, std::clamp(strength, 0.0f, 1.0f));
                    lastObservedBeatIndex_ = absoluteBeat;
                }
            } else {
                pllConsistency_ *= 0.93f;
            }
        }
        lastOnsetSeconds_ = seconds;
    }

    void updateState(double nowSeconds) noexcept {
        const auto best = tempo_.best();
        // Prefer the phase-locked loop while it is locking and agrees (within a plausible drift) with the histogram.
        double targetBpm = best.bpm;
        float targetConfidence = best.confidence;
        if (pllPeriod_ > 0.0 && pllConsistency_ >= 0.30f) {
            const double pllBpm = 60.0 / pllPeriod_;
            const double ratio = pllBpm / std::max(1.0, best.bpm);
            const float evidence = std::clamp(static_cast<float>(pllUpdates_) / 8.0f, 0.0f, 1.0f);
            const float pllConfidence = std::clamp(pllConsistency_ * 1.1f, 0.0f, 1.0f) * evidence;
            // A loop that is solidly locked wins even when the (slower) histogram has wandered to an octave.
            if ((ratio > 0.70 && ratio < 1.45) || pllConsistency_ >= 0.60f) {
                targetBpm = pllBpm;
                targetConfidence = std::max(best.confidence, pllConfidence);
            }
        }
        // While still acquiring (low confidence) a clearly better estimate is taken directly; once locked, changes
        // are filtered so a momentary octave or outlier cannot yank the tempo.
        const bool acquiring = state_.tempoConfidence <= 0.25f ||
                               (state_.tempoConfidence < 0.50f && targetConfidence >= 0.45f && std::abs(targetBpm - state_.tempoBpm) > 0.12 * state_.tempoBpm);
        const double filteredTempo = acquiring
            ? targetBpm
            : state_.tempoBpm + std::clamp(targetBpm - state_.tempoBpm, -3.0, 3.0) * 0.18;
        const double dt = std::max(1.0e-3, nowSeconds - lastUpdateSeconds_);
        movement_.update(filteredTempo, nowSeconds, targetConfidence);
        state_.movement = movement_.movement();
        state_.tempoVelocity = movement_.velocityBpmPerSecond();
        previousTempo_ = filteredTempo;
        lastUpdateSeconds_ = nowSeconds;
        state_.tempoBpm = filteredTempo;
        state_.tempoConfidence = targetConfidence;

        const double period = 60.0 / std::max(45.0, state_.tempoBpm);
        long long currentBeat = beatCounter_;
        if (std::isfinite(phaseAnchorSeconds_)) {
            const double beats = (nowSeconds - phaseAnchorSeconds_) / period;
            const double floorBeats = std::floor(beats);
            currentBeat += static_cast<long long>(floorBeats);
            state_.beatPhase = beats - floorBeats;
            if (state_.beatPhase < 0.0) state_.beatPhase += 1.0;
            state_.predictedNextBeatSeconds = phaseAnchorSeconds_ + (floorBeats + 1.0) * period;
        } else {
            state_.beatPhase = 0.0;
            state_.predictedNextBeatSeconds = nowSeconds + period;
        }

        const auto bar = downbeat_.stateAt(currentBeat);
        state_.meterNumerator = bar.meterNumerator;
        state_.beatInBar = bar.currentBeatInBar;
        state_.downbeatConfidence = bar.downbeatConfidence;
        state_.meterConfidence = bar.downbeatConfidence * 0.65f; // Meter is configured, bar phase is inferred.

        float recency = 0.0f;
        if (std::isfinite(lastOnsetSeconds_)) {
            const double silence = std::max(0.0, nowSeconds - lastOnsetSeconds_);
            recency = static_cast<float>(std::clamp(1.0 - silence / (period * 3.0), 0.0, 1.0));
        }
        state_.beatConfidence = std::clamp(state_.tempoConfidence * (0.45f + 0.55f * recency), 0.0f, 1.0f);
        // A lock needs recent onsets; the tempo histogram alone would keep a stale lock through silence.
        // Hysteresis (SPEC 13): entering needs clearly better confidence than staying locked.
        if (lockLatched_)
            lockLatched_ = recency > 0.0f && state_.tempoConfidence >= 0.28f && state_.beatConfidence >= 0.20f;
        else
            lockLatched_ = recency > 0.0f && state_.tempoConfidence >= 0.38f && state_.beatConfidence >= 0.30f;
        state_.locked = lockLatched_;
    }

    double sampleRate_{48000.0};
    long long sampleCursor_{0};
    double phaseAnchorSeconds_{std::numeric_limits<double>::quiet_NaN()};
    double lastOnsetSeconds_{std::numeric_limits<double>::quiet_NaN()};
    double lastUpdateSeconds_{0.0};
    double previousTempo_{120.0};
    long long lastObservedBeatIndex_{std::numeric_limits<long long>::min()};
    TempoMovementClassifier movement_{};
    std::array<double, 4> intervals_{};
    std::size_t intervalHead_{0};
    std::size_t intervalCount_{0};
    long long beatCounter_{0};
    double pllPeriod_{0.0};
    float pllConsistency_{0.0f};
    int pllUpdates_{0};
    bool lockLatched_{false};
    AdaptiveOnsetDetector detector_{};
    TempoTracker tempo_{};
    DownbeatTracker downbeat_{};
    RhythmState state_{};
    std::array<OnsetEvent, 32> blockOnsets_{};
    std::size_t blockOnsetCount_{0};
    long long blockStartSample_{0};
};

} // namespace robodrummer
