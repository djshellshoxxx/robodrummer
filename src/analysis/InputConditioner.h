// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace robodrummer {

enum class InputMode { AutoSum, LeftOnly, RightOnly };

// Prepares the guitar signal for analysis only (the audible guitar is never altered): channel selection,
// DC removal, high-pass, manual or automatic gain normalisation and an optional noise gate.
class InputConditioner {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        // 2nd-order Butterworth high-pass at 70 Hz (below the lowest guitar string, above hum/rumble).
        const double w0 = 6.283185307179586 * 70.0 / sampleRate_;
        const double alpha = std::sin(w0) / (2.0 * 0.7071067811865476);
        const double c = std::cos(w0);
        const double a0 = 1.0 + alpha;
        b0_ = (1.0 + c) / 2.0 / a0; b1_ = -(1.0 + c) / a0; b2_ = b0_;
        a1_ = -2.0 * c / a0; a2_ = (1.0 - alpha) / a0;
        envAttack_ = 1.0 - std::exp(-1.0 / (sampleRate_ * 0.0005));
        envRelease_ = 1.0 - std::exp(-1.0 / (sampleRate_ * 0.08));
        peakDecay_ = std::exp(-1.0 / (sampleRate_ * 3.0));
        gateOpenCoef_ = 1.0 - std::exp(-1.0 / (sampleRate_ * 0.003));
        gateCloseCoef_ = 1.0 - std::exp(-1.0 / (sampleRate_ * 0.030));
        reset();
    }

    void reset() noexcept {
        z1_ = z2_ = dcX_ = dcY_ = 0.0;
        env_ = 0.0; peak_ = 0.0; autoGain_ = 1.0; gate_ = 1.0; gateOpenState_ = true;
        levelPeak_ = 0.0f; levelRms_ = 0.0f;
    }

    void setMode(InputMode mode) noexcept { mode_ = mode; }
    void setGainDb(float db) noexcept { manualGain_ = std::pow(10.0, std::clamp(db, -24.0f, 36.0f) / 20.0); }
    void setAutoGain(bool enabled) noexcept { autoGainEnabled_ = enabled; }
    // At or below -80 dB the gate is bypassed.
    void setGateThresholdDb(float db) noexcept { gateThresholdDb_ = std::clamp(db, -90.0f, 0.0f); }
    void setGateEnabled(bool enabled) noexcept { gateEnabled_ = enabled; }

    // right may be null (mono input). out must hold numSamples floats.
    void process(const float* left, const float* right, float* out, int numSamples) noexcept {
        if (out == nullptr) return;
        if (left == nullptr || numSamples <= 0) { for (int i = 0; i < numSamples; ++i) out[i] = 0.0f; return; }
        const bool gateActive = gateEnabled_ && gateThresholdDb_ > -80.0f;
        const double threshold = std::pow(10.0, static_cast<double>(gateThresholdDb_) / 20.0);
        double sumSq = 0.0;
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            double x;
            switch (mode_) {
                case InputMode::LeftOnly: x = left[i]; break;
                case InputMode::RightOnly: x = right != nullptr ? right[i] : left[i]; break;
                case InputMode::AutoSum: default: x = right != nullptr ? 0.5 * (static_cast<double>(left[i]) + right[i]) : left[i]; break;
            }
            if (!std::isfinite(x)) x = 0.0;
            // DC blocker then high-pass (transposed direct form II).
            const double dc = x - dcX_ + 0.995 * dcY_;
            dcX_ = x; dcY_ = dc;
            const double y = b0_ * dc + z1_;
            z1_ = b1_ * dc + z2_ - a1_ * y;
            z2_ = b2_ * dc - a2_ * y;
            double v = y;

            const double a = std::abs(v);
            peak_ = std::max(a, peak_ * peakDecay_);
            double gain = manualGain_;
            if (autoGainEnabled_) {
                const double target = peak_ > 1.0e-4 ? std::clamp(0.35 / peak_, 0.5, 16.0) : autoGain_;
                autoGain_ += (target - autoGain_) * 2.0e-5;
                gain *= autoGain_;
            }
            v *= gain;

            env_ += ((a * gain) > env_ ? envAttack_ : envRelease_) * ((a * gain) - env_);
            if (gateActive) {
                if (gateOpenState_ && env_ < threshold * 0.7) gateOpenState_ = false;
                else if (!gateOpenState_ && env_ > threshold) gateOpenState_ = true;
                const double goal = gateOpenState_ ? 1.0 : 0.0;
                gate_ += (goal - gate_) * (goal > gate_ ? gateOpenCoef_ : gateCloseCoef_);
                v *= gate_;
            } else {
                gate_ = 1.0; gateOpenState_ = true;
            }
            const float f = static_cast<float>(v);
            out[i] = f;
            sumSq += static_cast<double>(f) * f;
            peak = std::max(peak, std::abs(f));
        }
        levelPeak_ = peak;
        levelRms_ = static_cast<float>(std::sqrt(sumSq / numSamples));
    }

    [[nodiscard]] float lastBlockPeak() const noexcept { return levelPeak_; }
    [[nodiscard]] float lastBlockRms() const noexcept { return levelRms_; }
    [[nodiscard]] bool gateOpen() const noexcept { return gateOpenState_; }

private:
    double sampleRate_{48000.0};
    InputMode mode_{InputMode::AutoSum};
    double manualGain_{1.0};
    bool autoGainEnabled_{false};
    bool gateEnabled_{true};
    float gateThresholdDb_{-90.0f};
    double b0_{1}, b1_{0}, b2_{0}, a1_{0}, a2_{0};
    double z1_{0}, z2_{0}, dcX_{0}, dcY_{0};
    double env_{0}, peak_{0}, autoGain_{1}, gate_{1};
    bool gateOpenState_{true};
    double envAttack_{0.05}, envRelease_{0.0003}, peakDecay_{0.99999}, gateOpenCoef_{0.1}, gateCloseCoef_{0.001};
    float levelPeak_{0.0f}, levelRms_{0.0f};
};

} // namespace robodrummer
