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

// Tap tempo: averages the most recent tap intervals; a long pause or a wildly different interval starts over.
class TapTempo {
public:
    static constexpr std::size_t MaxTaps = 8;

    void reset() noexcept { count_ = 0; }

    [[nodiscard]] std::optional<double> tap(double seconds) noexcept {
        if (!std::isfinite(seconds)) return std::nullopt;
        if (count_ > 0) {
            const double gap = seconds - last();
            if (gap <= 0.0 || gap > 2.5) count_ = 0;
            else if (count_ >= 2) {
                const double average = (last() - times_[0]) / static_cast<double>(count_ - 1);
                if (gap > average * 1.8 || gap < average * 0.55) count_ = 0;
            }
        }
        if (count_ == MaxTaps) {
            std::copy(times_.begin() + 1, times_.end(), times_.begin());
            --count_;
        }
        times_[count_++] = seconds;
        if (count_ < 2) return std::nullopt;
        const double average = (last() - times_[0]) / static_cast<double>(count_ - 1);
        return std::clamp(60.0 / average, 40.0, 240.0);
    }

private:
    [[nodiscard]] double last() const noexcept { return times_[count_ - 1]; }

    std::array<double, MaxTaps> times_{};
    std::size_t count_{0};
};

} // namespace robodrummer
