#pragma once
#include <algorithm>
#include <cmath>

namespace robodrummer {

enum class TempoMovement {
    Stable = 0,
    Jitter,
    Drift,
    Accelerating,
    Decelerating,
    AbruptShift,
    HalfTimeReinterpretation,
    DoubleTimeReinterpretation
};

class TempoMovementClassifier {
public:
    void reset(double bpm = 120.0) noexcept {
        previousBpm_ = sanitize(bpm);
        initialized_ = true;
    }

    [[nodiscard]] TempoMovement update(double bpm, double tempoVelocity, double deltaSeconds) noexcept {
        bpm = sanitize(bpm);
        tempoVelocity = std::isfinite(tempoVelocity) ? tempoVelocity : 0.0;
        deltaSeconds = std::clamp(std::isfinite(deltaSeconds) ? deltaSeconds : 0.0, 0.0, 2.0);

        if (!initialized_) {
            reset(bpm);
            return TempoMovement::Stable;
        }

        const double previous = previousBpm_;
        previousBpm_ = bpm;
        const double ratio = previous > 0.0 ? bpm / previous : 1.0;
        const double delta = bpm - previous;

        if (near(ratio, 0.5, 0.08))
            return TempoMovement::HalfTimeReinterpretation;
        if (near(ratio, 2.0, 0.12))
            return TempoMovement::DoubleTimeReinterpretation;

        const double abruptThreshold = std::max(8.0, previous * 0.08);
        if (std::abs(delta) >= abruptThreshold)
            return TempoMovement::AbruptShift;

        const double effectiveVelocity = deltaSeconds > 1.0e-6
            ? 0.55 * tempoVelocity + 0.45 * (delta / deltaSeconds)
            : tempoVelocity;

        if (effectiveVelocity >= 2.5)
            return TempoMovement::Accelerating;
        if (effectiveVelocity <= -2.5)
            return TempoMovement::Decelerating;
        if (std::abs(effectiveVelocity) >= 0.55)
            return TempoMovement::Drift;
        if (std::abs(delta) >= 0.55)
            return TempoMovement::Jitter;
        return TempoMovement::Stable;
    }

private:
    static double sanitize(double bpm) noexcept {
        return std::clamp(std::isfinite(bpm) ? bpm : 120.0, 20.0, 400.0);
    }

    static bool near(double value, double target, double tolerance) noexcept {
        return std::abs(value - target) <= tolerance;
    }

    double previousBpm_{120.0};
    bool initialized_{false};
};

} // namespace robodrummer
