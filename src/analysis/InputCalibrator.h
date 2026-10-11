// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "analysis/OnsetDetector.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace robodrummer {

enum class CalibrationState { Idle, Running, Done, FailedTooQuiet };

struct CalibrationResult {
    float peakDb{-120.0f};
    float rmsDb{-120.0f};          // RMS over active (non-floor) windows
    float noiseFloorDb{-120.0f};
    float dynamicRangeDb{0.0f};    // active-window p95 minus p10 loudness
    float onsetStrengthMean{0.0f};
    float onsetStrengthP90{0.0f};
    float eventsPerSecond{0.0f};
    float recommendedTrimDb{0.0f};      // -24..24, aims at about -12 dBFS peak
    float recommendedGateThresholdDb{-60.0f};
    float recommendedOnsetSensitivity{1.0f}; // 0.25..4, for AdaptiveOnsetDetector::setSensitivity
};

// Feed conditioned (post-InputConditioner) audio. Not thread-safe; call from the audio thread.
class InputCalibrator {
public:
    static constexpr float MinUsablePeakDb = -55.0f;

    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        windowSamples_ = std::max(64, static_cast<int>(sampleRate_ * 0.02));
        detector_.prepare(sampleRate_);
        stop();
    }

    void start(float seconds = 8.0f) noexcept {
        durationSamples_ = static_cast<long long>(std::clamp(std::isfinite(seconds) ? seconds : 8.0f, 1.0f, 30.0f) * sampleRate_);
        elapsed_ = 0;
        windowAcc_ = 0.0; windowCount_ = 0; windowIndex_ = 0;
        windowDbs_.fill(-120.0f);
        peak_ = 0.0f;
        strengthCount_ = 0; strengths_.fill(0.0f);
        detector_.reset();
        result_ = {};
        state_ = CalibrationState::Running;
    }

    void stop() noexcept { state_ = CalibrationState::Idle; }

    void process(const float* mono, int numSamples) noexcept {
        if (state_ != CalibrationState::Running || mono == nullptr || numSamples <= 0) return;
        const int n = static_cast<int>(std::min<long long>(numSamples, durationSamples_ - elapsed_));
        for (int i = 0; i < n; ++i) {
            const float x = std::isfinite(mono[i]) ? mono[i] : 0.0f;
            peak_ = std::max(peak_, std::abs(x));
            windowAcc_ += static_cast<double>(x) * x;
            if (++windowCount_ >= windowSamples_) {
                if (windowIndex_ < windowDbs_.size())
                    windowDbs_[windowIndex_++] = toDb(static_cast<float>(std::sqrt(windowAcc_ / windowCount_)));
                windowAcc_ = 0.0; windowCount_ = 0;
            }
        }
        std::array<OnsetEvent, 32> ev{};
        int done = 0;
        while (done < n) {
            const int chunk = std::min(n - done, 512);
            const auto c = detector_.processBlock(mono + done, chunk, ev.data(), ev.size());
            for (std::size_t k = 0; k < c; ++k)
                if (strengthCount_ < strengths_.size()) strengths_[strengthCount_++] = ev[k].strength;
            done += chunk;
        }
        elapsed_ += n;
        if (elapsed_ >= durationSamples_) finish();
    }

    [[nodiscard]] CalibrationState state() const noexcept { return state_; }
    [[nodiscard]] float progress() const noexcept {
        if (state_ == CalibrationState::Done || state_ == CalibrationState::FailedTooQuiet) return 1.0f;
        if (state_ == CalibrationState::Idle || durationSamples_ <= 0) return 0.0f;
        return std::clamp(static_cast<float>(static_cast<double>(elapsed_) / static_cast<double>(durationSamples_)), 0.0f, 1.0f);
    }
    [[nodiscard]] const CalibrationResult& result() const noexcept { return result_; }

private:
    static float toDb(float lin) noexcept { return lin > 1.0e-6f ? 20.0f * std::log10(lin) : -120.0f; }

    void finish() noexcept {
        CalibrationResult r;
        r.peakDb = toDb(peak_);
        // Sorted copy of window levels (insertion into scratch; fixed size, no allocation).
        std::array<float, MaxWindows> sorted = windowDbs_;
        const std::size_t n = windowIndex_;
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n));
        auto pct = [&](float p) { return n ? sorted[std::min(n - 1, static_cast<std::size_t>(p * static_cast<float>(n)))] : -120.0f; };
        r.noiseFloorDb = pct(0.10f);
        const float p95 = pct(0.95f);
        // Active windows: clearly above the floor.
        double acc = 0.0; std::size_t active = 0;
        for (std::size_t i = 0; i < n; ++i)
            if (windowDbs_[i] > r.noiseFloorDb + 10.0f) { acc += std::pow(10.0, windowDbs_[i] / 10.0); ++active; }
        r.rmsDb = active ? static_cast<float>(10.0 * std::log10(acc / static_cast<double>(active))) : p95;
        r.dynamicRangeDb = std::max(0.0f, p95 - r.noiseFloorDb);

        std::array<float, MaxStrengths> s = strengths_;
        std::sort(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(strengthCount_));
        if (strengthCount_) {
            float m = 0; for (std::size_t i = 0; i < strengthCount_; ++i) m += s[i];
            r.onsetStrengthMean = m / static_cast<float>(strengthCount_);
            r.onsetStrengthP90 = s[std::min(strengthCount_ - 1, static_cast<std::size_t>(0.9f * static_cast<float>(strengthCount_)))];
        }
        r.eventsPerSecond = static_cast<float>(strengthCount_) / static_cast<float>(static_cast<double>(durationSamples_) / sampleRate_);

        r.recommendedTrimDb = std::clamp(-12.0f - r.peakDb, -24.0f, 24.0f);
        r.recommendedGateThresholdDb = std::clamp(r.noiseFloorDb + r.recommendedTrimDb + 6.0f, -90.0f, -20.0f);
        // Sparse/weak detections -> raise sensitivity; very busy detections -> lower it.
        float sens = 1.0f;
        if (r.eventsPerSecond < 0.5f) sens = 1.6f;
        else if (r.eventsPerSecond > 8.0f) sens = 0.7f;
        if (r.onsetStrengthMean > 0.0f && r.onsetStrengthMean < 0.15f) sens *= 1.2f;
        r.recommendedOnsetSensitivity = std::clamp(sens, 0.25f, 4.0f);

        result_ = r;
        state_ = (r.peakDb < MinUsablePeakDb) ? CalibrationState::FailedTooQuiet : CalibrationState::Done;
    }

    static constexpr std::size_t MaxWindows = 3000;   // 60 s of 20 ms windows
    static constexpr std::size_t MaxStrengths = 512;

    double sampleRate_{48000.0};
    int windowSamples_{960};
    long long durationSamples_{0};
    long long elapsed_{0};
    double windowAcc_{0.0};
    int windowCount_{0};
    std::size_t windowIndex_{0};
    std::array<float, MaxWindows> windowDbs_{};
    std::array<float, MaxStrengths> strengths_{};
    std::size_t strengthCount_{0};
    float peak_{0.0f};
    AdaptiveOnsetDetector detector_{};
    CalibrationResult result_{};
    CalibrationState state_{CalibrationState::Idle};
};

} // namespace robodrummer
