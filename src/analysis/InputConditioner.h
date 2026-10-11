// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace robodrummer {

enum class InputChannelMode { Left, Right, StereoSum }; // Left also serves mono inputs

// Analysis-path conditioning only; the audible signal is never touched.
class InputConditioner {
public:
    void prepare(double sampleRate, int /*maxBlock*/ = 0) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        dcCoeff_ = static_cast<float>(std::exp(-2.0 * 3.14159265358979323846 * 5.0 / sampleRate_));
        meterRelease_ = static_cast<float>(std::exp(-1.0 / (sampleRate_ * 0.3)));
        rmsCoeff_ = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate_ * 0.1)));
        updateHighPass();
        holdSamples_ = static_cast<int>(holdSeconds_ * sampleRate_);
        reset();
    }

    void reset() noexcept {
        dcX1_ = dcY1_ = 0.0f;
        z1_ = z2_ = 0.0f;
        peakEnv_ = 0.0f;
        rmsSq_ = 0.0f;
        gateOpen_ = false;
        holdCounter_ = 0;
        gateGain_ = 1.0f;
        peakDb_.store(-120.0f, std::memory_order_relaxed);
        rmsDb_.store(-120.0f, std::memory_order_relaxed);
    }

    void setChannelMode(InputChannelMode m) noexcept { mode_ = m; }
    void setTrimDb(float db) noexcept {
        db = std::isfinite(db) ? std::clamp(db, -24.0f, 24.0f) : 0.0f;
        trimDb_ = db;
        trim_ = std::pow(10.0f, db / 20.0f);
    }
    void setHighPassHz(float hz) noexcept {
        hpHz_ = std::isfinite(hz) ? std::clamp(hz, 20.0f, 400.0f) : 70.0f;
        updateHighPass();
    }
    void setGateEnabled(bool on) noexcept { gateEnabled_ = on; }
    void setGateThresholdDb(float db) noexcept { gateOpenDb_ = std::isfinite(db) ? std::clamp(db, -90.0f, 0.0f) : -60.0f; }
    void setGateHysteresisDb(float db) noexcept { hysteresisDb_ = std::isfinite(db) ? std::clamp(db, 0.0f, 24.0f) : 6.0f; }
    void setGateHoldSeconds(float s) noexcept {
        holdSeconds_ = std::isfinite(s) ? std::clamp(s, 0.0f, 2.0f) : 0.1f;
        holdSamples_ = static_cast<int>(holdSeconds_ * sampleRate_);
    }

    [[nodiscard]] InputChannelMode channelMode() const noexcept { return mode_; }
    [[nodiscard]] float trimDb() const noexcept { return trimDb_; }
    [[nodiscard]] float highPassHz() const noexcept { return hpHz_; }
    [[nodiscard]] bool gateEnabled() const noexcept { return gateEnabled_; }
    [[nodiscard]] float gateThresholdDb() const noexcept { return gateOpenDb_; }
    [[nodiscard]] bool gateOpen() const noexcept { return !gateEnabled_ || gateOpen_; }

    // Meter values (dBFS) for UI; safe to read from any thread.
    [[nodiscard]] float peakDb() const noexcept { return peakDb_.load(std::memory_order_relaxed); }
    [[nodiscard]] float rmsDb() const noexcept { return rmsDb_.load(std::memory_order_relaxed); }

    // monoOut must hold numSamples floats. Missing channels yield silence.
    void process(const float* const* channels, int numChannels, int numSamples, float* monoOut) noexcept {
        if (monoOut == nullptr || numSamples <= 0) return;
        const bool haveL = channels != nullptr && numChannels >= 1 && channels[0] != nullptr;
        const bool haveR = channels != nullptr && numChannels >= 2 && channels[1] != nullptr;
        const float* l = haveL ? channels[0] : nullptr;
        const float* r = haveR ? channels[1] : nullptr;
        const float openLin = std::pow(10.0f, gateOpenDb_ / 20.0f);
        const float closeLin = std::pow(10.0f, (gateOpenDb_ - hysteresisDb_) / 20.0f);
        float blockPeak = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            float x = 0.0f;
            switch (mode_) {
                case InputChannelMode::Left: x = l ? sane(l[i]) : (r ? sane(r[i]) : 0.0f); break;
                case InputChannelMode::Right: x = r ? sane(r[i]) : (l ? sane(l[i]) : 0.0f); break;
                case InputChannelMode::StereoSum:
                    x = (l && r) ? 0.5f * (sane(l[i]) + sane(r[i])) : (l ? sane(l[i]) : (r ? sane(r[i]) : 0.0f));
                    break;
            }
            x *= trim_;
            // DC blocker
            const float dc = x - dcX1_ + dcCoeff_ * dcY1_;
            dcX1_ = x; dcY1_ = std::isfinite(dc) ? dc : 0.0f;
            // RBJ biquad high-pass, transposed direct form II
            float y = b0_ * dcY1_ + z1_;
            z1_ = b1_ * dcY1_ - a1_ * y + z2_;
            z2_ = b2_ * dcY1_ - a2_ * y;
            if (!std::isfinite(y)) { y = 0.0f; z1_ = z2_ = 0.0f; }

            if (gateEnabled_) {
                const float a = std::abs(y);
                peakEnv_ = std::max(a, peakEnv_ * gateRelease_);
                if (peakEnv_ >= openLin) { gateOpen_ = true; holdCounter_ = holdSamples_; }
                else if (gateOpen_ && peakEnv_ < closeLin) {
                    if (holdCounter_ > 0) --holdCounter_; else gateOpen_ = false;
                }
                const float target = gateOpen_ ? 1.0f : 0.0f;
                gateGain_ += (target - gateGain_) * 0.01f;
                y *= gateGain_;
            } else {
                gateGain_ = 1.0f;
            }

            monoOut[i] = y;
            const float a = std::abs(y);
            blockPeak = std::max(blockPeak, a);
            rmsSq_ += rmsCoeff_ * (y * y - rmsSq_);
        }

        meterPeak_ = std::max(blockPeak, meterPeak_ * std::pow(meterRelease_, static_cast<float>(numSamples)));
        peakDb_.store(toDb(meterPeak_), std::memory_order_relaxed);
        rmsDb_.store(toDb(std::sqrt(std::max(0.0f, rmsSq_))), std::memory_order_relaxed);
    }

