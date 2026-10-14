// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

// SPEC 11: the timing model must tell apart jitter, gradual drift, intentional acceleration/deceleration, an
// abrupt tempo shift and half/double-time reinterpretation.
enum class TempoMovement : std::uint8_t {
    Steady,
    Jitter,
    Drift,
    Accelerating,
    Decelerating,
    AbruptShift,
    HalfDoubleReinterpretation
};

// Samples the tracked tempo a few times per second and classifies the recent movement.
class TempoMovementClassifier {
public:
    static constexpr std::size_t Capacity = 48;       // 12 s at 4 samples per second
    static constexpr double SampleInterval = 0.25;

    void reset() noexcept {
        count_ = 0; head_ = 0; lastSampleTime_ = -1.0e9; movement_ = TempoMovement::Steady;
        velocity_ = 0.0; holdUntil_ = -1.0e9;
    }

    // Call regularly with the current tempo and the time; only samples when confident enough.
    void update(double bpm, double nowSeconds, float confidence) noexcept {
        if (!std::isfinite(bpm) || bpm <= 0.0 || confidence < 0.30f) return;
        if (nowSeconds - lastSampleTime_ < SampleInterval) return;
        lastSampleTime_ = nowSeconds;
        times_[head_] = nowSeconds;
        values_[head_] = bpm;
        head_ = (head_ + 1) % Capacity;
        count_ = std::min(count_ + 1, Capacity);
        classify(nowSeconds);
    }

    [[nodiscard]] TempoMovement movement() const noexcept { return movement_; }
    // Least-squares tempo slope over the last few seconds, in BPM per second (positive = accelerating).
    [[nodiscard]] double velocityBpmPerSecond() const noexcept { return velocity_; }

private:
    [[nodiscard]] double timeAt(std::size_t age) const noexcept { return times_[(head_ + Capacity - 1 - age) % Capacity]; }
    [[nodiscard]] double valueAt(std::size_t age) const noexcept { return values_[(head_ + Capacity - 1 - age) % Capacity]; }

    void classify(double now) noexcept {
        if (count_ < 6) { movement_ = TempoMovement::Steady; velocity_ = 0.0; return; }
        const double current = valueAt(0);

        // Half/double-time reinterpretation: the tempo jumped by (about) a factor of two.
        const std::size_t lookback = std::min<std::size_t>(count_ - 1, 8); // ~2 s
        const double earlier = valueAt(lookback);
        const double ratio = current / std::max(1.0, earlier);
        if (std::abs(ratio - 2.0) < 0.12 || std::abs(ratio - 0.5) < 0.06) {
            movement_ = TempoMovement::HalfDoubleReinterpretation;
            holdUntil_ = now + 3.0;
            return;
        }
        // Abrupt shift: a big step over ~1.5 s that is not a gradual slope.
        const std::size_t shortBack = std::min<std::size_t>(count_ - 1, 6);
        const double stepBpm = std::abs(current - valueAt(shortBack));
        if (stepBpm > std::max(7.0, current * 0.06)) {
            movement_ = TempoMovement::AbruptShift;
            holdUntil_ = now + 3.0;
            return;
        }
        if (now < holdUntil_ && (movement_ == TempoMovement::AbruptShift || movement_ == TempoMovement::HalfDoubleReinterpretation))
            return;

        // Slope and residual over the last ~6 s.
        const std::size_t n = std::min<std::size_t>(count_, 24);
        double sumT = 0.0, sumV = 0.0;
        for (std::size_t i = 0; i < n; ++i) { sumT += timeAt(i); sumV += valueAt(i); }
        const double meanT = sumT / static_cast<double>(n), meanV = sumV / static_cast<double>(n);
        double covariance = 0.0, variance = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            covariance += (timeAt(i) - meanT) * (valueAt(i) - meanV);
            variance += (timeAt(i) - meanT) * (timeAt(i) - meanT);
        }
        const double slope = variance > 1.0e-9 ? covariance / variance : 0.0;
        double residual = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double fit = meanV + slope * (timeAt(i) - meanT);
            residual += (valueAt(i) - fit) * (valueAt(i) - fit);
        }
        residual = std::sqrt(residual / static_cast<double>(n));
        velocity_ = slope;

        const double relativeSlope = slope / std::max(1.0, current) * 60.0; // percent-ish per minute scale
        if (slope > 0.35 && relativeSlope > 0.25) movement_ = TempoMovement::Accelerating;
        else if (slope < -0.35 && relativeSlope < -0.25) movement_ = TempoMovement::Decelerating;
        else if (std::abs(slope) > 0.07) movement_ = TempoMovement::Drift;
        else if (residual > 1.1) movement_ = TempoMovement::Jitter;
        else movement_ = TempoMovement::Steady;
    }

    std::array<double, Capacity> times_{};
    std::array<double, Capacity> values_{};
    std::size_t count_{0};
    std::size_t head_{0};
    double lastSampleTime_{-1.0e9};
    double velocity_{0.0};
    double holdUntil_{-1.0e9};
    TempoMovement movement_{TempoMovement::Steady};
};

} // namespace robodrummer
