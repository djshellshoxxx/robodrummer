#pragma once
#include "analysis/DownbeatTracker.h"
#include "analysis/OnsetDetector.h"
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
        if (mono == nullptr || numSamples <= 0) return;
        std::array<OnsetEvent, 32> events{};
        const auto count = detector_.processBlock(mono, numSamples, events.data(), events.size());
        for (std::size_t i = 0; i < count; ++i) {
            const double eventSeconds = (static_cast<double>(sampleCursor_) + events[i].sampleOffset) / sampleRate_;
            acceptOnset(eventSeconds, events[i].strength);
        }
        sampleCursor_ += numSamples;
        updateState(static_cast<double>(sampleCursor_) / sampleRate_);
    }

    [[nodiscard]] RhythmState state() const noexcept { return state_; }
    [[nodiscard]] const TempoTracker& tempoTracker() const noexcept { return tempo_; }

private:
    void acceptOnset(double seconds, float strength) noexcept {
        tempo_.addOnset(seconds, std::max(0.15f, strength));
        const auto best = tempo_.best();
        const double period = 60.0 / std::max(45.0, best.bpm);

        if (!std::isfinite(phaseAnchorSeconds_) || best.confidence < 0.30f) {
            phaseAnchorSeconds_ = seconds;
        } else {
            const double beatPosition = (seconds - phaseAnchorSeconds_) / period;
            const long long nearestBeat = static_cast<long long>(std::llround(beatPosition));
            const double predicted = phaseAnchorSeconds_ + static_cast<double>(nearestBeat) * period;
            const double error = seconds - predicted;
            if (std::abs(error) <= period * 0.22) {
                phaseAnchorSeconds_ += std::clamp(error * 0.25, -0.020, 0.020);
                if (best.confidence >= 0.30f && nearestBeat != lastObservedBeatIndex_) {
                    downbeat_.observeBeatAt(nearestBeat, std::clamp(strength, 0.0f, 1.0f));
                    lastObservedBeatIndex_ = nearestBeat;
                }
            }
        }
        lastOnsetSeconds_ = seconds;
    }

    void updateState(double nowSeconds) noexcept {
        const auto best = tempo_.best();
        const double filteredTempo = state_.tempoConfidence > 0.25f
            ? state_.tempoBpm + std::clamp(best.bpm - state_.tempoBpm, -3.0, 3.0) * 0.18
            : best.bpm;
        const double dt = std::max(1.0e-3, nowSeconds - lastUpdateSeconds_);
        state_.tempoVelocity = (filteredTempo - previousTempo_) / dt;
        previousTempo_ = filteredTempo;
        lastUpdateSeconds_ = nowSeconds;
        state_.tempoBpm = filteredTempo;
        state_.tempoConfidence = best.confidence;

        const double period = 60.0 / std::max(45.0, state_.tempoBpm);
        if (std::isfinite(phaseAnchorSeconds_)) {
            const double beats = (nowSeconds - phaseAnchorSeconds_) / period;
            const double floorBeats = std::floor(beats);
            state_.beatPhase = beats - floorBeats;
            if (state_.beatPhase < 0.0) state_.beatPhase += 1.0;
            state_.predictedNextBeatSeconds = phaseAnchorSeconds_ + (floorBeats + 1.0) * period;
        } else {
            state_.beatPhase = 0.0;
            state_.predictedNextBeatSeconds = nowSeconds + period;
        }

        const auto bar = downbeat_.state();
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
        state_.locked = recency > 0.0f && state_.tempoConfidence >= 0.38f && state_.beatConfidence >= 0.30f;
    }

    double sampleRate_{48000.0};
    long long sampleCursor_{0};
    double phaseAnchorSeconds_{std::numeric_limits<double>::quiet_NaN()};
    double lastOnsetSeconds_{std::numeric_limits<double>::quiet_NaN()};
    double lastUpdateSeconds_{0.0};
    double previousTempo_{120.0};
    long long lastObservedBeatIndex_{std::numeric_limits<long long>::min()};
    AdaptiveOnsetDetector detector_{};
    TempoTracker tempo_{};
    DownbeatTracker downbeat_{};
    RhythmState state_{};
};

} // namespace robodrummer
