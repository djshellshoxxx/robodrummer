#pragma once
#include "analysis/FollowResponseProfile.h"
#include "analysis/TimingAuthorityController.h"
#include <algorithm>
#include <cmath>

namespace robodrummer {

struct PhaseFollowResult {
    long long correctionSamples{0};
    double phaseErrorCycles{0.0};
    bool hardResyncRecommended{false};
};

class PhaseFollower {
public:
    [[nodiscard]] PhaseFollowResult update(double drummerPhase,
                                           const RhythmState& guitar,
                                           double effectiveBpm,
                                           float guitarAuthority,
                                           FollowResponse response,
                                           double sampleRate) const noexcept {
        if (!guitar.locked || guitarAuthority < 0.10f || !(sampleRate > 0.0) || !(effectiveBpm > 0.0))
            return {};

        const double error = wrapHalf(guitar.beatPhase - drummerPhase);
        const double absError = std::abs(error);
        const bool hard = absError > 0.30;
        if (hard) return {0, error, true};

        const double periodSamples = sampleRate * 60.0 / effectiveBpm;
        const double gain = followResponseProfile(response).phaseGain * std::clamp(static_cast<double>(guitarAuthority), 0.0, 1.0);
        const double requested = error * periodSamples * gain;
        const double maxCorrection = sampleRate * followResponseProfile(response).maxPhaseCorrectionSeconds;
        const auto bounded = static_cast<long long>(std::llround(std::clamp(requested, -maxCorrection, maxCorrection)));
        return {bounded, error, false};
    }

private:
    static double wrapHalf(double value) noexcept {
        if (!std::isfinite(value)) return 0.0;
        value -= std::floor(value + 0.5);
        return value;
    }
};

} // namespace robodrummer
