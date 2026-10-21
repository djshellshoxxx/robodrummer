// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
// Shared, JUCE-free, allocation-free helpers for the drum-bus effects.
#include <algorithm>
#include <cmath>

namespace robodrummer::dsp {

inline constexpr float kLn10Over20 = 0.115129254649702f; // dB -> linear: exp(dB * kLn10Over20)

// NaN-safe clamp: a non-finite value becomes `def` before clamping (std::clamp would pass NaN through).
[[nodiscard]] inline float clampParam(float v, float lo, float hi, float def) noexcept {
    if (!std::isfinite(v)) v = def;
    return v < lo ? lo : (v > hi ? hi : v);
}
[[nodiscard]] inline float finiteOr0(float x) noexcept { return std::isfinite(x) ? x : 0.0f; }
[[nodiscard]] inline float flushDenormal(float x, float threshold = 1.0e-20f) noexcept {
    return std::fabs(x) < threshold ? 0.0f : x;
}
[[nodiscard]] inline float clampSignal(float x) noexcept { return x < -1.0e4f ? -1.0e4f : (x > 1.0e4f ? 1.0e4f : x); }
[[nodiscard]] inline double sanitiseSampleRate(double sr) noexcept {
    return (std::isfinite(sr) && sr >= 8000.0 && sr <= 768000.0) ? sr : 44100.0;
}
// One-pole envelope coefficient for a 1-1/e time constant of `ms` milliseconds.
[[nodiscard]] inline float timeCoef(double sr, double ms) noexcept {
    return static_cast<float>(std::exp(-1.0 / (std::max(ms, 1.0e-3) * 0.001 * sr)));
}

// Per-sample exponential parameter smoother; snaps onto the target so a settled value is exact.
struct ParamSmoother {
    float value{0.0f}, target{0.0f}, coeff{1.0f};
    void setTime(double sr, double ms) noexcept { coeff = 1.0f - timeCoef(sr, ms); }
    void setTarget(float t) noexcept { target = t; }
    void snap(float t) noexcept { value = target = t; }
    [[nodiscard]] float next() noexcept {
        if (value != target) {
            value += (target - value) * coeff;
            if (std::fabs(target - value) < 1.0e-6f) value = target;
        }
        return value;
    }
    [[nodiscard]] bool settled() const noexcept { return value == target; }
};

// Linear ~5 ms crossfade between unprocessed (0) and processed (1).
struct EnableFader {
    float g{0.0f}, target{0.0f}, step{0.01f};
    void prepare(double sr) noexcept { step = static_cast<float>(1.0 / std::max(1.0, 0.005 * sr)); }
    void set(bool on) noexcept { target = on ? 1.0f : 0.0f; }
    void snap(bool on) noexcept { g = target = on ? 1.0f : 0.0f; }
    [[nodiscard]] float next() noexcept {
        if (g < target) g = std::min(target, g + step);
        else if (g > target) g = std::max(target, g - step);
        return g;
    }
    [[nodiscard]] bool offAndSettled() const noexcept { return g == 0.0f && target == 0.0f; }
};

// Final stage: crossfade + denormal flush. `proc` is the processed sample, `in` the unprocessed one.
[[nodiscard]] inline float crossfade(float in, float proc, float f) noexcept {
    const float out = f >= 1.0f ? proc : in * (1.0f - f) + proc * f;
    return std::fabs(out) < 1.0e-30f ? 0.0f : out;
}

} // namespace robodrummer::dsp
