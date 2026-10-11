// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once

namespace robodrummer {

enum class FollowResponse { VeryStable, Stable, Balanced, Responsive, VeryResponsive };

// SPEC 11: a follow-response preset maps to smoothing, phase-correction limits,
// acceleration sensitivity and outlier rejection. Balanced reproduces the original behaviour exactly.
struct FollowResponseProfile {
    double smoothingRateBpmPerSecond{6.0};   // max tempo slew rate
    double phaseGain{0.11};                  // fraction of phase error corrected per update
    double maxPhaseCorrectionSeconds{0.004}; // per-update phase nudge limit
    double accelerationSensitivity{0.0};     // 0 = off; extra slew multiplier (up to 1+x) while tempo ramps consistently
    double outlierRejectionBpm{1.0e9};       // guitar tempo jumps beyond this need persistence to be trusted
    int outlierPersistUpdates{8};            // consecutive agreeing updates that turn an outlier into a real shift
};

[[nodiscard]] constexpr FollowResponseProfile followResponseProfile(FollowResponse r) noexcept {
    switch (r) {
        case FollowResponse::VeryStable:     return {1.5, 0.04, 0.0015, 0.0, 6.0, 12};
        case FollowResponse::Stable:         return {3.0, 0.07, 0.0025, 0.0, 10.0, 10};
        case FollowResponse::Balanced:       return {6.0, 0.11, 0.0040, 0.0, 1.0e9, 8};
        case FollowResponse::Responsive:     return {12.0, 0.17, 0.0060, 0.5, 1.0e9, 8};
        case FollowResponse::VeryResponsive: return {24.0, 0.24, 0.0090, 1.0, 1.0e9, 8};
    }
    return {};
}

} // namespace robodrummer
