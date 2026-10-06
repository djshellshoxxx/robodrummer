#pragma once
#include "analysis/RhythmAnalyzer.h"
#include <algorithm>
#include <cmath>

namespace robodrummer {

enum class LeadershipMode { DrummerLeads, Hybrid, GuitaristLeads };
enum class FollowResponse { VeryStable, Stable, Balanced, Responsive, VeryResponsive };

struct TimingAuthoritySettings {
    LeadershipMode mode{LeadershipMode::DrummerLeads};
    float leadership{0.5f};
    double followRangeBpm{15.0};
    FollowResponse response{FollowResponse::Balanced};
    float followStrength{1.0f};
    float tempoInertia{0.5f};
    float beatCorrectionSpeed{0.5f};
    float confidenceSensitivity{0.5f};
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
    }

    [[nodiscard]] TimingAuthorityState update(double baseBpm,
                                               const RhythmState& guitar,
                                               const TimingAuthoritySettings& settings,
                                               double deltaSeconds) noexcept {
        baseBpm = sanitizeBpm(baseBpm);
        deltaSeconds = std::clamp(std::isfinite(deltaSeconds) ? deltaSeconds : 0.0, 0.0, 0.5);

        float leadership = std::clamp(settings.leadership, 0.0f, 1.0f);
        if (settings.mode == LeadershipMode::DrummerLeads) leadership = 0.0f;
        if (settings.mode == LeadershipMode::GuitaristLeads) leadership = 1.0f;

        const float confidenceAuthority = confidenceGain(
            guitar.tempoConfidence,
            guitar.beatConfidence,
            guitar.locked,
            settings.confidenceSensitivity);
        const float followStrength = std::clamp(settings.followStrength, 0.0f, 1.0f);
        const float effectiveAuthority = leadership * followStrength * confidenceAuthority;

        if (guitar.locked && guitar.tempoConfidence >= 0.60f && guitar.beatConfidence >= 0.50f) {
            trustedGuitarBpm_ = sanitizeBpm(guitar.tempoBpm);
            hasTrustedGuitar_ = true;
        }

        double guitarTarget = hasTrustedGuitar_ ? trustedGuitarBpm_ : sanitizeBpm(guitar.tempoBpm);
        const double range = std::clamp(settings.followRangeBpm, 0.0, 80.0);
        guitarTarget = std::clamp(guitarTarget, baseBpm - range, baseBpm + range);
        const double desired = baseBpm + (guitarTarget - baseBpm) * effectiveAuthority;

        if (!initialized_) {
            currentBpm_ = baseBpm;
            initialized_ = true;
        }

        const double inertia = std::clamp(static_cast<double>(settings.tempoInertia), 0.0, 1.0);
        const double inertiaMultiplier = 1.75 - 1.50 * inertia;
        const double maxRate = responseRate(settings.response) * inertiaMultiplier;
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

    static float confidenceGain(float tempoConfidence,
                                float beatConfidence,
                                bool locked,
                                float sensitivity) noexcept {
        const float sensitivityOffset = (0.5f - std::clamp(sensitivity, 0.0f, 1.0f)) * 0.20f;
        const float c = std::clamp(std::min(tempoConfidence, beatConfidence) + sensitivityOffset, 0.0f, 1.0f);
        if (!locked || c < 0.20f) return 0.0f;
        if (c < 0.40f) return 0.15f * (c - 0.20f) / 0.20f;
        if (c < 0.60f) return 0.15f + 0.35f * (c - 0.40f) / 0.20f;
        if (c < 0.80f) return 0.50f + 0.30f * (c - 0.60f) / 0.20f;
        return 0.80f + 0.20f * (c - 0.80f) / 0.20f;
    }

    static double responseRate(FollowResponse response) noexcept {
        switch (response) {
            case FollowResponse::VeryStable: return 1.5;
            case FollowResponse::Stable: return 3.0;
            case FollowResponse::Balanced: return 6.0;
            case FollowResponse::Responsive: return 12.0;
            case FollowResponse::VeryResponsive: return 24.0;
        }
        return 6.0;
    }

    double currentBpm_{120.0};
    double trustedGuitarBpm_{120.0};
    bool hasTrustedGuitar_{false};
    bool initialized_{false};
};

} // namespace robodrummer
