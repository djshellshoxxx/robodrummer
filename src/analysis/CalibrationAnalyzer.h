// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include "analysis/OnsetDetector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace robodrummer {

struct CalibrationResult {
    bool valid{false};
    float noiseFloorDb{-90.0f};
    float typicalRmsDb{-90.0f};
    float loudRmsDb{-90.0f};
    float peakDb{-90.0f};
    float dynamicRangeDb{0.0f};
    float onsetsPerSecond{0.0f};
    float meanOnsetStrength{0.0f};
    float suggestedGateDb{-80.0f};
    float suggestedGainDb{0.0f};
};

// Optional calibration (SPEC 5): the guitarist plays normally for a few seconds and this estimates levels,
// noise floor and transient behaviour to suggest analysis thresholds. All storage is fixed-size.
class CalibrationAnalyzer {
public:
    static constexpr int HistogramBins = 100; // 1 dB bins covering -100..0 dBFS

    void start(double sampleRate, double seconds = 8.0) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        detector_.prepare(sampleRate_);
        windowSamples_ = std::max(1, static_cast<int>(sampleRate_ * 0.010));
        totalSamples_ = static_cast<long long>(std::max(1.0, seconds) * sampleRate_);
        elapsed_ = 0; windowEnergy_ = 0.0; windowFill_ = 0;
        histogram_.fill(0); windows_ = 0; peak_ = 0.0f; onsets_ = 0; strengthSum_ = 0.0;
        result_ = {};
        running_ = true;
    }
    void cancel() noexcept { running_ = false; }

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] float progress() const noexcept {
        return totalSamples_ > 0 ? static_cast<float>(std::clamp(static_cast<double>(elapsed_) / static_cast<double>(totalSamples_), 0.0, 1.0)) : 0.0f;
    }
    [[nodiscard]] const CalibrationResult& result() const noexcept { return result_; }

    // Returns true on the block where calibration completes.
    bool process(const float* mono, int numSamples) noexcept {
        if (!running_ || mono == nullptr || numSamples <= 0) return false;
        std::array<OnsetEvent, 32> events{};
        const auto count = detector_.processBlock(mono, numSamples, events.data(), events.size());
        for (std::size_t i = 0; i < count; ++i) { ++onsets_; strengthSum_ += events[i].strength; }
        for (int i = 0; i < numSamples; ++i) {
            const float x = std::isfinite(mono[i]) ? mono[i] : 0.0f;
            peak_ = std::max(peak_, std::abs(x));
            windowEnergy_ += static_cast<double>(x) * x;
            if (++windowFill_ >= windowSamples_) {
                const double rms = std::sqrt(windowEnergy_ / windowFill_);
                const double db = rms > 1.0e-6 ? 20.0 * std::log10(rms) : -100.0;
                const int bin = std::clamp(static_cast<int>(std::floor(db)) + 100, 0, HistogramBins - 1);
                ++histogram_[static_cast<std::size_t>(bin)];
                ++windows_;
                windowEnergy_ = 0.0; windowFill_ = 0;
            }
        }
        elapsed_ += numSamples;
        if (elapsed_ >= totalSamples_) { finish(); return true; }
        return false;
    }

private:
    float percentileDb(double fraction) const noexcept {
        if (windows_ == 0) return -100.0f;
        const long long target = static_cast<long long>(std::ceil(fraction * static_cast<double>(windows_)));
        long long cumulative = 0;
        for (int bin = 0; bin < HistogramBins; ++bin) {
            cumulative += histogram_[static_cast<std::size_t>(bin)];
            if (cumulative >= target) return static_cast<float>(bin - 100) + 0.5f;
        }
        return 0.0f;
    }

    void finish() noexcept {
        running_ = false;
        CalibrationResult r;
        r.valid = windows_ > 20;
        r.noiseFloorDb = percentileDb(0.10);
        r.typicalRmsDb = percentileDb(0.50);
        r.loudRmsDb = percentileDb(0.95);
        r.peakDb = peak_ > 1.0e-6f ? 20.0f * std::log10(peak_) : -100.0f;
        r.dynamicRangeDb = std::max(0.0f, r.loudRmsDb - r.noiseFloorDb);
        const double seconds = static_cast<double>(elapsed_) / sampleRate_;
        r.onsetsPerSecond = seconds > 0.0 ? static_cast<float>(static_cast<double>(onsets_) / seconds) : 0.0f;
        r.meanOnsetStrength = onsets_ > 0 ? static_cast<float>(strengthSum_ / static_cast<double>(onsets_)) : 0.0f;
        // Gate just above the noise floor, but never so high that quiet playing is cut off.
        const float gateCeiling = std::max(-80.0f, std::min(-30.0f, r.loudRmsDb - 20.0f));
        r.suggestedGateDb = std::clamp(r.noiseFloorDb + 9.0f, -80.0f, gateCeiling);
        // Bring the loudest peaks to roughly -9 dBFS for the analysis path.
        r.suggestedGainDb = std::clamp(-9.0f - r.peakDb, -12.0f, 30.0f);
        result_ = r;
    }

    double sampleRate_{48000.0};
    AdaptiveOnsetDetector detector_{};
    int windowSamples_{480};
    long long totalSamples_{0};
    long long elapsed_{0};
    double windowEnergy_{0.0};
    int windowFill_{0};
    std::array<long long, HistogramBins> histogram_{};
    long long windows_{0};
    float peak_{0.0f};
    long long onsets_{0};
    double strengthSum_{0.0};
    CalibrationResult result_{};
    bool running_{false};
};

} // namespace robodrummer