private:
    static float sane(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }
    static float toDb(float lin) noexcept { return lin > 1.0e-6f ? 20.0f * std::log10(lin) : -120.0f; }

    void updateHighPass() noexcept {
        const double w0 = 2.0 * 3.14159265358979323846 * std::min<double>(hpHz_, sampleRate_ * 0.45) / sampleRate_;
        const double q = 0.70710678;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double cw = std::cos(w0);
        const double a0 = 1.0 + alpha;
        b0_ = static_cast<float>((1.0 + cw) * 0.5 / a0);
        b1_ = static_cast<float>(-(1.0 + cw) / a0);
        b2_ = b0_;
        a1_ = static_cast<float>(-2.0 * cw / a0);
        a2_ = static_cast<float>((1.0 - alpha) / a0);
    }

    double sampleRate_{48000.0};
    InputChannelMode mode_{InputChannelMode::Left};
    float trimDb_{0.0f}, trim_{1.0f};
    float hpHz_{70.0f};
    float b0_{1.0f}, b1_{0.0f}, b2_{0.0f}, a1_{0.0f}, a2_{0.0f};
    float dcCoeff_{0.999f};
    float dcX1_{0.0f}, dcY1_{0.0f}, z1_{0.0f}, z2_{0.0f};
    bool gateEnabled_{false};
    float gateOpenDb_{-60.0f}, hysteresisDb_{6.0f}, holdSeconds_{0.1f};
    float gateRelease_{0.9995f};
    int holdSamples_{4800};
    int holdCounter_{0};
    bool gateOpen_{false};
    float gateGain_{1.0f};
    float peakEnv_{0.0f};
    float rmsSq_{0.0f}, rmsCoeff_{0.0002f};
    float meterPeak_{0.0f}, meterRelease_{0.9999f};
    std::atomic<float> peakDb_{-120.0f};
    std::atomic<float> rmsDb_{-120.0f};
};

} // namespace robodrummer
