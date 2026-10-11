// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace robodrummer {

class TapTempo {
public:
    static constexpr double ResetGapSeconds = 2.0;
    static constexpr double MinBpm = 40.0;
    static constexpr double MaxBpm = 240.0;
    static constexpr std::size_t MaxIntervals = 8;

    void reset() noexcept { count_ = 0; hasLast_ = false; }

    // Feed a tap timestamp in seconds. Returns BPM once at least 2 taps are available.
    [[nodiscard]] std::optional<double> tap(double seconds) noexcept {
        if (!std::isfinite(seconds)) return std::nullopt;
        if (hasLast_) {
            const double dt = seconds - last_;
            if (dt <= 0.0) return currentBpm();      // duplicate/out-of-order tap ignored
            if (dt > ResetGapSeconds) count_ = 0;     // long gap starts a new series
            else push(dt);
        }
        last_ = seconds;
        hasLast_ = true;
        return currentBpm();
    }

    [[nodiscard]] std::optional<double> currentBpm() const noexcept {
        if (count_ == 0) return std::nullopt;
        std::array<double, MaxIntervals> v{};
        std::copy_n(ring_.begin(), count_, v.begin());
        std::sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(count_));
        const double median = (count_ % 2) ? v[count_ / 2] : 0.5 * (v[count_ / 2 - 1] + v[count_ / 2]);
        double sum = 0.0; int n = 0;
        for (std::size_t i = 0; i < count_; ++i)
            if (std::abs(v[i] - median) <= 0.25 * median) { sum += v[i]; ++n; } // reject outliers
        const double mean = n > 0 ? sum / n : median;
        if (!(mean > 0.0)) return std::nullopt;
        return std::clamp(60.0 / mean, MinBpm, MaxBpm);
    }

    [[nodiscard]] std::size_t intervalCount() const noexcept { return count_; }

private:
    void push(double dt) noexcept {
        if (count_ < MaxIntervals) { ring_[count_++] = dt; return; }
        std::rotate(ring_.begin(), ring_.begin() + 1, ring_.end());
        ring_[MaxIntervals - 1] = dt;
    }

    std::array<double, MaxIntervals> ring_{};
    std::size_t count_{0};
    double last_{0.0};
    bool hasLast_{false};
};

} // namespace robodrummer
