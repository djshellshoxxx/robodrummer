// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <algorithm>
#include <cmath>

namespace robodrummer {

// SPEC 10: the scheduler lookahead follows the host buffer size and how much the tracker is trusted. A
// confident tracker can schedule close to the beat; a doubtful one keeps a larger cushion for corrections.
[[nodiscard]] inline double recommendedLookaheadMs(int blockSamples, double sampleRate, float trackingConfidence) noexcept {
    if (!(sampleRate > 0.0) || blockSamples <= 0) return 0.0;
    const double blockMs = 1000.0 * static_cast<double>(blockSamples) / sampleRate;
    const double confidence = std::clamp(static_cast<double>(std::isfinite(trackingConfidence) ? trackingConfidence : 0.0f), 0.0, 1.0);
    return blockMs + 2.0 + (1.0 - confidence) * 25.0;
}

} // namespace robodrummer
