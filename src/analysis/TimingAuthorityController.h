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
    // SPEC 12 controls. The defaults reproduce the behaviour of the response preset alone.
    float followStrength{1.0f};          // 0-1 scales how much of the guitar's authority is applied
    float tempoInertia{0.5f};            // 0 = nimble, 1 = heavy (scales the tempo slew rate; 0.5 = preset)
    float confidenceSensitivity{0.5f};   // 0 = demands strong evidence, 1 = trusts weak evidence (0.5 = preset)
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
        guitarOffset_ = 0.0;
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

        const float sensitivity = std::clamp(settings.confidenceSensitivity, 0.0f, 1.0f);
        const float confidenceAuthority = confidenceGain(guitar.tempoConfidence * (0.7f + 0.6f * sensitivity),
                                                         guitar.beatConfidence * (0.7f + 0.6f * sensitivity), guitar.locked);
        const float effectiveAuthority = leadership * confidenceAuthority * std::clamp(settings.followStrength, 0.0f, 1.0f);

        if (guitar.locked && guitar.tempoConfidence >= 0.60f && guitar.beatConfidence >= 0.50f) {
            trustedGuitarBpm_ = sanitizeBpm(guitar.tempoBpm);
            hasTrustedGuitar_ = true;
        }

        double guitarTarget = hasTrustedGuitar_ ? trustedGuitarBpm_ : sanitizeBpm(guitar.tempoBpm);
        const double range = std::clamp(settings.followRangeBpm, 0.0, 80.0);
        guitarTarget = std::clamp(guitarTarget, baseBpm - range, baseBpm + range);
        // Only the guitar's contribution is slew-limited. The base tempo (host, MIDI clock, tap or manual BPM) is
        // authoritative and passes straight through, so a host tempo change is never smeared by the follow response.
        const double desiredOffset = (guitarTarget - baseBpm) * effectiveAuthority;
        const double inertiaScale = std::pow(2.0, (0.5 - static_cast<double>(std::clamp(settings.tempoInertia, 0.0f, 1.0f))) * 2.0);
        const double maxRate = responseRate(settings.response) * inertiaScale;
        const double maxStep = maxRate * deltaSeconds;
        if (maxStep > 0.0)
            guitarOffset_ += std::clamp(desiredOffset - guitarOffset_, -maxStep, maxStep);
        else
            guitarOffset_ = desiredOffset;

        currentBpm_ = sanitizeBpm(baseBpm + guitarOffset_);
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
    double guitarOffset_{0.0};
};

} // namespace robodrummer
