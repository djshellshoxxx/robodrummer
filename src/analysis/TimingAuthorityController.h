#pragma once
#include "analysis/FollowResponseProfile.h"
#include "analysis/RhythmAnalyzer.h"
#include <algorithm>
#include <cmath>

namespace robodrummer {

enum class LeadershipMode { DrummerLeads, Hybrid, GuitaristLeads };

struct TimingAuthoritySettings {
    LeadershipMode mode{LeadershipMode::DrummerLeads};
    float leadership{0.5f};
    double followRangeBpm{15.0};
    FollowResponse response{FollowResponse::Balanced};
};

struct TimingAuthorityState {
    double outputBpm{120.0};
    float effectiveGuitarAuthority{0.0f};
    bool usingGuitarTiming{false};
};

class TimingAuthorityController {
public:
    void reset(double bpm = 120.0) noexcept {
        currentBpm_ = sanitizeBpm(bpm);
        trustedGuitarBpm_ = currentBpm_;
        hasTrustedGuitar_ = false;
        initialized_ = false;
        smoothedVelocity_ = 0.0;
        pendingBpm_ = 0.0;
        pendingCount_ = 0;
    }

    // Low-passed guitar tempo velocity in BPM/s (positive = accelerating).
    [[nodiscard]] double smoothedTempoVelocity() const noexcept { return smoothedVelocity_; }

    [[nodiscard]] TimingAuthorityState update(double baseBpm,
                                               const RhythmState& guitar,
                                               const TimingAuthoritySettings& settings,
                                               double deltaSeconds) noexcept {
        baseBpm = sanitizeBpm(baseBpm);
        deltaSeconds = std::clamp(std::isfinite(deltaSeconds) ? deltaSeconds : 0.0, 0.0, 0.5);

        float leadership = std::clamp(settings.leadership, 0.0f, 1.0f);
        if (settings.mode == LeadershipMode::DrummerLeads) leadership = 0.0f;
        if (settings.mode == LeadershipMode::GuitaristLeads) leadership = 1.0f;

        const float confidenceAuthority = confidenceGain(guitar.tempoConfidence, guitar.beatConfidence, guitar.locked);
        const float effectiveAuthority = leadership * confidenceAuthority;

        const auto profile = followResponseProfile(settings.response);

        if (guitar.locked && guitar.tempoConfidence >= 0.60f && guitar.beatConfidence >= 0.50f) {
            const double candidate = sanitizeBpm(guitar.tempoBpm);
            if (!hasTrustedGuitar_ || std::abs(candidate - trustedGuitarBpm_) <= profile.outlierRejectionBpm) {
                trustedGuitarBpm_ = candidate;
                hasTrustedGuitar_ = true;
                pendingCount_ = 0;
            } else {
                // Outlier: only accept once several consecutive updates agree on the new tempo.
                if (pendingCount_ > 0 && std::abs(candidate - pendingBpm_) <= 3.0) ++pendingCount_;
                else { pendingBpm_ = candidate; pendingCount_ = 1; }
                if (pendingCount_ >= profile.outlierPersistUpdates) {
                    trustedGuitarBpm_ = candidate;
                    pendingCount_ = 0;
                }
            }
        }

        const double velocity = std::isfinite(guitar.tempoVelocity) ? std::clamp(guitar.tempoVelocity, -50.0, 50.0) : 0.0;
        if (deltaSeconds > 0.0) {
            const double a = 1.0 - std::exp(-deltaSeconds / 1.0);
            smoothedVelocity_ += (velocity - smoothedVelocity_) * a;
        }

        double guitarTarget = hasTrustedGuitar_ ? trustedGuitarBpm_ : sanitizeBpm(guitar.tempoBpm);
        const double range = std::clamp(settings.followRangeBpm, 0.0, 80.0);
        guitarTarget = std::clamp(guitarTarget, baseBpm - range, baseBpm + range);
        const double desired = baseBpm + (guitarTarget - baseBpm) * effectiveAuthority;

        if (!initialized_) {
            currentBpm_ = baseBpm;
            initialized_ = true;
        }

        double maxRate = profile.smoothingRateBpmPerSecond;
        if (profile.accelerationSensitivity > 0.0 && guitar.locked) {
            const double gap = desired - currentBpm_;
            if (gap * smoothedVelocity_ > 0.0 && std::abs(smoothedVelocity_) > 0.5)
                maxRate *= 1.0 + profile.accelerationSensitivity * std::min(1.0, std::abs(smoothedVelocity_) / 4.0);
        }
        const double maxStep = maxRate * deltaSeconds;
        if (maxStep > 0.0)
            currentBpm_ += std::clamp(desired - currentBpm_, -maxStep, maxStep);
        else
            currentBpm_ = desired;

        currentBpm_ = sanitizeBpm(currentBpm_);
        return {currentBpm_, effectiveAuthority, effectiveAuthority >= 0.10f};
    }

private:
    static double sanitizeBpm(double bpm) noexcept {
        return std::clamp(std::isfinite(bpm) ? bpm : 120.0, 20.0, 400.0);
    }

    static float confidenceGain(float tempoConfidence, float beatConfidence, bool locked) noexcept {
        const float c = std::clamp(std::min(tempoConfidence, beatConfidence), 0.0f, 1.0f);
        if (!locked || c < 0.20f) return 0.0f;
        if (c < 0.40f) return 0.15f * (c - 0.20f) / 0.20f;
        if (c < 0.60f) return 0.15f + 0.35f * (c - 0.40f) / 0.20f;
        if (c < 0.80f) return 0.50f + 0.30f * (c - 0.60f) / 0.20f;
        return 0.80f + 0.20f * (c - 0.80f) / 0.20f;
    }

    double currentBpm_{120.0};
    double trustedGuitarBpm_{120.0};
    bool hasTrustedGuitar_{false};
    bool initialized_{false};
    double smoothedVelocity_{0.0};
    double pendingBpm_{0.0};
    int pendingCount_{0};
};

} // namespace robodrummer
